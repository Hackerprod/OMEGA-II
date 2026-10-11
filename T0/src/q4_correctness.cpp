#include "q4_kernel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

// Declared before the correctness command is run; do not change after observing outputs.
static constexpr float FINAL_ATOL=1.0e-5f;
static constexpr float FINAL_RTOL=1.0e-5f;
static constexpr std::uint64_t BASE_SEED=0x544F30302D513431ULL;

struct Shape{std::size_t O,I,M;bool extremes;const char*name;};

static bool close(float a,float b,double&maxAbs,double&maxRel){
    double diff=std::fabs(static_cast<double>(a)-static_cast<double>(b));
    double rel=diff/std::max(std::fabs(static_cast<double>(b)),1.0e-30);
    maxAbs=std::max(maxAbs,diff);maxRel=std::max(maxRel,rel);
    return diff<=static_cast<double>(FINAL_ATOL)+static_cast<double>(FINAL_RTOL)*std::fabs(static_cast<double>(b));
}

static bool runCase(const Shape&s,std::uint64_t seed){
    auto weights=t0q4::generate_weights(s.O,s.I,seed,s.extremes);
    auto acts=t0q4::generate_activations(s.M,s.I,seed^0x9E3779B97F4A7C15ULL,s.extremes);
    auto ref=t0q4::execute(weights,acts,t0q4::Route::Scalar);
    std::cout<<"{\"record\":\"route\",\"case\":\""<<s.name<<"\",\"route\":\""<<t0q4::route_name(t0q4::Route::Scalar)<<"\",\"role\":\"correctness_reference\",\"integer_group_exact\":true,\"final_pass\":true,\"max_abs_error\":0,\"max_rel_error\":0,\"status\":\"PASS_REFERENCE\"}\n";
    std::vector<std::pair<t0q4::Route,bool>> routes{{t0q4::Route::AVX2,t0q4::avx2_available()},{t0q4::Route::AVXVNNI256,t0q4::avxvnni256_available()}};
    bool all=true;
    std::cout<<"{\"record\":\"case\",\"name\":\""<<s.name<<"\",\"seed\":"<<seed<<",\"O\":"<<s.O<<",\"I\":"<<s.I<<",\"m\":"<<s.M<<",\"extremes\":"<<(s.extremes?"true":"false")<<",\"groups\":"<<(s.O*(s.I/32))<<",\"weight_payload_bytes\":"<<weights.payload_bytes()<<",\"metadata_bytes\":"<<weights.metadata_bytes()<<",\"vector_capacity_bytes\":"<<weights.storage.capacity()<<",\"physical_allocated_bytes\":"<<weights.physical_allocated_bytes()<<",\"final_tolerance\":{\"atol\":"<<FINAL_ATOL<<",\"rtol\":"<<FINAL_RTOL<<"}}\n";
    for(auto r:routes){
        if(!r.second){std::cout<<"{\"record\":\"route\",\"case\":\""<<s.name<<"\",\"route\":\""<<t0q4::route_name(r.first)<<"\",\"status\":\"NOT_AVAILABLE_CPUID\"}\n";continue;}
        auto got=t0q4::execute(weights,acts,r.first);bool groupExact=got.group_dot==ref.group_dot;bool qaFinal=got.output.size()==ref.output.size();double qaMaxAbs=0,qaMaxRel=0;
        if(qaFinal)for(std::size_t i=0;i<got.output.size();++i)qaFinal=close(got.output[i],ref.output[i],qaMaxAbs,qaMaxRel)&&qaFinal;
        t0q4::validate_inputs(weights,acts);
        t0q4::TimedOutput timed;t0q4::prepare_timed_output(timed,s.M,s.O);
        const auto entry=t0q4::select_prevalidated_kernel(r.first,s.M);
        entry(weights,acts,timed.values.data());
        bool timedFinal=timed.values.size()==ref.output.size();double timedMaxAbs=0,timedMaxRel=0;
        if(timedFinal)for(std::size_t i=0;i<timed.values.size();++i)timedFinal=close(timed.values[i],ref.output[i],timedMaxAbs,timedMaxRel)&&timedFinal;
        bool pass=groupExact&&qaFinal&&timedFinal;all=all&&pass;
        std::cout<<"{\"record\":\"route\",\"case\":\""<<s.name<<"\",\"route\":\""<<t0q4::route_name(r.first)<<"\",\"integer_group_exact\":"<<(groupExact?"true":"false")<<",\"qa_final_pass\":"<<(qaFinal?"true":"false")<<",\"timed_output_pass\":"<<(timedFinal?"true":"false")<<",\"qa_max_abs_error\":"<<std::setprecision(10)<<qaMaxAbs<<",\"timed_max_abs_error\":"<<timedMaxAbs<<",\"timed_max_rel_error\":"<<timedMaxRel<<",\"status\":\""<<(pass?"PASS":"FAIL")<<"\"}\n";
    }
    return all;
}

int main(){
    std::cout<<"{\"record\":\"correctness_plan\",\"generator\":\"SplitMix64\",\"base_seed\":"<<BASE_SEED<<",\"final_atol\":"<<FINAL_ATOL<<",\"final_rtol\":"<<FINAL_RTOL<<",\"measurement\":false}\n";
    const Shape small[]={{1,32,1,false,"small_1x32_m1"},{3,96,4,false,"small_3x96_m4"},{7,160,16,false,"small_7x160_m16"},{5,128,4,true,"edge_5x128_m4"}};
    bool all=true;std::uint64_t s=BASE_SEED;
    for(const auto&x:small)all=runCase(x,s++)&&all;
    const Shape full[]={{512,512,1,false,"full_512x512_m1"},{512,512,2,false,"full_512x512_m2"},{512,512,4,false,"full_512x512_m4"},{512,512,8,false,"full_512x512_m8"},{512,512,16,false,"full_512x512_m16"},{512,512,32,false,"full_512x512_m32"},{512,512,16,true,"full_512x512_m16_extremes"}};
    for(const auto&x:full)all=runCase(x,s++)&&all;
    std::cout<<"{\"record\":\"summary\",\"status\":\""<<(all?"PASS":"FAIL")<<"\",\"routes\":[\"ISA_REF_SCALAR\",\"ISA_BASE_AVX2\",\"ISA_OPT_AVX_VNNI_256 if CPUID\"],\"measurement\":false}\n";
    return all?0:1;
}
