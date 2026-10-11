#include "q4_kernel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

extern "C" unsigned long long phase1_instr_avx2_qa();
extern "C" unsigned long long phase1_instr_vnni_qa();

static constexpr float FINAL_ATOL=1.0e-5f;
static constexpr float FINAL_RTOL=1.0e-5f;
static constexpr int QA_BATCH_CALLS_N=31;
static constexpr std::uint64_t BASE_SEED=0x5048312D51344351ULL;
struct Shape { std::size_t O,I,M; bool extremes; const char* name; };

static bool within(float actual,float reference,double& maxAbs,double& maxRel) {
    const double diff=std::fabs(static_cast<double>(actual)-reference);
    const double rel=diff/std::max(std::fabs(static_cast<double>(reference)),1.0e-30);
    maxAbs=std::max(maxAbs,diff);maxRel=std::max(maxRel,rel);
    return diff<=static_cast<double>(FINAL_ATOL)+static_cast<double>(FINAL_RTOL)*std::fabs(reference);
}

static bool runCase(const Shape& shape,std::uint64_t seed) {
    const auto weights=t0q4::generate_weights(shape.O,shape.I,seed,shape.extremes);
    const auto activations=t0q4::generate_activations(shape.M,shape.I,seed^0x9E3779B97F4A7C15ULL,shape.extremes);
    const auto reference=t0q4::execute(weights,activations,t0q4::Route::Scalar);
    bool passed=true;
    bool groupExactBoth=true,finiteOutputs=true;
    for(float value:reference.output)finiteOutputs=finiteOutputs&&std::isfinite(value);
    std::array<std::vector<float>,2> routeOutputs;
    std::array<bool,2> allclose{};
    std::cout<<"{\"record\":\"case\",\"name\":\""<<shape.name<<"\",\"O\":"<<shape.O<<",\"I\":"<<shape.I<<",\"m\":"<<shape.M
             <<",\"extremes\":"<<(shape.extremes?"true":"false")<<",\"weight_payload_bytes\":"<<weights.payload_bytes()
             <<",\"physical_allocated_bytes\":"<<weights.physical_allocated_bytes()<<",\"measurement\":false}\n";
    int routeIndex=0;
    for(const auto route:{t0q4::Route::AVX2,t0q4::Route::AVXVNNI256}) {
        if((route==t0q4::Route::AVX2&&!t0q4::avx2_available()) ||
           (route==t0q4::Route::AVXVNNI256&&!t0q4::avxvnni256_available())) {
            std::cout<<"{\"record\":\"route\",\"case\":\""<<shape.name<<"\",\"route\":\""<<t0q4::route_name(route)<<"\",\"status\":\"NOT_AVAILABLE_CPUID\"}\n";
            return false;
        }
        const auto qa=t0q4::execute(weights,activations,route);
        const bool groupExact=qa.group_dot==reference.group_dot;
        groupExactBoth=groupExactBoth&&groupExact;
        double qaMaxAbs=0,qaMaxRel=0,timedMaxAbs=0,timedMaxRel=0;
        bool qaPass=qa.output.size()==reference.output.size();
        if(qaPass)for(std::size_t i=0;i<reference.output.size();++i)
            qaPass=within(qa.output[i],reference.output[i],qaMaxAbs,qaMaxRel)&&qaPass;

        // Exercise the exact output-only entrypoint used by the timer, with all preparation outside it.
        t0q4::validate_inputs(weights,activations);
        t0q4::TimedOutput timed;
        t0q4::prepare_timed_output(timed,shape.M,shape.O);
        const auto entry=t0q4::select_prevalidated_kernel(route,shape.M);
        entry(weights,activations,timed.values.data());
        bool timedPass=timed.values.size()==reference.output.size();
        if(timedPass)for(std::size_t i=0;i<reference.output.size();++i)
            timedPass=within(timed.values[i],reference.output[i],timedMaxAbs,timedMaxRel)&&timedPass;
        allclose[routeIndex]=qaPass&&timedPass;
        for(float value:qa.output)finiteOutputs=finiteOutputs&&std::isfinite(value);
        for(float value:timed.values)finiteOutputs=finiteOutputs&&std::isfinite(value);
        const auto batchEntry=t0q4::select_prevalidated_batch_kernel(route,shape.M);
        std::vector<float> batchOne(shape.M*shape.O),batchMany(shape.M*shape.O);
        t0q4::g_qa_batch_call_count=0;batchEntry(weights,activations,batchOne.data(),1);
        const bool countOne=t0q4::g_qa_batch_call_count==1;
        t0q4::g_qa_batch_call_count=0;batchEntry(weights,activations,batchMany.data(),QA_BATCH_CALLS_N);
        const bool countMany=t0q4::g_qa_batch_call_count==QA_BATCH_CALLS_N;
        const bool batchLoopPass=countOne&&countMany&&std::memcmp(batchOne.data(),batchMany.data(),batchOne.size()*sizeof(float))==0;
        routeOutputs[routeIndex]=std::move(timed.values);
        const bool routePass=groupExact&&qaPass&&timedPass&&batchLoopPass;
        passed=passed&&routePass;
        std::cout<<"{\"record\":\"route\",\"case\":\""<<shape.name<<"\",\"route\":\""<<t0q4::route_name(route)
                 <<"\",\"integer_group_exact\":"<<(groupExact?"true":"false")<<",\"qa_output_pass\":"<<(qaPass?"true":"false")
                  <<",\"timed_output_pass\":"<<(timedPass?"true":"false")<<",\"qa_max_abs_error\":"<<std::setprecision(10)<<qaMaxAbs
                  <<",\"timed_max_abs_error\":"<<timedMaxAbs<<",\"timed_max_rel_error\":"<<timedMaxRel
                  <<",\"batch_calls_1_vs_N_sink_pass\":"<<(batchLoopPass?"true":"false")
                  <<",\"status\":\""<<(routePass?"PASS":"FAIL")<<"\"}\n";
        ++routeIndex;
    }
    const bool routesBitExact=routeOutputs[0].size()==routeOutputs[1].size()&&
        (routeOutputs[0].empty()||std::memcmp(routeOutputs[0].data(),routeOutputs[1].data(),routeOutputs[0].size()*sizeof(float))==0);
    const bool fixturePass=groupExactBoth&&allclose[0]&&allclose[1]&&routesBitExact&&finiteOutputs;
    std::cout<<"{\"record\":\"fixture_correctness\",\"case\":\""<<shape.name
             <<"\",\"integer_group_exact\":"<<(groupExactBoth?"true":"false")
             <<",\"AVX2_vs_ref_allclose\":"<<(allclose[0]?"true":"false")
             <<",\"VNNI_vs_ref_allclose\":"<<(allclose[1]?"true":"false")
             <<",\"AVX2_vs_VNNI_bit_exact\":"<<(routesBitExact?"true":"false")
             <<",\"finite_outputs\":"<<(finiteOutputs?"true":"false")
             <<",\"status\":\""<<(fixturePass?"PASS":"FAIL")<<"\"}\n";
    passed=passed&&fixturePass;
    return passed;
}

static bool rejectsUnsupportedMatrixizationM() {
    const auto weights=t0q4::generate_weights(1,32,BASE_SEED);
    bool passed=true;
    for(const std::size_t M:{3u,6u}){
        const auto activations=t0q4::generate_activations(M,32,BASE_SEED^M);
        bool rejected=false;
        try{t0q4::validate_inputs(weights,activations);}
        catch(const std::invalid_argument&){rejected=true;}
        std::cout<<"{\"record\":\"unsupported_matrixization_guard\",\"M\":"<<M
                 <<",\"rejected_before_kernel_dispatch\":"<<(rejected?"true":"false")<<",\"measurement\":false}\n";
        passed=passed&&rejected;
    }
    return passed;
}

int main() {
    std::cout<<"{\"record\":\"correctness_plan\",\"base_seed\":"<<BASE_SEED<<",\"atol\":"<<FINAL_ATOL
             <<",\"rtol\":"<<FINAL_RTOL<<",\"group_dot_bit_exact\":true,\"measurement\":false}\n";
    const long long oneGroupDot=127LL*7LL+(-127LL)*(-7LL);
    unsigned long long expectedInstr=0;
    for(unsigned long long acc=0;acc<8;++acc)expectedInstr+=static_cast<unsigned long long>(oneGroupDot)+acc;
    const bool avx2InstrCorrect=phase1_instr_avx2_qa()==expectedInstr;
    const bool vnniInstrCorrect=phase1_instr_vnni_qa()==expectedInstr;
    std::cout<<"{\"record\":\"p_instr_primitive_correctness\",\"AVX2_VPMADDWD_VPADDD\":"<<(avx2InstrCorrect?"true":"false")
             <<",\"AVX_VNNI_256_VPDPWSSD\":"<<(vnniInstrCorrect?"true":"false")<<",\"measurement\":false}\n";
    const Shape cases[]={
        {1,32,1,false,"small_1x32_m1"},{3,96,4,false,"small_3x96_m4"},
        {7,160,16,false,"small_7x160_m16"},{5,128,4,true,"edge_5x128_m4"},
        {512,512,1,false,"full_512x512_m1"},{512,512,2,false,"full_512x512_m2"},
        {512,512,4,false,"full_512x512_m4"},{512,512,8,false,"full_512x512_m8"},
        {512,512,16,false,"full_512x512_m16"},{512,512,32,false,"full_512x512_m32"},
        {512,512,16,true,"full_512x512_m16_extremes"}
    };
    const bool unsupportedMGuardPass=rejectsUnsupportedMatrixizationM();
    bool passed=avx2InstrCorrect&&vnniInstrCorrect&&unsupportedMGuardPass;
    for(std::size_t i=0;i<std::size(cases);++i) passed=runCase(cases[i],BASE_SEED+i)&&passed;
    std::cout<<"{\"record\":\"summary\",\"status\":\""<<(passed?"PASS":"FAIL")
             <<"\",\"routes\":[\"ISA_REF_SCALAR\",\"ISA_BASE_AVX2\",\"ISA_OPT_AVX_VNNI_256\"],\"measurement\":false}\n";
    return passed?0:1;
}
