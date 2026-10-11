#include "q4_kernel.hpp"
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
#ifdef T0_Q4_QA
volatile std::uint64_t g_qa_batch_call_count=0;
#endif
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
__forceinline float half_to_float_fast(std::uint16_t h){
    const __m128i packed=_mm_cvtsi32_si128(static_cast<int>(h));
    return _mm_cvtss_f32(_mm_cvtph_ps(packed));
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

struct DecodedQ4Group { __m256i low,high; float weight_scale; };
__forceinline DecodedQ4Group decode_q4_group(const std::uint8_t* packed){
    const __m128i bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(packed));
    const __m128i mask=_mm_set1_epi8(0x0f);
    const __m128i low_nibbles=_mm_and_si128(bytes,mask);
    const __m128i high_nibbles=_mm_and_si128(_mm_srli_epi16(bytes,4),mask);
    const __m128i first=_mm_unpacklo_epi8(low_nibbles,high_nibbles);
    const __m128i second=_mm_unpackhi_epi8(low_nibbles,high_nibbles);
    __m256i low=_mm256_cvtepu8_epi16(first);
    __m256i high=_mm256_cvtepu8_epi16(second);
    low=_mm256_srai_epi16(_mm256_slli_epi16(low,12),12);
    high=_mm256_srai_epi16(_mm256_slli_epi16(high,12),12);
    return {low,high,half_to_float_fast(load_le16(packed+16))};
}

__forceinline __m256i decode_q4_half16(const std::uint8_t* packed8){
    const __m128i bytes=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(packed8));
    const __m128i mask=_mm_set1_epi8(0x0f);
    const __m128i low=_mm_and_si128(bytes,mask);
    const __m128i high=_mm_and_si128(_mm_srli_epi16(bytes,4),mask);
    const __m128i ordered=_mm_unpacklo_epi8(low,high);
    const __m256i words=_mm256_cvtepu8_epi16(ordered);
    return _mm256_srai_epi16(_mm256_slli_epi16(words,12),12);
}

__forceinline float horizontal_sum(__m256 value){
    __m128 sum=_mm_add_ps(_mm256_castps256_ps128(value),_mm256_extractf128_ps(value,1));
    sum=_mm_hadd_ps(sum,sum);
    sum=_mm_hadd_ps(sum,sum);
    return _mm_cvtss_f32(sum);
}

template<Route route>
__forceinline __m256 accumulate_q4_group(__m256 current,const std::int8_t* activation,
                                         const DecodedQ4Group& weights,float activation_scale){
    const __m128i act_low_bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(activation));
    const __m128i act_high_bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(activation+16));
    const __m256i act_low=_mm256_cvtepi8_epi16(act_low_bytes);
    const __m256i act_high=_mm256_cvtepi8_epi16(act_high_bytes);
    __m256i dot;
    if constexpr(route==Route::AVX2){
        dot=_mm256_add_epi32(_mm256_madd_epi16(act_low,weights.low),_mm256_madd_epi16(act_high,weights.high));
    }else{
        dot=_mm256_dpwssd_avx_epi32(_mm256_setzero_si256(),act_low,weights.low);
        dot=_mm256_dpwssd_avx_epi32(dot,act_high,weights.high);
    }
    const float scale=activation_scale*weights.weight_scale;
    const __m256 contribution=_mm256_mul_ps(_mm256_cvtepi32_ps(dot),_mm256_set1_ps(scale));
    return _mm256_add_ps(current,contribution);
}

template<Route route,int blockSize>
__forceinline void execute_output_block(const WeightMatrix& weights,const Activations& activations,std::size_t o,
                                       std::size_t mBase,float* output){
    const std::size_t groups=weights.groups_per_row;
    __m256 acc0=_mm256_setzero_ps(),acc1=_mm256_setzero_ps(),acc2=_mm256_setzero_ps(),acc3=_mm256_setzero_ps();
    for(std::size_t g=0;g<groups;++g){
        const std::uint8_t* packed=weights.storage.data()+(o*groups+g)*18;
        const DecodedQ4Group decoded=decode_q4_group(packed);
        if constexpr(blockSize>=1) acc0=accumulate_q4_group<route>(acc0,activations.q.data()+(mBase+0)*activations.I+g*32,decoded,activations.scale[(mBase+0)*groups+g]);
        if constexpr(blockSize>=2) acc1=accumulate_q4_group<route>(acc1,activations.q.data()+(mBase+1)*activations.I+g*32,decoded,activations.scale[(mBase+1)*groups+g]);
        if constexpr(blockSize>=3) acc2=accumulate_q4_group<route>(acc2,activations.q.data()+(mBase+2)*activations.I+g*32,decoded,activations.scale[(mBase+2)*groups+g]);
        if constexpr(blockSize>=4) acc3=accumulate_q4_group<route>(acc3,activations.q.data()+(mBase+3)*activations.I+g*32,decoded,activations.scale[(mBase+3)*groups+g]);
    }
    output[(mBase+0)*weights.O+o]=horizontal_sum(acc0);
    if constexpr(blockSize>=2) output[(mBase+1)*weights.O+o]=horizontal_sum(acc1);
    if constexpr(blockSize>=3) output[(mBase+2)*weights.O+o]=horizontal_sum(acc2);
    if constexpr(blockSize>=4) output[(mBase+3)*weights.O+o]=horizontal_sum(acc3);
}

template<Route route,int blockSize>
__declspec(noinline) void execute_core_fixed_m(const WeightMatrix& weights,const Activations& activations,float* output){
    for(std::size_t o=0;o<weights.O;++o)
        for(std::size_t m=0;m<activations.M;m+=blockSize)
            execute_output_block<route,blockSize>(weights,activations,o,m,output);
}

template<Route route>
__declspec(noinline) void execute_core_m1(const WeightMatrix& weights,const Activations& activations,float* output){
    const std::size_t groups=weights.groups_per_row;
    for(std::size_t o=0;o<weights.O;++o){
        __m256 accum=_mm256_setzero_ps();
        for(std::size_t g=0;g<groups;++g){
            const std::uint8_t* packed=weights.storage.data()+(o*groups+g)*18;
            const __m256i w0=decode_q4_half16(packed);
            const __m256i w1=decode_q4_half16(packed+8);
            const std::int8_t* activation=activations.q.data()+g*32;
            const __m256i a0=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(activation)));
            const __m256i a1=_mm256_cvtepi8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(activation+16)));
            __m256i dot;
            if constexpr(route==Route::AVX2)dot=_mm256_add_epi32(_mm256_madd_epi16(a0,w0),_mm256_madd_epi16(a1,w1));
            else{dot=_mm256_dpwssd_avx_epi32(_mm256_setzero_si256(),a0,w0);dot=_mm256_dpwssd_avx_epi32(dot,a1,w1);}
            const float scale=activations.scale[g]*half_to_float_fast(load_le16(packed+16));
            accum=_mm256_add_ps(accum,_mm256_mul_ps(_mm256_cvtepi32_ps(dot),_mm256_set1_ps(scale)));
        }
        output[o]=horizontal_sum(accum);
    }
}

static void execute_avx2_m1(const WeightMatrix& w,const Activations& a,float* y){execute_core_m1<Route::AVX2>(w,a,y);}
static void execute_avx2_m2(const WeightMatrix& w,const Activations& a,float* y){execute_core_fixed_m<Route::AVX2,2>(w,a,y);}
static void execute_vnni_m1(const WeightMatrix& w,const Activations& a,float* y){execute_core_m1<Route::AVXVNNI256>(w,a,y);}
static void execute_vnni_m2(const WeightMatrix& w,const Activations& a,float* y){execute_core_fixed_m<Route::AVXVNNI256,2>(w,a,y);}
template<Route route,int blockSize>
__declspec(noinline) static void execute_batch_fixed_m(const WeightMatrix& w,const Activations& a,float* y,int calls){
    for(int call=0;call<calls;++call){
        execute_core_fixed_m<route,blockSize>(w,a,y);
#ifdef T0_Q4_QA
        ++g_qa_batch_call_count;
#endif
    }
}
template<Route route>
__declspec(noinline) static void execute_batch_m1(const WeightMatrix& w,const Activations& a,float* y,int calls){
    for(int call=0;call<calls;++call){
        execute_core_m1<route>(w,a,y);
#ifdef T0_Q4_QA
        ++g_qa_batch_call_count;
#endif
    }
}
static void execute_avx2_m1_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_m1<Route::AVX2>(w,a,y,calls);}
static void execute_avx2_m2_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_fixed_m<Route::AVX2,2>(w,a,y,calls);}
static void execute_vnni_m1_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_m1<Route::AVXVNNI256>(w,a,y,calls);}
static void execute_vnni_m2_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_fixed_m<Route::AVXVNNI256,2>(w,a,y,calls);}
static void execute_avx2_m4plus(const WeightMatrix& w,const Activations& a,float* y){execute_core_fixed_m<Route::AVX2,4>(w,a,y);}
static void execute_vnni_m4plus(const WeightMatrix& w,const Activations& a,float* y){execute_core_fixed_m<Route::AVXVNNI256,4>(w,a,y);}
static void execute_avx2_m4plus_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_fixed_m<Route::AVX2,4>(w,a,y,calls);}
static void execute_vnni_m4plus_batch(const WeightMatrix& w,const Activations& a,float* y,int calls){execute_batch_fixed_m<Route::AVXVNNI256,4>(w,a,y,calls);}
#ifdef T0_Q4_QA
std::int8_t unpack_nibble(std::uint8_t byte,unsigned index){
    unsigned n=(index&1u)?(byte>>4):(byte&0x0fu);
    return static_cast<std::int8_t>(n>=8?static_cast<int>(n)-16:static_cast<int>(n));
}
void unpack_group(const std::uint8_t*packed,std::int16_t*w){
    for(unsigned j=0;j<32;++j)w[j]=unpack_nibble(packed[j/2],j);
}
#endif
#ifdef T0_Q4_QA
void unpack_group_simd(const std::uint8_t*packed,std::int16_t*w){
    const __m128i bytes=_mm_loadu_si128(reinterpret_cast<const __m128i*>(packed));
    const __m128i mask=_mm_set1_epi8(0x0f);
    const __m128i low=_mm_and_si128(bytes,mask);
    const __m128i high=_mm_and_si128(_mm_srli_epi16(bytes,4),mask);
    const __m128i first=_mm_unpacklo_epi8(low,high);
    const __m128i second=_mm_unpackhi_epi8(low,high);
    __m256i firstWords=_mm256_cvtepu8_epi16(first);
    __m256i secondWords=_mm256_cvtepu8_epi16(second);
    firstWords=_mm256_srai_epi16(_mm256_slli_epi16(firstWords,12),12);
    secondWords=_mm256_srai_epi16(_mm256_slli_epi16(secondWords,12),12);
    _mm256_store_si256(reinterpret_cast<__m256i*>(w),firstWords);
    _mm256_store_si256(reinterpret_cast<__m256i*>(w+16),secondWords);
}
std::int32_t dot_scalar(const std::int8_t*a,const std::int16_t*w){
    std::int32_t sum=0;
    for(unsigned j=0;j<32;++j)sum+=static_cast<std::int32_t>(a[j])*static_cast<std::int32_t>(w[j]);
    return sum;
}
std::int32_t dot_avx2(const std::int16_t*a,const std::int16_t*weights){
    __m256i acc=_mm256_setzero_si256();
    for(int off=0;off<32;off+=16){
        __m256i va=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a+off));
        __m256i vw=_mm256_load_si256(reinterpret_cast<const __m256i*>(weights+off));
        acc=_mm256_add_epi32(acc,_mm256_madd_epi16(va,vw));
    }
    alignas(32) std::int32_t lanes[8];_mm256_store_si256(reinterpret_cast<__m256i*>(lanes),acc);
    std::int64_t total=0;for(auto v:lanes)total+=v;return static_cast<std::int32_t>(total);
}
std::int32_t dot_vnni256(const std::int16_t*a,const std::int16_t*weights){
    __m256i acc=_mm256_setzero_si256();
    for(int off=0;off<32;off+=16){
        __m256i va=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a+off));
        __m256i vw=_mm256_load_si256(reinterpret_cast<const __m256i*>(weights+off));
        acc=_mm256_dpwssd_avx_epi32(acc,va,vw);
    }
    alignas(32) std::int32_t lanes[8];_mm256_store_si256(reinterpret_cast<__m256i*>(lanes),acc);
    std::int64_t total=0;for(auto v:lanes)total+=v;return static_cast<std::int32_t>(total);
}
#endif
}

std::uint64_t WeightMatrix::metadata_bytes()const{return sizeof(WeightMatrix);}
std::uint64_t WeightMatrix::physical_allocated_bytes()const{return static_cast<std::uint64_t>(storage.capacity())+metadata_bytes();}
bool avx2_available(){int r[4]={};__cpuidex(r,1,0);unsigned ecx=static_cast<unsigned>(r[2]);bool osx=(ecx&(1u<<27))!=0;unsigned long long x= osx?_xgetbv(0):0;__cpuidex(r,7,0);return ((static_cast<unsigned>(r[1])&(1u<<5))!=0)&&((x&0x6)==0x6);}
bool avxvnni256_available(){int r[4]={};__cpuidex(r,1,0);unsigned ecx=static_cast<unsigned>(r[2]);bool osx=(ecx&(1u<<27))!=0;unsigned long long x=osx?_xgetbv(0):0;__cpuidex(r,7,0);if(static_cast<unsigned>(r[0])<1)return false;__cpuidex(r,7,1);return ((static_cast<unsigned>(r[0])&(1u<<4))!=0)&&((x&0x6)==0x6);}
bool f16c_available(){int r[4]={};__cpuidex(r,1,0);unsigned ecx=static_cast<unsigned>(r[2]);return avx2_available()&&((ecx&(1u<<29))!=0);}

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

void validate_inputs(const WeightMatrix& weights,const Activations& activations){
    validate(weights,activations);
    const std::size_t M=activations.M;
    if(!(M==1||M==2||(M>=4&&M%4==0)))
        throw std::invalid_argument("unsupported Q4 matrixization M; supported values are 1, 2, or positive multiples of 4");
}
void prepare_timed_output(TimedOutput& output,std::size_t M,std::size_t O){output.values.resize(M*O);}
std::size_t kernel_m_tile(std::size_t M){return M>=4&&M%4==0?4:(M==2?2:1);}

KernelEntry select_prevalidated_kernel(Route route,std::size_t M){
    if(!f16c_available())throw std::runtime_error("F16C is required by the optimized Q4 scale conversion");
    if(M==0)throw std::invalid_argument("M must be positive");
    switch(route){
        case Route::AVX2:if(!avx2_available())throw std::runtime_error("AVX2 is unavailable");return M>=4&&M%4==0?&execute_avx2_m4plus:(M==2?&execute_avx2_m2:&execute_avx2_m1);
        case Route::AVXVNNI256:if(!avxvnni256_available())throw std::runtime_error("AVX-VNNI-256 is unavailable");return M>=4&&M%4==0?&execute_vnni_m4plus:(M==2?&execute_vnni_m2:&execute_vnni_m1);
        default:throw std::invalid_argument("scalar route is QA-only and has no timed kernel entry");
    }
}
KernelBatchEntry select_prevalidated_batch_kernel(Route route,std::size_t M){
    if(!f16c_available())throw std::runtime_error("F16C is required by the optimized Q4 scale conversion");
    if(M==0)throw std::invalid_argument("M must be positive");
    switch(route){
        case Route::AVX2:if(!avx2_available())throw std::runtime_error("AVX2 is unavailable");return M>=4&&M%4==0?&execute_avx2_m4plus_batch:(M==2?&execute_avx2_m2_batch:&execute_avx2_m1_batch);
        case Route::AVXVNNI256:if(!avxvnni256_available())throw std::runtime_error("AVX-VNNI-256 is unavailable");return M>=4&&M%4==0?&execute_vnni_m4plus_batch:(M==2?&execute_vnni_m2_batch:&execute_vnni_m1_batch);
        default:throw std::invalid_argument("scalar route is QA-only and has no timed kernel entry");
    }
}
void execute_into_prechecked(const WeightMatrix& weights,const Activations& activations,Route route,TimedOutput& output){
    select_prevalidated_kernel(route,activations.M)(weights,activations,output.values.data());
}

#ifdef T0_Q4_QA
template<Route route>
static void execute_qa_route(const WeightMatrix& weights,const Activations& activations,Evaluation& result){
    validate(weights,activations);
    const std::size_t groups=weights.groups_per_row;
    result.group_dot.resize(activations.M*weights.O*groups);
    result.output.resize(activations.M*weights.O);
    std::vector<double> accum(activations.M*weights.O,0.0);
    std::vector<std::int16_t> expanded;
    if constexpr(route!=Route::Scalar){
        expanded.resize(activations.M*activations.I);
        for(std::size_t m=0;m<activations.M;++m)for(std::size_t i=0;i<activations.I;i+=16){
            const __m128i packed=_mm_loadu_si128(reinterpret_cast<const __m128i*>(activations.q.data()+m*activations.I+i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(expanded.data()+m*activations.I+i),_mm256_cvtepi8_epi16(packed));
        }
    }
    for(std::size_t o=0;o<weights.O;++o)for(std::size_t g=0;g<groups;++g){
        const std::uint8_t* packed=weights.storage.data()+(o*groups+g)*18;
        alignas(32) std::int16_t unpacked[32];
        if constexpr(route==Route::Scalar)unpack_group(packed,unpacked);
        else unpack_group_simd(packed,unpacked);
        const double wscale=static_cast<double>(half_to_float_fast(load_le16(packed+16)));
        for(std::size_t m=0;m<activations.M;++m){
            const std::size_t activationOffset=m*activations.I+g*32;
            std::int32_t dot=0;
            if constexpr(route==Route::Scalar)dot=dot_scalar(activations.q.data()+activationOffset,unpacked);
            else if constexpr(route==Route::AVX2)dot=dot_avx2(expanded.data()+activationOffset,unpacked);
            else dot=dot_vnni256(expanded.data()+activationOffset,unpacked);
            result.group_dot[(m*weights.O+o)*groups+g]=dot;
            accum[m*weights.O+o]+=static_cast<double>(dot)*static_cast<double>(activations.scale[m*groups+g])*wscale;
        }
    }
    for(std::size_t i=0;i<accum.size();++i)result.output[i]=static_cast<float>(accum[i]);
}

void execute_into(const WeightMatrix& weights,const Activations& activations,Route route,Evaluation& result){
    if(!f16c_available())throw std::runtime_error("F16C is required by the optimized Q4 scale conversion");
    if(route==Route::AVX2&&!avx2_available())throw std::runtime_error("AVX2 route unavailable on executing logical processor");
    if(route==Route::AVXVNNI256&&!avxvnni256_available())throw std::runtime_error("AVX-VNNI-256 route unavailable on executing logical processor");
    switch(route){
        case Route::Scalar:execute_qa_route<Route::Scalar>(weights,activations,result);break;
        case Route::AVX2:execute_qa_route<Route::AVX2>(weights,activations,result);break;
        case Route::AVXVNNI256:execute_qa_route<Route::AVXVNNI256>(weights,activations,result);break;
    }
}
Evaluation execute(const WeightMatrix& weights,const Activations& activations,Route route){
    Evaluation result;execute_into(weights,activations,route,result);return result;
}
#endif

const char* route_name(Route r){switch(r){case Route::Scalar:return "ISA_REF_SCALAR";case Route::AVX2:return "ISA_BASE_AVX2";case Route::AVXVNNI256:return "ISA_OPT_AVX_VNNI_256";}return "UNKNOWN";}
}
