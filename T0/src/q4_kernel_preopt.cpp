#include "q4_kernel_preopt.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <intrin.h>
#include <immintrin.h>

namespace t0q4 {
namespace {
struct SplitMix64 {
    std::uint64_t state;
    explicit SplitMix64(std::uint64_t seed):state(seed){}
    std::uint64_t next(){
        std::uint64_t z=(state+=0x9E3779B97F4A7C15ULL);
        z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;
        z=(z^(z>>27))*0x94D049BB133111EBULL;
        return z^(z>>31);
    }
};
std::uint16_t scale_code(std::size_t group,std::size_t salt){
    // Exact binary fractions, 0.5 through 0.96875 in 1/32 increments.
    return static_cast<std::uint16_t>(0x3800 + ((group+salt)%16)*0x40);
}
float half_to_float(std::uint16_t h){
    const unsigned sign=(h>>15)&1u, exp=(h>>10)&31u, frac=h&1023u;
    float v;
    if(exp==0) v=frac?std::ldexp(static_cast<float>(frac),-24):0.0f;
    else if(exp==31) v=frac?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
    else v=std::ldexp(static_cast<float>(1024u+frac),static_cast<int>(exp)-25);
    return sign?-v:v;
}
std::int8_t unpack_nibble(std::uint8_t byte,unsigned index){
    unsigned n=(index&1u)?(byte>>4):(byte&0x0fu);
    return static_cast<std::int8_t>(n>=8?static_cast<int>(n)-16:static_cast<int>(n));
}
void validate(const WeightMatrix&w,const Activations&a){
    if(!w.O||!w.I||w.I%32||w.groups_per_row!=w.I/32)throw std::invalid_argument("invalid Q4 matrix shape");
    if(a.M==0||a.I!=w.I||a.groups_per_row!=w.groups_per_row)throw std::invalid_argument("invalid activation shape");
    const std::uint64_t expected=static_cast<std::uint64_t>(w.O)*w.groups_per_row*18;
    if(w.storage.size()!=expected)throw std::invalid_argument("Q4 storage size does not match O*groups*18");
    if(a.q.size()!=a.M*a.I||a.scale.size()!=a.M*a.groups_per_row)throw std::invalid_argument("activation arrays do not match dimensions");
    for(float s:a.scale)if(!std::isfinite(s)||s<0)throw std::invalid_argument("activation scale must be finite and nonnegative");
}
std::uint16_t load_le16(const std::uint8_t*p){return static_cast<std::uint16_t>(p[0]|(static_cast<std::uint16_t>(p[1])<<8));}
void unpack_group(const std::uint8_t*packed,std::int16_t*w){
    for(unsigned j=0;j<32;++j)w[j]=unpack_nibble(packed[j/2],j);
}
std::int32_t dot_scalar(const std::int8_t*a,const std::int16_t*w){
    std::int32_t sum=0;
    for(unsigned j=0;j<32;++j)sum+=static_cast<std::int32_t>(a[j])*static_cast<std::int32_t>(w[j]);
    return sum;
}
std::int32_t dot_avx2(const std::int8_t*a,const std::int16_t*weights){
    alignas(32) std::int16_t av[32];for(unsigned j=0;j<32;++j)av[j]=a[j];
    __m256i acc=_mm256_setzero_si256();
    for(int off=0;off<32;off+=16){
        __m256i va=_mm256_load_si256(reinterpret_cast<const __m256i*>(av+off));
        __m256i vw=_mm256_load_si256(reinterpret_cast<const __m256i*>(weights+off));
        acc=_mm256_add_epi32(acc,_mm256_madd_epi16(va,vw));
    }
    alignas(32) std::int32_t lanes[8];_mm256_store_si256(reinterpret_cast<__m256i*>(lanes),acc);
    std::int64_t total=0;for(auto v:lanes)total+=v;return static_cast<std::int32_t>(total);
}
std::int32_t dot_vnni256(const std::int8_t*a,const std::int16_t*weights){
    alignas(32) std::int16_t av[32];for(unsigned j=0;j<32;++j)av[j]=a[j];
    __m256i acc=_mm256_setzero_si256();
    for(int off=0;off<32;off+=16){
        __m256i va=_mm256_load_si256(reinterpret_cast<const __m256i*>(av+off));
        __m256i vw=_mm256_load_si256(reinterpret_cast<const __m256i*>(weights+off));
        acc=_mm256_dpwssd_avx_epi32(acc,va,vw);
    }
    alignas(32) std::int32_t lanes[8];_mm256_store_si256(reinterpret_cast<__m256i*>(lanes),acc);
    std::int64_t total=0;for(auto v:lanes)total+=v;return static_cast<std::int32_t>(total);
}
}

std::uint64_t WeightMatrix::metadata_bytes()const{return sizeof(WeightMatrix);}
std::uint64_t WeightMatrix::physical_allocated_bytes()const{return static_cast<std::uint64_t>(storage.capacity())+metadata_bytes();}
bool avx2_available(){int r[4]={};__cpuidex(r,1,0);unsigned ecx=static_cast<unsigned>(r[2]);bool osx=(ecx&(1u<<27))!=0;unsigned long long x= osx?_xgetbv(0):0;__cpuidex(r,7,0);return ((static_cast<unsigned>(r[1])&(1u<<5))!=0)&&((x&0x6)==0x6);}
bool avxvnni256_available(){int r[4]={};__cpuidex(r,1,0);unsigned ecx=static_cast<unsigned>(r[2]);bool osx=(ecx&(1u<<27))!=0;unsigned long long x=osx?_xgetbv(0):0;__cpuidex(r,7,0);if(static_cast<unsigned>(r[0])<1)return false;__cpuidex(r,7,1);return ((static_cast<unsigned>(r[0])&(1u<<4))!=0)&&((x&0x6)==0x6);}

WeightMatrix generate_weights(std::size_t O,std::size_t I,std::uint64_t seed,bool extremes){
    if(!O||!I||I%32)throw std::invalid_argument("I must be positive and divisible by 32");
    WeightMatrix w;w.O=O;w.I=I;w.groups_per_row=I/32;
    const std::size_t groups=O*w.groups_per_row;w.storage.resize(groups*18);
    SplitMix64 rng(seed);
    for(std::size_t o=0;o<O;++o)for(std::size_t g=0;g<w.groups_per_row;++g){
        const std::size_t gi=o*w.groups_per_row+g,base=gi*18;
        for(unsigned j=0;j<32;j+=2){
            int q0=static_cast<int>(rng.next()%15)-7, q1=static_cast<int>(rng.next()%15)-7;
            if(extremes){q0=(j/2%3==0)?-7:((j/2%3==1)?7:0);q1=((j/2+1)%3==0)?-7:(((j/2+1)%3==1)?7:0);}
            unsigned n0=static_cast<unsigned>(q0)&15u,n1=static_cast<unsigned>(q1)&15u;
            w.storage[base+j/2]=static_cast<std::uint8_t>(n0|(n1<<4));
        }
        const std::uint16_t h=scale_code(g,o);
        w.storage[base+16]=static_cast<std::uint8_t>(h&0xffu);w.storage[base+17]=static_cast<std::uint8_t>(h>>8);
    }
    return w;
}

Activations generate_activations(std::size_t M,std::size_t I,std::uint64_t seed,bool extremes){
    if(!M||!I||I%32)throw std::invalid_argument("M and I must be positive; I divisible by 32");
    Activations a;a.M=M;a.I=I;a.groups_per_row=I/32;a.q.resize(M*I);a.scale.resize(M*a.groups_per_row);SplitMix64 rng(seed);
    for(std::size_t m=0;m<M;++m)for(std::size_t i=0;i<I;++i){int q=static_cast<int>(rng.next()%255)-127;if(extremes){switch(i%3){case 0:q=-127;break;case 1:q=127;break;default:q=0;}}a.q[m*I+i]=static_cast<std::int8_t>(q);}
    for(std::size_t m=0;m<M;++m)for(std::size_t g=0;g<a.groups_per_row;++g)a.scale[m*a.groups_per_row+g]=0.25f+static_cast<float>((g+7*m+(seed&31u))%32)/32.0f;
    return a;
}

static void execute_core(const WeightMatrix&w,const Activations&a,Route route,Evaluation& result){
    validate(w,a);
    const std::size_t G=w.groups_per_row;
    result.group_dot.resize(a.M*w.O*G);result.output.resize(a.M*w.O);result.accumulator.assign(a.M*w.O,0.0);
    // Reuse each packed Q4 group across all M activation rows before advancing the group/weight row.
    for(std::size_t o=0;o<w.O;++o)for(std::size_t g=0;g<G;++g){
        const std::uint8_t* packed=w.storage.data()+(o*G+g)*18;
        alignas(32) std::int16_t unpacked_weights[32];
        unpack_group(packed,unpacked_weights);
        const double wscale=static_cast<double>(half_to_float(load_le16(packed+16)));
        for(std::size_t m=0;m<a.M;++m){
            const std::int8_t* aq=a.q.data()+m*a.I+g*32;
            std::int32_t dot=0;
            if(route==Route::Scalar)dot=dot_scalar(aq,unpacked_weights);
            else if(route==Route::AVX2)dot=dot_avx2(aq,unpacked_weights);
            else dot=dot_vnni256(aq,unpacked_weights);
            const std::size_t gi=(m*w.O+o)*G+g;result.group_dot[gi]=dot;
            result.accumulator[m*w.O+o]+=static_cast<double>(dot)*static_cast<double>(a.scale[m*G+g])*wscale;
        }
    }
    for(std::size_t i=0;i<result.accumulator.size();++i)result.output[i]=static_cast<float>(result.accumulator[i]);
}

void execute_into(const WeightMatrix&w,const Activations&a,Route route,Evaluation& result){
    if(route==Route::AVX2&&!avx2_available())throw std::runtime_error("AVX2 route unavailable on executing logical processor");
    if(route==Route::AVXVNNI256&&!avxvnni256_available())throw std::runtime_error("AVX-VNNI-256 route unavailable on executing logical processor");
    execute_core(w,a,route,result);
}

void execute_into_prechecked(const WeightMatrix&w,const Activations&a,Route route,Evaluation& result){execute_core(w,a,route,result);}

Evaluation execute(const WeightMatrix&w,const Activations&a,Route route){
    Evaluation result;execute_into(w,a,route,result);
    return result;
}

const char* route_name(Route r){switch(r){case Route::Scalar:return "ISA_REF_SCALAR";case Route::AVX2:return "ISA_BASE_AVX2";case Route::AVXVNNI256:return "ISA_OPT_AVX_VNNI_256";}return "UNKNOWN";}
}
