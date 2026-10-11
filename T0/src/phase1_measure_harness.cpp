#define NOMINMAX
#include "q4_kernel.hpp"
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

struct Phase1InstrAsmTiming { unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; };
extern "C" void phase1_instr_avx2(std::uint32_t loops,Phase1InstrAsmTiming* timing,std::int32_t* accumulator_lanes);
extern "C" void phase1_instr_vnni(std::uint32_t loops,Phase1InstrAsmTiming* timing,std::int32_t* accumulator_lanes);

namespace {
constexpr int kN=31;
constexpr int kWarmup=5;
constexpr int kMValues[]={1,2,4,8,16,32};
constexpr std::uint32_t kInstrUnroll=4;
constexpr std::uint64_t kInstrMacsPerIteration=8ULL*16ULL;
constexpr int kIndependentAccumulators=8;
constexpr std::uint64_t kTimedRegionMinNs=100000000ULL;
constexpr std::uint64_t kCalibrationTargetNs=150000000ULL;
constexpr std::size_t kO=512, kI=512;
constexpr std::uint64_t kObservedLlcBytes=31457280ULL;
constexpr std::uint64_t kReadBytesPerWorker=4ULL*kObservedLlcBytes;
constexpr std::uint64_t kECoreOrderSeed=0x4D44323545434F52ULL;
constexpr std::uint64_t kPcoreM1OrderSeed=0x4D44323850314D31ULL;

struct Core { std::string key,type; WORD group; DWORD logical; };
struct TimerResult { long long qpcTicks=0; unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; };
struct InstrResult { TimerResult timer; std::array<std::int32_t,64> lanes{}; };
struct Sample { long long qpcTicks=0; unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; };
struct TeamGate { std::mutex mutex; std::condition_variable cv; int ready=0,done=0; bool go=false; };
struct TeamWorkerResult { long long qpcTicks=0; unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; std::uint64_t sink=0; std::string error; };
struct TeamSample { long long groupQpcTicks=0; std::vector<TeamWorkerResult> workers; };
struct Calibration { std::uint64_t units=1; std::uint64_t elapsedNs=0; };
struct AttemptResult { std::vector<double> rates; std::vector<double> secondaryRates; bool tooShort=false; };
struct OfficialSeriesResult { std::vector<double> rates; std::vector<double> secondaryRates; std::uint64_t fixedUnits=0; int attempt=0; bool recovered=false; };
struct SeriesHold : std::runtime_error { using std::runtime_error::runtime_error; };

long long qpc() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value)) throw std::runtime_error("QueryPerformanceCounter failed");
    return value.QuadPart;
}

template<class Work>
Calibration calibrateUnits(Work work) {
    LARGE_INTEGER frequency{};
    if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    const auto limit=static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    auto duration=[&](std::uint64_t units){const long long start=qpc();work(units);const long long stop=qpc();
        return static_cast<std::uint64_t>(stop-start)*1000000000ULL/static_cast<std::uint64_t>(frequency.QuadPart);};
    std::uint64_t low=0,high=1,highNs=duration(high);
    while(highNs<kCalibrationTargetNs){
        low=high;if(high>=limit)throw std::runtime_error("engineering calibration unit limit reached below 150 ms");
        high=std::min(limit,high*2);highNs=duration(high);
    }
    while(high-low>1){const std::uint64_t mid=low+(high-low)/2;const auto ns=duration(mid);
        if(ns>=kCalibrationTargetNs){high=mid;highNs=ns;}else low=mid;}
    return {high,highNs};
}

template<class Work>
Calibration calibrateByMeasuredNs(Work work) {
    const auto limit=static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    std::uint64_t low=0,high=1,highNs=work(high);
    while(highNs<kCalibrationTargetNs){
        low=high;if(high>=limit)throw std::runtime_error("engineering calibration unit limit reached below 150 ms");
        high=std::min(limit,high*2);highNs=work(high);
    }
    while(high-low>1){const std::uint64_t mid=low+(high-low)/2;const auto ns=work(mid);
        if(ns>=kCalibrationTargetNs){high=mid;highNs=ns;}else low=mid;}
    return {high,highNs};
}

void recordCalibration(const std::string& outputDirectory,const std::string& series,const Calibration& c){
    const std::string path=outputDirectory+"\\engineering_calibration_only.csv";
    const bool exists=GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES;
    std::ofstream out(path,std::ios::binary|std::ios::app);
    if(!out)throw std::runtime_error("cannot write engineering calibration record");
    if(!exists)out<<"series,phase,fixed_units,calibration_elapsed_ns,timed_region_min_ns,calibration_target_ns,statistical_sample\n";
    out<<series<<",ENGINEERING_CALIBRATION_ONLY,"<<c.units<<","<<c.elapsedNs<<","<<kTimedRegionMinNs<<","<<kCalibrationTargetNs<<",false\n";
}

void recordSeriesStatus(const std::string& outputDirectory,const std::string& series,int attempt,std::uint64_t fixedUnits,const char* status){
    const std::string path=outputDirectory+"\\series_status.csv";
    const bool exists=GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES;
    std::ofstream out(path,std::ios::binary|std::ios::app);
    if(!out)throw std::runtime_error("cannot write series status record");
    if(!exists)out<<"series,attempt,fixed_units,N,warmup_count,status,has_official_median\n";
    out<<series<<","<<attempt<<","<<fixedUnits<<","<<kN<<","<<kWarmup<<","<<status<<","<<(std::string(status)=="SERIES_COMPLETE"||std::string(status)=="SERIES_RECOVERED_TECHNICAL_RETRY"?"true":"false")<<"\n";
}

template<class Warmup,class RunAttempt>
OfficialSeriesResult runOfficialSeries(const std::string& outputDirectory,const std::string& series,std::uint64_t calibrationUnits,
                                       Warmup warmup,RunAttempt runAttempt){
    std::uint64_t units=calibrationUnits;
    for(int attempt=1;attempt<=2;++attempt){
        warmup(units);
        AttemptResult measured=runAttempt(units,attempt);
        if(!measured.tooShort){
            if(measured.rates.size()!=kN)throw SeriesHold("official sample count is not N=31 for "+series);
            if(!measured.secondaryRates.empty()&&measured.secondaryRates.size()!=kN)throw SeriesHold("secondary official sample count is not N=31 for "+series);
            recordSeriesStatus(outputDirectory,series,attempt,units,attempt==1?"SERIES_COMPLETE":"SERIES_RECOVERED_TECHNICAL_RETRY");
            return {std::move(measured.rates),std::move(measured.secondaryRates),units,attempt,attempt==2};
        }
        recordSeriesStatus(outputDirectory,series,attempt,units,attempt==1?"SERIES_INVALID_TIMER_TOO_SHORT":"SERIES_HOLD");
        if(attempt==2)throw SeriesHold("series held after second attempt contained elapsed_ns < 100000000: "+series);
        if(units>static_cast<std::uint64_t>(std::numeric_limits<int>::max())/2)
            throw SeriesHold("fixed_units cannot be safely doubled for technical retry: "+series);
        units*=2;
    }
    throw SeriesHold("unreachable series state: "+series);
}

unsigned long long rdtscp(unsigned& aux) {
    _mm_lfence();
    const unsigned long long value=__rdtscp(&aux);
    _mm_lfence();
    return value;
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted=false;
    for (char c:line) {
        if (c=='"') { quoted=!quoted; continue; }
        if (c==',' && !quoted) { cells.push_back(cell); cell.clear(); }
        else cell.push_back(c);
    }
    cells.push_back(cell);
    return cells;
}

std::vector<Core> loadCores(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open topology CSV: "+path);
    std::string line;
    if (!std::getline(input,line)) throw std::runtime_error("empty topology CSV");
    std::vector<Core> cores;
    std::string last;
    while (std::getline(input,line)) {
        const auto cells=splitCsv(line);
        if (cells.size()<7 || cells[2]==last) continue;
        last=cells[2];
        cores.push_back({cells[2],cells[5],static_cast<WORD>(std::stoul(cells[0])),static_cast<DWORD>(std::stoul(cells[1]))});
    }
    std::sort(cores.begin(),cores.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    return cores;
}

class AffinityGuard {
public:
    explicit AffinityGuard(const Core& core) {
        if (core.logical>=sizeof(KAFFINITY)*8) throw std::runtime_error("logical processor exceeds GROUP_AFFINITY mask");
        GROUP_AFFINITY target{};
        target.Group=core.group;
        target.Mask=(KAFFINITY(1)<<core.logical);
        if (!SetThreadGroupAffinity(GetCurrentThread(),&target,&old_)) throw std::runtime_error("SetThreadGroupAffinity failed for "+core.key);
        active_=true;
    }
    ~AffinityGuard() { if (active_) SetThreadGroupAffinity(GetCurrentThread(),&old_,nullptr); }
    AffinityGuard(const AffinityGuard&)=delete;
    AffinityGuard& operator=(const AffinityGuard&)=delete;
private:
    GROUP_AFFINITY old_{};
    bool active_=false;
};

std::int16_t activation_values[16]={127,-127,96,-96,64,-64,32,-32,16,-16,8,-8,4,-4,2,-2};
std::int16_t weight_values[16]={7,-7,7,-7,7,-7,7,-7,7,-7,7,-7,7,-7,7,-7};

// The microbenchmark arithmetic loop is in MASM so MSVC cannot hoist VPMADDWD or spill
// the independent accumulators around QueryPerformanceCounter.
__declspec(noinline) InstrResult measureInstrAvx2(std::uint32_t loops) {
    LARGE_INTEGER q0{},q1{};InstrResult result{};Phase1InstrAsmTiming stamp{};
    QueryPerformanceCounter(&q0);phase1_instr_avx2(loops,&stamp,result.lanes.data());QueryPerformanceCounter(&q1);
    result.timer.qpcTicks=q1.QuadPart-q0.QuadPart;result.timer.tscTicks=stamp.tscTicks;
    result.timer.auxStart=stamp.auxStart;result.timer.auxEnd=stamp.auxEnd;return result;
}

__declspec(noinline) InstrResult measureInstrVnni(std::uint32_t loops) {
    LARGE_INTEGER q0{},q1{};InstrResult result{};Phase1InstrAsmTiming stamp{};
    QueryPerformanceCounter(&q0);phase1_instr_vnni(loops,&stamp,result.lanes.data());QueryPerformanceCounter(&q1);
    result.timer.qpcTicks=q1.QuadPart-q0.QuadPart;result.timer.tscTicks=stamp.tscTicks;
    result.timer.auxStart=stamp.auxStart;result.timer.auxEnd=stamp.auxEnd;return result;
}

std::array<std::int32_t,8> scalarDotLanes() {
    std::array<std::int32_t,8> result{};
    for (int lane=0;lane<8;++lane)
        result[lane]=static_cast<std::int32_t>(activation_values[2*lane])*weight_values[2*lane]+
                     static_cast<std::int32_t>(activation_values[2*lane+1])*weight_values[2*lane+1];
    return result;
}

bool checkInstrResult(const InstrResult& result,std::uint64_t updatesPerAccumulator) {
    const auto expected=scalarDotLanes();
    for(int acc=0;acc<kIndependentAccumulators;++acc)for(int lane=0;lane<8;++lane){
        const auto want=static_cast<std::int32_t>(static_cast<std::int64_t>(expected[lane])*updatesPerAccumulator+acc);
        if(result.lanes[acc*8+lane]!=want)return false;
    }
    return true;
}

double median(std::vector<double> values) {
    std::sort(values.begin(),values.end());
    return values[values.size()/2];
}
double quantile(std::vector<double> values,double p) {
    std::sort(values.begin(),values.end());
    return values[static_cast<std::size_t>(std::floor(p*(values.size()-1)))];
}
struct Stats { double median=0,iqr=0,mad=0,min=0,max=0; };
Stats summarize(const std::vector<double>& values) {
    std::vector<double> deviations;
    const double med=median(values);
    for (double x:values) deviations.push_back(std::fabs(x-med));
    return {med,quantile(values,.75)-quantile(values,.25),median(deviations),
            *std::min_element(values.begin(),values.end()),*std::max_element(values.begin(),values.end())};
}

std::vector<Core> orderedCoreTypes(const std::string& topologyPath) {
    const auto all=loadCores(topologyPath);
    std::vector<Core> selected;
    for (const char* type:{"P-core","E-core"})
        for (const auto& core:all) if (core.type==type) selected.push_back(core);
    const auto pcount=std::count_if(selected.begin(),selected.end(),[](const Core& c){return c.type=="P-core";});
    const auto ecount=std::count_if(selected.begin(),selected.end(),[](const Core& c){return c.type=="E-core";});
    if(pcount!=8||ecount!=8)throw std::runtime_error("P_instr requires all eight physical P and eight physical E cores");
    return selected;
}

std::string routeName(int route) { return route==0?"AVX2":"AVX-VNNI-256"; }
void ensureOutputDirectory(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path,ec);
    if (ec) throw std::runtime_error("cannot create output directory: "+ec.message());
}
void refuseOverwrite(const std::vector<std::string>& paths) {
    for (const auto& path:paths) if (GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("refusing to overwrite existing output: "+path);
}

void writeInstr(const std::string& topology,const std::string& outputDirectory) {
    if (!t0q4::avx2_available() || !t0q4::avxvnni256_available()) throw std::runtime_error("both AVX2 and AVX-VNNI-256 must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_instr_raw.csv",summaryPath=outputDirectory+"\\p_instr_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if (!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0) throw std::runtime_error("QPC frequency unavailable");
    auto cores=orderedCoreTypes(topology);
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if (!raw||!summary) throw std::runtime_error("cannot create P_instr CSV output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,route,series_attempt,fixed_units,rep,sample_phase,warmup_count,N,calibration_units,calibration_elapsed_ns,outer_loop_iterations,unroll_factor,accumulator_updates_per_accumulator,independent_accumulators,conventional_MACs,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,conventional_MAC_per_second,correctness,status\n";
    summary<<"worker_key,worker_type,route,metric,median_conventional_MAC_per_second,iqr,mad,min,max,N,warmup_count,outlier_discard_count,fixed_units,series_attempt\n";
    for (std::size_t coreIndex=0;coreIndex<cores.size();++coreIndex) {
        const auto& core=cores[coreIndex];
        AffinityGuard affinity(core);
        for (int routeSlot=0;routeSlot<2;++routeSlot) {
            const int route=routeSlot ^ static_cast<int>(coreIndex%2);
            const auto calibrate=[&](std::uint64_t units){
                const auto loops=static_cast<std::uint32_t>(units);
                if(route==0)(void)measureInstrAvx2(loops);else (void)measureInstrVnni(loops);
            };
            const Calibration calibration=calibrateUnits(calibrate);
            recordCalibration(outputDirectory,core.key+"_P_instr_"+routeName(route),calibration);
            const std::string series=core.key+"_P_instr_"+routeName(route);
            const auto outcome=runOfficialSeries(outputDirectory,series,calibration.units,
                [&](std::uint64_t fixed){const auto n=static_cast<std::uint32_t>(fixed);for(int w=0;w<kWarmup;++w){if(route==0)(void)measureInstrAvx2(n);else (void)measureInstrVnni(n);}},
                [&](std::uint64_t fixed,int attempt){
                    AttemptResult out;const auto n=static_cast<std::uint32_t>(fixed);
                    const std::uint64_t updates=fixed*kInstrUnroll,macs=updates*kInstrMacsPerIteration;
                    for(int rep=0;rep<kN;++rep){
                        const InstrResult result=route==0?measureInstrAvx2(n):measureInstrVnni(n);
                        const bool correct=checkInstrResult(result,updates);
                        const double elapsedNs=static_cast<double>(result.timer.qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                        const bool auxOk=result.timer.auxStart==result.timer.auxEnd;
                        if(!correct||!auxOk)throw std::runtime_error("P_instr correctness/AUX/timer failure: "+series);
                        const bool shortSample=elapsedNs<static_cast<double>(kTimedRegionMinNs);
                        const double rate=shortSample?0.0:static_cast<double>(macs)/(static_cast<double>(result.timer.qpcTicks)/frequency.QuadPart);
                        raw<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<routeName(route)<<","<<attempt<<","<<fixed<<","<<rep<<",measured,"<<kWarmup<<","<<kN<<","<<calibration.units<<","<<calibration.elapsedNs<<","<<fixed<<","<<kInstrUnroll<<","<<updates<<","<<kIndependentAccumulators<<","<<macs<<","<<result.timer.qpcTicks<<","<<std::setprecision(12)<<elapsedNs<<","<<result.timer.tscTicks<<","<<result.timer.auxStart<<","<<result.timer.auxEnd<<","<<rate<<",true,"<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                        if(shortSample)out.tooShort=true;else out.rates.push_back(rate);
                    }
                    return out;
                });
            const Stats s=summarize(outcome.rates);
            summary<<core.key<<","<<core.type<<","<<routeName(route)<<",conventional_MAC_per_second,"<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<kN<<","<<kWarmup<<",0,"<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
        }
    }
}

void writeQ4Sweep(const std::string& topology,const std::string& outputDirectory) {
    if (!t0q4::avx2_available() || !t0q4::avxvnni256_available()) throw std::runtime_error("both AVX2 and AVX-VNNI-256 must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_q4_m_raw.csv",summaryPath=outputDirectory+"\\p_q4_m_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if (!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0) throw std::runtime_error("QPC frequency unavailable");
    auto allCores=loadCores(topology);
    std::sort(allCores.begin(),allCores.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    const auto pRefIt=std::find_if(allCores.begin(),allCores.end(),[](const Core& c){return c.type=="P-core";});
    if(pRefIt==allCores.end())throw std::runtime_error("P_ref unavailable in topology");
    const Core pRef=*pRefIt;
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if (!raw||!summary) throw std::runtime_error("cannot create P_Q4 CSV output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,route,m,m_tile,kernel_weight_payload_traversals_per_call,series_attempt,fixed_units,rep,warmup_count,N,calibrated_calls_per_sample,calibration_elapsed_ns,conventional_MACs,canonical_weight_payload_bytes_per_call,kernel_weight_payload_load_bytes_logical,weight_physical_allocated_bytes,activation_q_allocated_bytes,activation_scale_allocated_bytes,output_allocated_bytes,extra_kernel_persistent_bytes,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,P_Q4_conventional_MAC_per_second,BW_Q4_logical_B_per_s,status\n";
    summary<<"worker_key,worker_type,route,m,m_tile,kernel_weight_payload_traversals_per_call,metric,median_conventional_MAC_per_second,iqr,mad,min,max,median_BW_Q4_logical_B_per_s,N,warmup_count,outlier_discard_count,canonical_weight_payload_bytes_per_call,kernel_weight_payload_load_bytes_logical,weight_physical_allocated_bytes,activation_q_allocated_bytes,activation_scale_allocated_bytes,output_allocated_bytes,extra_kernel_persistent_bytes,resident_buffers_bytes,fixed_units,series_attempt\n";
    {
        const auto& core=pRef;
        AffinityGuard affinity(core);
        const auto weights=t0q4::generate_weights(kO,kI,0x5030513452455349ULL+core.logical);
        for (std::size_t mIndex=0;mIndex<std::size(kMValues);++mIndex) {
            const int m=kMValues[mIndex];
            for (int routeSlot=0;routeSlot<2;++routeSlot) {
                const int routeIndex=routeSlot^static_cast<int>(mIndex%2);
                const auto route=routeIndex==0?t0q4::Route::AVX2:t0q4::Route::AVXVNNI256;
                const int tile=static_cast<int>(t0q4::kernel_m_tile(static_cast<std::size_t>(m)));
                if(m%tile!=0)throw std::runtime_error("official M must divide exactly by m_tile");
                const std::uint64_t traversals=static_cast<std::uint64_t>(m/tile);
                const auto acts=t0q4::generate_activations(static_cast<std::size_t>(m),kI,0x5030513441435400ULL+(static_cast<std::uint64_t>(core.logical)<<8)+static_cast<unsigned>(m));
                t0q4::TimedOutput eval;
                t0q4::validate_inputs(weights,acts);
                t0q4::prepare_timed_output(eval,static_cast<std::size_t>(m),kO);
                const t0q4::KernelBatchEntry kernel=t0q4::select_prevalidated_batch_kernel(route,static_cast<std::size_t>(m));
                const std::uint64_t activationQBytes=acts.q.capacity()*sizeof(std::int8_t);
                const std::uint64_t activationScaleBytes=acts.scale.capacity()*sizeof(float);
                const std::uint64_t outputBytes=eval.values.capacity()*sizeof(float);
                const std::uint64_t residentBuffersBytes=weights.physical_allocated_bytes()+activationQBytes+activationScaleBytes+outputBytes;
                const std::string series=core.key+"_P_Q4_"+t0q4::route_name(route)+"_m"+std::to_string(m);
                const Calibration calibration=calibrateUnits([&](std::uint64_t calls){kernel(weights,acts,eval.values.data(),static_cast<int>(calls));});
                if(calibration.units>static_cast<std::uint64_t>(std::numeric_limits<int>::max()))throw std::runtime_error("P_Q4 calibration call count exceeds int");
                recordCalibration(outputDirectory,series,calibration);
                const auto outcome=runOfficialSeries(outputDirectory,series,calibration.units,
                    [&](std::uint64_t fixed){for(int w=0;w<kWarmup;++w)kernel(weights,acts,eval.values.data(),static_cast<int>(fixed));},
                    [&](std::uint64_t fixed,int attempt){
                    AttemptResult out;const int calibratedCalls=static_cast<int>(fixed);
                    for (int rep=0;rep<kN;++rep) {
                    LARGE_INTEGER q0{},q1{};QueryPerformanceCounter(&q0);unsigned aux0=0,aux1=0;
                    const auto t0=rdtscp(aux0);
                    kernel(weights,acts,eval.values.data(),calibratedCalls);
                    const auto t1=rdtscp(aux1);QueryPerformanceCounter(&q1);
                    const long long qpcTicks=q1.QuadPart-q0.QuadPart;
                    const double elapsedNs=static_cast<double>(qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                    const std::uint64_t macs=static_cast<std::uint64_t>(kO)*kI*m*calibratedCalls;
                    const std::uint64_t canonicalBytes=weights.payload_bytes();
                    const std::uint64_t logicalBytes=canonicalBytes*traversals*calibratedCalls;
                    if(aux0!=aux1)throw std::runtime_error("P_Q4 invalid AUX: "+series);
                    const bool shortSample=elapsedNs<static_cast<double>(kTimedRegionMinNs);
                    const double rate=shortSample?0.0:static_cast<double>(macs)/(static_cast<double>(qpcTicks)/frequency.QuadPart);
                    const double logicalRate=shortSample?0.0:static_cast<double>(logicalBytes)/(static_cast<double>(qpcTicks)/frequency.QuadPart);
                    raw<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<t0q4::route_name(route)<<","<<m<<","<<tile<<","<<traversals<<","<<attempt<<","<<fixed<<","<<rep<<","<<kWarmup<<","<<kN<<","<<calibratedCalls<<","<<calibration.elapsedNs<<","<<macs<<","<<canonicalBytes<<","<<logicalBytes<<","<<weights.physical_allocated_bytes()<<","<<activationQBytes<<","<<activationScaleBytes<<","<<outputBytes<<",0,"<<qpcTicks<<","<<std::setprecision(12)<<elapsedNs<<","<<(t1-t0)<<","<<aux0<<","<<aux1<<","<<rate<<","<<logicalRate<<","<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                    if(shortSample)out.tooShort=true;else{out.rates.push_back(rate);out.secondaryRates.push_back(logicalRate);}
                    }
                    return out;
                });
                const Stats s=summarize(outcome.rates);
                const Stats b=summarize(outcome.secondaryRates);
                summary<<core.key<<","<<core.type<<","<<t0q4::route_name(route)<<","<<m<<","<<tile<<","<<traversals<<",conventional_MAC_per_second,"<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<b.median<<","<<kN<<","<<kWarmup<<",0,"<<weights.payload_bytes()<<","<<weights.payload_bytes()*traversals*outcome.fixedUnits<<","<<weights.physical_allocated_bytes()<<","<<activationQBytes<<","<<activationScaleBytes<<","<<outputBytes<<",0,"<<residentBuffersBytes<<","<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
            }
        }
    }
}

template<class State,class Work>
class PersistentTeam {
public:
    PersistentTeam(std::vector<State>& states,Work work):states_(states),work_(std::move(work)),results_(states.size()){
        threads_.reserve(states_.size());
        for(std::size_t i=0;i<states_.size();++i)threads_.emplace_back([this,i]{worker(i);});
        std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return ready_==states_.size();});
        for(const auto& result:results_)if(!result.error.empty()){
            stop_=true;cv_.notify_all();lock.unlock();
            for(auto& thread:threads_)if(thread.joinable())thread.join();
            throw std::runtime_error("team startup failed before ready barrier: "+result.error);
        }
    }
    ~PersistentTeam(){
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;cv_.notify_all();}
        for(auto& thread:threads_)if(thread.joinable())thread.join();
    }
    PersistentTeam(const PersistentTeam&)=delete;
    PersistentTeam& operator=(const PersistentTeam&)=delete;

    TeamSample run(std::uint64_t units,bool timed){
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,[&]{return ready_==states_.size();});
        ready_=0;done_=0;units_=units;timed_=timed;
        for(auto& r:results_)r=TeamWorkerResult{};
        const long long start=timed?qpc():0;
        ++generation_;cv_.notify_all();
        cv_.wait(lock,[&]{return done_==states_.size();});
        const long long end=timed?qpc():0;
        TeamSample sample;sample.groupQpcTicks=timed?end-start:0;sample.workers=results_;
        for(const auto& worker:sample.workers)if(!worker.error.empty())throw std::runtime_error("team worker failed: "+worker.error);
        return sample;
    }
private:
    void worker(std::size_t i){
        bool readyPosted=false;
        try{
            AffinityGuard affinity(states_[i].core);
            std::uint64_t seen=0;
            std::unique_lock<std::mutex> lock(mutex_);
            for(;;){
                ++ready_;readyPosted=true;cv_.notify_all();
                cv_.wait(lock,[&]{return stop_||generation_>seen;});
                if(stop_)return;
                seen=generation_;const auto units=units_;const bool timed=timed_;
                lock.unlock();
                TeamWorkerResult result{};
                if(timed){LARGE_INTEGER q0{},q1{};QueryPerformanceCounter(&q0);const auto t0=rdtscp(result.auxStart);
                    result.sink=work_(states_[i],units);const auto t1=rdtscp(result.auxEnd);QueryPerformanceCounter(&q1);
                    result.qpcTicks=q1.QuadPart-q0.QuadPart;result.tscTicks=t1-t0;
                }else result.sink=work_(states_[i],units);
                lock.lock();results_[i]=std::move(result);++done_;readyPosted=false;cv_.notify_all();
            }
        }catch(const std::exception& ex){
            std::lock_guard<std::mutex> lock(mutex_);results_[i].error=ex.what();
            if(!readyPosted)++ready_;
            if(done_<states_.size())++done_;cv_.notify_all();
        }
    }
    std::vector<State>& states_;Work work_;std::vector<TeamWorkerResult> results_;std::vector<std::thread> threads_;
    std::mutex mutex_;std::condition_variable cv_;std::size_t ready_=0,done_=0;std::uint64_t generation_=0,units_=0;bool timed_=false,stop_=false;
};

struct Q4TeamWorker { Core core; t0q4::WeightMatrix weights; t0q4::Activations activations; t0q4::TimedOutput output; t0q4::KernelBatchEntry kernel=nullptr; };
std::vector<Core> getPcoreList(const std::string& topology){
    std::vector<Core> p;for(const auto& core:loadCores(topology))if(core.type=="P-core")p.push_back(core);
    std::sort(p.begin(),p.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    if(p.size()!=8)throw std::runtime_error("BW_Q4_logical_8P requires exactly 8 P-cores from topology");
    return p;
}

std::string pCoreM1OrderPath(const std::string& outputDirectory){return outputDirectory+"\\p_core_m1_order.json";}
void validateLiveTopology(const std::string& topology);
void preparePCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    ensureOutputDirectory(outputDirectory);validateLiveTopology(topology);
    auto cores=getPcoreList(topology);std::mt19937_64 rng(kPcoreM1OrderSeed);std::shuffle(cores.begin(),cores.end(),rng);
    const auto path=pCoreM1OrderPath(outputDirectory);refuseOverwrite({path});
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("cannot preregister P-core Q4 m1 order");
    out<<"{\n  \"random_seed_hex\": \"4D44323850314D31\",\n  \"generator\": \"std::mt19937_64 + std::shuffle\",\n  \"route_schedule\": \"alternating by worker_order index; AVX2 first for even indices\",\n  \"worker_order\": [";
    for(std::size_t i=0;i<cores.size();++i)out<<(i?", ":"")<<"\""<<cores[i].key<<"\"";
    out<<"],\n  \"series\": \"P_Q4_i(m=1), AVX2 and AVX-VNNI-256 per physical P-core\",\n  \"N\": 31,\n  \"warmup_count\": 5\n}\n";
    if(!out)throw std::runtime_error("failed writing preregistered P-core Q4 m1 order");
}

std::vector<Core> readPreregisteredPCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    std::ifstream input(pCoreM1OrderPath(outputDirectory),std::ios::binary);if(!input)throw std::runtime_error("preregistered P-core Q4 m1 order file is missing");
    const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    if(text.find("\"random_seed_hex\": \"4D44323850314D31\"")==std::string::npos||text.find("\"generator\": \"std::mt19937_64 + std::shuffle\"")==std::string::npos)
        throw std::runtime_error("P-core m1 order seed/generator differs from pre-registration");
    const auto begin=text.find("\"worker_order\"");const auto arrayStart=begin==std::string::npos?std::string::npos:text.find('[',begin);
    const auto end=arrayStart==std::string::npos?std::string::npos:text.find(']',arrayStart);
    if(end==std::string::npos)throw std::runtime_error("malformed P-core m1 worker_order");
    std::vector<std::string> keys;std::size_t pos=arrayStart+1;
    while((pos=text.find('"',pos))!=std::string::npos&&pos<end){const auto close=text.find('"',pos+1);if(close==std::string::npos||close>=end)throw std::runtime_error("unterminated P-core key in worker_order");keys.push_back(text.substr(pos+1,close-pos-1));pos=close+1;}
    const auto cores=getPcoreList(topology);std::vector<Core> ordered;
    for(const auto& key:keys){const auto it=std::find_if(cores.begin(),cores.end(),[&](const Core& c){return c.key==key;});if(it==cores.end())throw std::runtime_error("unknown P-core key in preregistered order: "+key);ordered.push_back(*it);}
    if(ordered.size()!=cores.size())throw std::runtime_error("P-core m1 order must include all eight physical P-cores");
    std::sort(keys.begin(),keys.end());for(std::size_t i=1;i<keys.size();++i)if(keys[i]==keys[i-1])throw std::runtime_error("duplicate P-core key in preregistered order");
    return ordered;
}
void checkPreregisteredPCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    const auto cores=readPreregisteredPCoreM1Order(topology,outputDirectory);
    std::cout<<"{\"record\":\"preregistered_pcore_m1_order_check\",\"seed_hex\":\"4D44323850314D31\",\"core_count\":"<<cores.size()<<",\"worker_order\":[";
    for(std::size_t i=0;i<cores.size();++i)std::cout<<(i?",":"")<<"\""<<cores[i].key<<"\"";
    std::cout<<"],\"route_pairs\":[";
    for(std::size_t i=0;i<cores.size();++i){const int first=static_cast<int>(i%2);std::cout<<(i?",":"")<<"[\""<<routeName(first)<<"\",\""<<routeName(first^1)<<"\"]";}
    std::cout<<"],\"measurement\":false}\n";
}

std::vector<Core> getEcoreList(const std::string& topology){
    std::vector<Core> e;for(const auto& core:loadCores(topology))if(core.type=="E-core")e.push_back(core);
    std::sort(e.begin(),e.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    if(e.size()!=8)throw std::runtime_error("E-core campaign requires exactly eight physical E-cores");
    return e;
}

void validateLiveTopology(const std::string& topology){
    DWORD required=0;
    GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&required);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||required==0)throw std::runtime_error("cannot query live processor-core topology size");
    std::vector<unsigned char> buffer(required);
    if(!GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),&required))
        throw std::runtime_error("cannot query live processor-core topology");
    using CoreSet=std::set<std::string>;
    std::map<CoreSet,BYTE> live;
    DWORD offset=0;
    while(offset<required){
        const auto* info=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+offset);
        if(info->Size==0||offset+info->Size>required)throw std::runtime_error("malformed live processor topology record");
        if(info->Relationship==RelationProcessorCore){
            CoreSet ids;const auto& pr=info->Processor;
            for(WORD g=0;g<pr.GroupCount;++g){
                const auto& gm=pr.GroupMask[g];
                for(DWORD lp=0;lp<sizeof(KAFFINITY)*8;++lp)if((gm.Mask&(KAFFINITY(1)<<lp))!=0)ids.insert(std::to_string(gm.Group)+":"+std::to_string(lp));
            }
            if(ids.empty()||!live.emplace(ids,pr.EfficiencyClass).second)throw std::runtime_error("duplicate or empty live physical core topology record");
        }
        offset+=info->Size;
    }
    std::ifstream input(topology);if(!input)throw std::runtime_error("cannot open frozen topology CSV");
    std::string line;if(!std::getline(input,line))throw std::runtime_error("frozen topology CSV is empty");
    std::map<CoreSet,BYTE> frozen;std::size_t pcount=0,ecount=0;
    while(std::getline(input,line)){
        const auto cells=splitCsv(line);if(cells.size()<8)throw std::runtime_error("malformed frozen topology CSV row");
        CoreSet ids;std::stringstream list(cells[7]);std::string item;
        while(std::getline(list,item,';'))if(!item.empty())ids.insert(item);
        if(ids.empty())throw std::runtime_error("missing SMT sibling list in frozen topology CSV");
        const BYTE eff=static_cast<BYTE>(std::stoul(cells[3]));
        if(!frozen.emplace(ids,eff).second)continue;
        if(cells[5]=="P-core")++pcount;else if(cells[5]=="E-core")++ecount;
    }
    if(live!=frozen||live.size()!=16||pcount!=8||ecount!=8||GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)!=24)
        throw std::runtime_error("live processor topology differs from the frozen 8P+8E/24-LP topology");
}

std::string eOrderPath(const std::string& outputDirectory){return outputDirectory+"\\e_confirm_order.json";}

void prepareECoreOrder(const std::string& topology,const std::string& outputDirectory){
    ensureOutputDirectory(outputDirectory);
    validateLiveTopology(topology);
    const auto cores=getEcoreList(topology);
    std::mt19937_64 rng(kECoreOrderSeed);auto shuffled=cores;std::shuffle(shuffled.begin(),shuffled.end(),rng);
    const auto path=eOrderPath(outputDirectory);refuseOverwrite({path});
    std::ofstream order(path,std::ios::binary);if(!order)throw std::runtime_error("cannot preregister E-core order");
    order<<"{\n  \"random_seed_hex\": \"4D44323545434F52\",\n  \"generator\": \"std::mt19937_64 + std::shuffle\",\n  \"route_schedule\": \"alternating by worker_order index; AVX2 first for even indices\",\n  \"worker_order\": [";
    for(std::size_t i=0;i<shuffled.size();++i)order<<(i?", ":"")<<"\""<<shuffled[i].key<<"\"";
    order<<"],\n  \"N\": 31,\n  \"warmup_count\": 5\n}\n";
    if(!order)throw std::runtime_error("failed writing preregistered E-core order");
}

std::vector<Core> readPreregisteredECoreOrder(const std::string& topology,const std::string& outputDirectory){
    std::ifstream input(eOrderPath(outputDirectory),std::ios::binary);if(!input)throw std::runtime_error("preregistered E-core order file is missing");
    const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto begin=text.find("\"worker_order\"");if(begin==std::string::npos)throw std::runtime_error("E-core order JSON lacks worker_order");
    if(text.find("\"random_seed_hex\": \"4D44323545434F52\"")==std::string::npos||
       text.find("\"generator\": \"std::mt19937_64 + std::shuffle\"")==std::string::npos)
        throw std::runtime_error("preregistered E-core order seed/generator differs from frozen plan");
    const auto arrayStart=text.find('[',begin);if(arrayStart==std::string::npos)throw std::runtime_error("malformed E-core worker_order array");
    const auto end=text.find(']',arrayStart);if(end==std::string::npos)throw std::runtime_error("malformed E-core order JSON");
    std::vector<std::string> keys;std::size_t pos=arrayStart+1;
    while((pos=text.find('"',pos))!=std::string::npos&&pos<end){
        const auto close=text.find('"',pos+1);if(close==std::string::npos||close>=end)throw std::runtime_error("unterminated E-core key in worker_order");
        keys.push_back(text.substr(pos+1,close-pos-1));pos=close+1;
    }
    auto all=getEcoreList(topology);std::vector<Core> result;
    for(const auto& key:keys){const auto it=std::find_if(all.begin(),all.end(),[&](const Core& c){return c.key==key;});if(it==all.end())throw std::runtime_error("unknown core key in preregistered E-core order: "+key);result.push_back(*it);}
    if(result.size()!=all.size())throw std::runtime_error("preregistered E-core order must list each of eight physical cores exactly once");
    std::sort(keys.begin(),keys.end());for(std::size_t i=1;i<keys.size();++i)if(keys[i]==keys[i-1])throw std::runtime_error("duplicate E-core key in preregistered order");
    return result;
}

#if 0 // duplicate legacy copy; the canonical P-core order helpers are defined above.
void preparePCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    ensureOutputDirectory(outputDirectory);validateLiveTopology(topology);
    auto cores=getPcoreList(topology);std::mt19937_64 rng(kPcoreM1OrderSeed);std::shuffle(cores.begin(),cores.end(),rng);
    const auto path=pCoreM1OrderPath(outputDirectory);refuseOverwrite({path});
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("cannot preregister P-core m1 order");
    out<<"{\n  \"random_seed_hex\": \"4D44323850314D31\",\n  \"generator\": \"std::mt19937_64 + std::shuffle\",\n  \"route_schedule\": \"alternating by worker_order index; AVX2 first for even indices\",\n  \"worker_order\": [";
    for(std::size_t i=0;i<cores.size();++i)out<<(i?", ":"")<<"\""<<cores[i].key<<"\"";
    out<<"],\n  \"series\": \"P_Q4_i(m=1), AVX2 and AVX-VNNI-256 per physical P-core\",\n  \"N\": 31,\n  \"warmup_count\": 5\n}\n";
    if(!out)throw std::runtime_error("failed writing preregistered P-core m1 order");
}

std::vector<Core> readPreregisteredPCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    std::ifstream input(pCoreM1OrderPath(outputDirectory),std::ios::binary);if(!input)throw std::runtime_error("preregistered P-core m1 order file is missing");
    const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    if(text.find("\"random_seed_hex\": \"4D44323850314D31\"")==std::string::npos||text.find("\"generator\": \"std::mt19937_64 + std::shuffle\"")==std::string::npos)
        throw std::runtime_error("preregistered P-core m1 seed/generator differs from the frozen plan");
    const auto start=text.find("\"worker_order\"");const auto arrayStart=start==std::string::npos?std::string::npos:text.find('[',start);
    const auto end=arrayStart==std::string::npos?std::string::npos:text.find(']',arrayStart);
    if(end==std::string::npos)throw std::runtime_error("malformed preregistered P-core worker_order");
    std::vector<std::string> keys;std::size_t pos=arrayStart+1;
    while((pos=text.find('"',pos))!=std::string::npos&&pos<end){const auto close=text.find('"',pos+1);if(close==std::string::npos||close>=end)throw std::runtime_error("unterminated P-core key");keys.push_back(text.substr(pos+1,close-pos-1));pos=close+1;}
    const auto cores=getPcoreList(topology);std::vector<Core> ordered;
    for(const auto& key:keys){const auto it=std::find_if(cores.begin(),cores.end(),[&](const Core& c){return c.key==key;});if(it==cores.end())throw std::runtime_error("unknown P-core key in preregistered order: "+key);ordered.push_back(*it);}
    if(ordered.size()!=cores.size())throw std::runtime_error("P-core m1 preregistered order must contain exactly eight cores");
    std::sort(keys.begin(),keys.end());for(std::size_t i=1;i<keys.size();++i)if(keys[i]==keys[i-1])throw std::runtime_error("duplicate P-core key in preregistered order");
    return ordered;
}

void checkPreregisteredPCoreM1Order(const std::string& topology,const std::string& outputDirectory){
    const auto cores=readPreregisteredPCoreM1Order(topology,outputDirectory);
    std::cout<<"{\"record\":\"preregistered_pcore_m1_order_check\",\"seed_hex\":\"4D44323850314D31\",\"core_count\":"<<cores.size()<<",\"worker_order\":[";
    for(std::size_t i=0;i<cores.size();++i)std::cout<<(i?",":"")<<"\""<<cores[i].key<<"\"";
    std::cout<<"],\"route_pairs\":[";
    for(std::size_t i=0;i<cores.size();++i){const int first=static_cast<int>(i%2);std::cout<<(i?",":"")<<"[\""<<routeName(first)<<"\",\""<<routeName(first^1)<<"\"]";}
    std::cout<<"],\"measurement\":false}\n";
}
#endif

void checkPreregisteredECoreOrder(const std::string& topology,const std::string& outputDirectory){
    const auto cores=readPreregisteredECoreOrder(topology,outputDirectory);
    std::cout<<"{\"record\":\"preregistered_e_core_order_check\",\"seed_hex\":\"4D44323545434F52\",\"core_count\":"<<cores.size()<<",\"worker_order\":[";
    for(std::size_t i=0;i<cores.size();++i)std::cout<<(i?",":"")<<"\""<<cores[i].key<<"\"";
    std::cout<<"],\"route_pairs\":[";
    for(std::size_t i=0;i<cores.size();++i){const int first=static_cast<int>(i%2);std::cout<<(i?",":"")<<"[\""<<routeName(first)<<"\",\""<<routeName(first^1)<<"\"]";}
    std::cout<<"],\"measurement\":false}\n";
}

void writeQ4EightP(const std::string& topology,const std::string& outputDirectory){
    if(!t0q4::avx2_available()||!t0q4::avxvnni256_available())throw std::runtime_error("both Q4 ISA routes must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_q4_8p_m1_raw.csv",summaryPath=outputDirectory+"\\p_q4_8p_m1_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    const auto cores=getPcoreList(topology);
    std::vector<Q4TeamWorker> workers;workers.reserve(cores.size());
    for(std::size_t i=0;i<cores.size();++i){
        Q4TeamWorker state{};state.core=cores[i];state.weights=t0q4::generate_weights(kO,kI,0x513438505F4D3100ULL+i);
        state.activations=t0q4::generate_activations(1,kI,0x513438505F4D3100ULL^i);
        t0q4::validate_inputs(state.weights,state.activations);t0q4::prepare_timed_output(state.output,1,kO);workers.push_back(std::move(state));
    }
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if(!raw||!summary)throw std::runtime_error("cannot create 8P Q4 output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,route,series_attempt,fixed_units,rep,warmup_count,N,calibrated_calls_per_sample,calibration_elapsed_ns,conventional_MACs_per_worker,logical_weight_bytes_per_worker,worker_qpc_ticks,worker_elapsed_ns,worker_MAC_per_second,group_qpc_ticks,group_elapsed_ns,group_MAC_per_second,BW_Q4_logical_bytes_per_second,weight_physical_allocated_bytes,rdtscp_ticks,aux_start,aux_end,status\n";
    summary<<"worker_set,route,metric,median,iqr,mad,min,max,N,warmup_count,outlier_discard_count,fixed_units,series_attempt\n";
    auto work=[](Q4TeamWorker& w,std::uint64_t calls)->std::uint64_t{w.kernel(w.weights,w.activations,w.output.values.data(),static_cast<int>(calls));return 0;};
    PersistentTeam<Q4TeamWorker,decltype(work)> team(workers,work);
    for(int routeIndex=0;routeIndex<2;++routeIndex){
        const auto route=routeIndex==0?t0q4::Route::AVX2:t0q4::Route::AVXVNNI256;
        const auto entry=t0q4::select_prevalidated_batch_kernel(route,1);for(auto& worker:workers)worker.kernel=entry;
        const std::string series=std::string("P_Q4_8P_m1_")+t0q4::route_name(route);
        const Calibration calibration=calibrateByMeasuredNs([&](std::uint64_t calls){
            const auto sample=team.run(calls,true);
            return static_cast<std::uint64_t>(sample.groupQpcTicks)*1000000000ULL/static_cast<std::uint64_t>(frequency.QuadPart);
        });
        if(calibration.units>static_cast<std::uint64_t>(std::numeric_limits<int>::max()))throw std::runtime_error("8P Q4 calibration call count exceeds int");
        recordCalibration(outputDirectory,series,calibration);
        const auto outcome=runOfficialSeries(outputDirectory,series,calibration.units,
            [&](std::uint64_t fixed){for(int w=0;w<kWarmup;++w)(void)team.run(fixed,false);},
            [&](std::uint64_t fixed,int attempt){
                AttemptResult out;const int calibratedCalls=static_cast<int>(fixed);
                const std::uint64_t macsPerWorker=static_cast<std::uint64_t>(kO)*kI*calibratedCalls;
                const std::uint64_t bytesPerWorker=workers[0].weights.payload_bytes()*calibratedCalls;
                for(int rep=0;rep<kN;++rep){
                    TeamSample sample=team.run(fixed,true);
                    const double groupNs=static_cast<double>(sample.groupQpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                    const bool shortSample=groupNs<static_cast<double>(kTimedRegionMinNs);
                    const double groupMacsPerSecond=shortSample?0.0:static_cast<double>(macsPerWorker*workers.size())/(groupNs*1e-9);
                    const double groupBytesPerSecond=shortSample?0.0:static_cast<double>(bytesPerWorker*workers.size())/(groupNs*1e-9);
                    if(!shortSample){out.rates.push_back(groupMacsPerSecond);out.secondaryRates.push_back(groupBytesPerSecond);}else out.tooShort=true;
                    for(std::size_t i=0;i<workers.size();++i){
                        const auto& wr=sample.workers[i];if(wr.auxStart!=wr.auxEnd)throw std::runtime_error("8P Q4 AUX migration");
                        const double workerNs=static_cast<double>(wr.qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                        const double workerRate=shortSample||workerNs<=0?0.0:static_cast<double>(macsPerWorker)/(workerNs*1e-9);
                        raw<<workers[i].core.key<<","<<workers[i].core.type<<","<<workers[i].core.group<<","<<workers[i].core.logical<<","<<t0q4::route_name(route)<<","<<attempt<<","<<fixed<<","<<rep<<","<<kWarmup<<","<<kN<<","<<calibratedCalls<<","<<calibration.elapsedNs<<","<<macsPerWorker<<","<<bytesPerWorker<<","<<wr.qpcTicks<<","<<std::setprecision(12)<<workerNs<<","<<workerRate<<","<<sample.groupQpcTicks<<","<<groupNs<<","<<groupMacsPerSecond<<","<<groupBytesPerSecond<<","<<workers[i].weights.physical_allocated_bytes()<<","<<wr.tscTicks<<","<<wr.auxStart<<","<<wr.auxEnd<<","<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                    }
                }
                return out;
            });
        for(const auto& metric:{std::make_pair("P_Q4_8P_conventional_MAC_per_second",outcome.rates),std::make_pair("BW_Q4_logical_8P_B_per_s",outcome.secondaryRates)}){
            const Stats s=summarize(metric.second);summary<<"8P,"<<t0q4::route_name(route)<<","<<metric.first<<","<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<kN<<","<<kWarmup<<",0,"<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
        }
    }
}

struct ReadWorker { Core core; std::vector<std::uint64_t> words; };
__declspec(noinline) std::uint64_t sequentialReadOnly(const std::uint64_t* data,std::size_t words,std::uint32_t passes){
    __m256i a0=_mm256_setzero_si256(),a1=_mm256_setzero_si256(),a2=_mm256_setzero_si256(),a3=_mm256_setzero_si256();
    __m256i a4=_mm256_setzero_si256(),a5=_mm256_setzero_si256(),a6=_mm256_setzero_si256(),a7=_mm256_setzero_si256();
    for(std::uint32_t pass=0;pass<passes;++pass)for(std::size_t i=0;i<words;i+=32){
        a0=_mm256_add_epi64(a0,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+0)));
        a1=_mm256_add_epi64(a1,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+4)));
        a2=_mm256_add_epi64(a2,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+8)));
        a3=_mm256_add_epi64(a3,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+12)));
        a4=_mm256_add_epi64(a4,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+16)));
        a5=_mm256_add_epi64(a5,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+20)));
        a6=_mm256_add_epi64(a6,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+24)));
        a7=_mm256_add_epi64(a7,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+i+28)));
    }
    alignas(32) std::uint64_t lanes[32];
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes+0),a0);_mm256_store_si256(reinterpret_cast<__m256i*>(lanes+4),a1);
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes+8),a2);_mm256_store_si256(reinterpret_cast<__m256i*>(lanes+12),a3);
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes+16),a4);_mm256_store_si256(reinterpret_cast<__m256i*>(lanes+20),a5);
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes+24),a6);_mm256_store_si256(reinterpret_cast<__m256i*>(lanes+28),a7);
    std::uint64_t sink=0;for(auto value:lanes)sink^=value;return sink;
}

void writeDramRead8P(const std::string& topology,const std::string& outputDirectory){
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\bw_dram_read_8p_raw.csv",summaryPath=outputDirectory+"\\bw_dram_read_8p_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    const auto cores=getPcoreList(topology);std::vector<ReadWorker> workers;workers.reserve(cores.size());
    const std::size_t words=static_cast<std::size_t>(kReadBytesPerWorker/sizeof(std::uint64_t));
    for(std::size_t c=0;c<cores.size();++c){ReadWorker w{};w.core=cores[c];w.words.resize(words);for(std::size_t i=0;i<words;++i)w.words[i]=(static_cast<std::uint64_t>(i)*0x9E3779B97F4A7C15ULL)^((c+1)*0xD1B54A32D192ED03ULL);workers.push_back(std::move(w));}
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);if(!raw||!summary)throw std::runtime_error("cannot create DRAM read output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,series_attempt,fixed_units,rep,warmup_count,N,calibrated_passes,calibration_elapsed_ns,working_set_bytes_per_worker,worker_qpc_ticks,worker_elapsed_ns,worker_logical_bytes_per_second,group_qpc_ticks,group_elapsed_ns,group_logical_bytes_read,BW_DRAM_read_8P_B_per_s,rdtscp_ticks,aux_start,aux_end,sink,status\n";
    summary<<"worker_set,metric,median,iqr,mad,min,max,N,warmup_count,outlier_discard_count,working_set_bytes_per_worker,fixed_units,series_attempt\n";
    auto work=[](const ReadWorker& w,std::uint64_t passes)->std::uint64_t{return sequentialReadOnly(w.words.data(),w.words.size(),static_cast<std::uint32_t>(passes));};
    PersistentTeam<ReadWorker,decltype(work)> team(workers,work);
    const Calibration calibration=calibrateByMeasuredNs([&](std::uint64_t passes){
        const auto sample=team.run(passes,true);
        return static_cast<std::uint64_t>(sample.groupQpcTicks)*1000000000ULL/static_cast<std::uint64_t>(frequency.QuadPart);
    });
    if(calibration.units>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("DRAM calibration pass count exceeds uint32");
    recordCalibration(outputDirectory,"BW_DRAM_read_8P",calibration);
    const std::string series="BW_DRAM_read_8P";
    const auto outcome=runOfficialSeries(outputDirectory,series,calibration.units,
        [&](std::uint64_t fixed){for(int w=0;w<kWarmup;++w)(void)team.run(fixed,false);},
        [&](std::uint64_t fixed,int attempt){
            AttemptResult out;const auto samplePasses=static_cast<std::uint32_t>(fixed);
            const std::uint64_t groupBytes=kReadBytesPerWorker*workers.size()*samplePasses;
            for(int rep=0;rep<kN;++rep){
                TeamSample sample=team.run(fixed,true);
                const double groupNs=static_cast<double>(sample.groupQpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                const bool shortSample=groupNs<static_cast<double>(kTimedRegionMinNs);
                const double groupRate=shortSample?0.0:static_cast<double>(groupBytes)/(groupNs*1e-9);
                if(shortSample)out.tooShort=true;else out.rates.push_back(groupRate);
                for(std::size_t i=0;i<workers.size();++i){
                    const auto& wr=sample.workers[i];if(wr.auxStart!=wr.auxEnd)throw std::runtime_error("DRAM read AUX migration");
                    const double workerNs=static_cast<double>(wr.qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                    const double workerRate=shortSample||workerNs<=0?0.0:static_cast<double>(kReadBytesPerWorker*samplePasses)/(workerNs*1e-9);
                    raw<<workers[i].core.key<<","<<workers[i].core.type<<","<<workers[i].core.group<<","<<workers[i].core.logical<<","<<attempt<<","<<fixed<<","<<rep<<","<<kWarmup<<","<<kN<<","<<samplePasses<<","<<calibration.elapsedNs<<","<<kReadBytesPerWorker<<","<<wr.qpcTicks<<","<<std::setprecision(12)<<workerNs<<","<<workerRate<<","<<sample.groupQpcTicks<<","<<groupNs<<","<<groupBytes<<","<<groupRate<<","<<wr.tscTicks<<","<<wr.auxStart<<","<<wr.auxEnd<<","<<wr.sink<<","<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                }
            }
            return out;
        });
    const Stats s=summarize(outcome.rates);summary<<"8P,BW_DRAM_read_8P_B_per_s,"<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<kN<<","<<kWarmup<<",0,"<<kReadBytesPerWorker<<","<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
}

void writeECoreConfirmation(const std::string& topology,const std::string& outputDirectory){
    if(!t0q4::avx2_available()||!t0q4::avxvnni256_available()||!t0q4::f16c_available())throw std::runtime_error("AVX2, AVX-VNNI-256 and F16C are required for E-core confirmation");
    if(!t0q4::avx2_available()||!t0q4::avxvnni256_available())throw std::runtime_error("both AVX2 and AVX-VNNI-256 must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string q4Raw=outputDirectory+"\\e_confirm_q4_m1_raw.csv",q4Summary=outputDirectory+"\\e_confirm_q4_m1_summary.csv";
    refuseOverwrite({q4Raw,q4Summary});
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    const auto cores=readPreregisteredECoreOrder(topology,outputDirectory);
    std::ofstream qr(q4Raw,std::ios::binary),qs(q4Summary,std::ios::binary);
    if(!qr||!qs)throw std::runtime_error("cannot create E-core Q4 confirm files");
    qr<<"order_index,worker_key,worker_type,processor_group,logical_processor,route,series_attempt,fixed_units,m,m_tile,kernel_weight_payload_traversals_per_call,calibration_calls,calibration_elapsed_ns,rep,warmup_count,N,conventional_MACs,canonical_weight_payload_bytes_per_call,kernel_weight_payload_load_bytes_logical,weight_physical_allocated_bytes,activation_q_allocated_bytes,activation_scale_allocated_bytes,output_allocated_bytes,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,P_Q4_conventional_MAC_per_second,BW_Q4_logical_B_per_s,status\n";
    qs<<"order_index,worker_key,worker_type,route,m,metric,median,iqr,mad,min,max,median_BW_Q4_logical_B_per_s,N,warmup_count,outlier_discard_count,fixed_units,series_attempt\n";
    for(std::size_t orderIndex=0;orderIndex<cores.size();++orderIndex){
        const auto& core=cores[orderIndex];AffinityGuard affinity(core);
        const auto weights=t0q4::generate_weights(kO,kI,0x45434F5245513455ULL+core.logical);
        const auto acts=t0q4::generate_activations(1,kI,0x45434F5245513455ULL^core.logical);
        t0q4::validate_inputs(weights,acts);t0q4::TimedOutput output;t0q4::prepare_timed_output(output,1,kO);
        const auto outputBytes=output.values.capacity()*sizeof(float);
        const auto activationQBytes=acts.q.capacity()*sizeof(std::int8_t);
        const auto activationScaleBytes=acts.scale.capacity()*sizeof(float);
        for(int routeSlot=0;routeSlot<2;++routeSlot){
            const int routeIndex=routeSlot^static_cast<int>(orderIndex%2);
            const std::string route=routeName(routeIndex);
            const auto entry=t0q4::select_prevalidated_batch_kernel(routeIndex==0?t0q4::Route::AVX2:t0q4::Route::AVXVNNI256,1);
            const auto q4Calibration=calibrateUnits([&](std::uint64_t calls){entry(weights,acts,output.values.data(),static_cast<int>(calls));});
            if(q4Calibration.units>static_cast<std::uint64_t>(std::numeric_limits<int>::max()))throw std::runtime_error("E-core Q4 calibration calls exceed int");
            recordCalibration(outputDirectory,core.key+"_E_P_Q4_"+route+"_m1",q4Calibration);
            const std::string series=core.key+"_E_P_Q4_"+route+"_m1";
            const auto outcome=runOfficialSeries(outputDirectory,series,q4Calibration.units,
                [&](std::uint64_t fixed){for(int warm=0;warm<kWarmup;++warm)entry(weights,acts,output.values.data(),static_cast<int>(fixed));},
                [&](std::uint64_t fixed,int attempt){
                AttemptResult out;const auto calls=static_cast<std::uint64_t>(fixed);const std::uint64_t macs=static_cast<std::uint64_t>(kO)*kI*calls;
                const std::uint64_t bytes=weights.payload_bytes()*calls;
                for(int rep=0;rep<kN;++rep){
                LARGE_INTEGER q0{},q1{};QueryPerformanceCounter(&q0);unsigned aux0=0,aux1=0;const auto t0=rdtscp(aux0);
                entry(weights,acts,output.values.data(),static_cast<int>(calls));
                const auto t1=rdtscp(aux1);QueryPerformanceCounter(&q1);const long long ticks=q1.QuadPart-q0.QuadPart;
                const double ns=static_cast<double>(ticks)*1e9/static_cast<double>(frequency.QuadPart);
                if(aux0!=aux1)throw std::runtime_error("E-core P_Q4 AUX failure: "+series);
                const bool shortSample=ns<static_cast<double>(kTimedRegionMinNs);
                const double rate=shortSample?0.0:static_cast<double>(macs)/(static_cast<double>(ticks)/frequency.QuadPart);
                const double bw=shortSample?0.0:static_cast<double>(bytes)/(static_cast<double>(ticks)/frequency.QuadPart);
                qr<<orderIndex<<","<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<route<<","<<attempt<<","<<fixed<<",1,1,1,"<<calls<<","<<q4Calibration.elapsedNs<<","<<rep<<","<<kWarmup<<","<<kN<<","<<macs<<","<<weights.payload_bytes()<<","<<bytes<<","<<weights.physical_allocated_bytes()<<","<<activationQBytes<<","<<activationScaleBytes<<","<<outputBytes<<","<<ticks<<","<<std::setprecision(12)<<ns<<","<<(t1-t0)<<","<<aux0<<","<<aux1<<","<<rate<<","<<bw<<","<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                if(shortSample)out.tooShort=true;else{out.rates.push_back(rate);out.secondaryRates.push_back(bw);}
                }
                return out;
            });
            const Stats sq=summarize(outcome.rates),sb=summarize(outcome.secondaryRates);
            qs<<orderIndex<<","<<core.key<<","<<core.type<<","<<route<<",1,conventional_MAC_per_second,"<<sq.median<<","<<sq.iqr<<","<<sq.mad<<","<<sq.min<<","<<sq.max<<","<<sb.median<<","<<kN<<","<<kWarmup<<",0,"<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
        }
    }
}

void writePCoreQ4M1(const std::string& topology,const std::string& outputDirectory){
    if(!t0q4::avx2_available()||!t0q4::avxvnni256_available()||!t0q4::f16c_available())
        throw std::runtime_error("AVX2, AVX-VNNI-256 and F16C are required for P-core Q4 m1");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_q4_pcore_m1_raw.csv";
    const std::string summaryPath=outputDirectory+"\\p_q4_pcore_m1_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    const auto cores=readPreregisteredPCoreM1Order(topology,outputDirectory);
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if(!raw||!summary)throw std::runtime_error("cannot create P-core Q4 m1 outputs");
    raw<<"order_index,worker_key,worker_type,processor_group,logical_processor,route,series_attempt,fixed_units,m,m_tile,kernel_weight_payload_traversals_per_call,calibration_units,calibration_elapsed_ns,rep,warmup_count,N,conventional_MACs,canonical_weight_payload_bytes_per_call,kernel_weight_payload_load_bytes_logical,weight_physical_allocated_bytes,activation_q_allocated_bytes,activation_scale_allocated_bytes,output_allocated_bytes,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,P_Q4_conventional_MAC_per_second,BW_Q4_logical_B_per_s,status\n";
    summary<<"order_index,worker_key,worker_type,route,m,m_tile,kernel_weight_payload_traversals_per_call,metric,median_conventional_MAC_per_second,iqr,mad,min,max,median_BW_Q4_logical_B_per_s,N,warmup_count,outlier_discard_count,canonical_weight_payload_bytes_per_call,kernel_weight_payload_load_bytes_logical,weight_physical_allocated_bytes,fixed_units,series_attempt\n";
    for(std::size_t orderIndex=0;orderIndex<cores.size();++orderIndex){
        const auto& core=cores[orderIndex];AffinityGuard affinity(core);
        const auto weights=t0q4::generate_weights(kO,kI,0x5031434F52455F00ULL+core.logical);
        const auto acts=t0q4::generate_activations(1,kI,0x5031434F52455F00ULL^core.logical);
        t0q4::validate_inputs(weights,acts);t0q4::TimedOutput output;t0q4::prepare_timed_output(output,1,kO);
        const std::uint64_t outputBytes=output.values.capacity()*sizeof(float);
        const std::uint64_t activationQBytes=acts.q.capacity()*sizeof(std::int8_t);
        const std::uint64_t activationScaleBytes=acts.scale.capacity()*sizeof(float);
        for(int routeSlot=0;routeSlot<2;++routeSlot){
            const int routeIndex=routeSlot^static_cast<int>(orderIndex%2);
            const auto route=routeIndex==0?t0q4::Route::AVX2:t0q4::Route::AVXVNNI256;
            const auto entry=t0q4::select_prevalidated_batch_kernel(route,1);
            const std::string routeLabel=routeName(routeIndex);
            const std::string series=core.key+"_P_Q4_i_m1_"+routeLabel;
            const Calibration calibration=calibrateUnits([&](std::uint64_t calls){entry(weights,acts,output.values.data(),static_cast<int>(calls));});
            recordCalibration(outputDirectory,series,calibration);
            const auto outcome=runOfficialSeries(outputDirectory,series,calibration.units,
                [&](std::uint64_t fixed){for(int warm=0;warm<kWarmup;++warm)entry(weights,acts,output.values.data(),static_cast<int>(fixed));},
                [&](std::uint64_t fixed,int attempt){
                    AttemptResult out;const auto calls=fixed;
                    const auto macs=static_cast<std::uint64_t>(kO)*kI*calls;
                    const auto canonicalBytes=weights.payload_bytes();const auto logicalBytes=canonicalBytes*calls;
                    for(int rep=0;rep<kN;++rep){
                        LARGE_INTEGER q0{},q1{};QueryPerformanceCounter(&q0);unsigned aux0=0,aux1=0;
                        const auto t0=rdtscp(aux0);entry(weights,acts,output.values.data(),static_cast<int>(calls));
                        const auto t1=rdtscp(aux1);QueryPerformanceCounter(&q1);
                        const long long ticks=q1.QuadPart-q0.QuadPart;
                        const double elapsedNs=static_cast<double>(ticks)*1e9/static_cast<double>(frequency.QuadPart);
                        if(aux0!=aux1)throw std::runtime_error("P-core Q4 m1 AUX migration: "+series);
                        const bool shortSample=elapsedNs<static_cast<double>(kTimedRegionMinNs);
                        const double rate=shortSample?0.0:static_cast<double>(macs)/(static_cast<double>(ticks)/frequency.QuadPart);
                        const double bw=shortSample?0.0:static_cast<double>(logicalBytes)/(static_cast<double>(ticks)/frequency.QuadPart);
                        raw<<orderIndex<<","<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<routeLabel<<","<<attempt<<","<<fixed<<",1,1,1,"<<calibration.units<<","<<calibration.elapsedNs<<","<<rep<<","<<kWarmup<<","<<kN<<","<<macs<<","<<canonicalBytes<<","<<logicalBytes<<","<<weights.physical_allocated_bytes()<<","<<activationQBytes<<","<<activationScaleBytes<<","<<outputBytes<<","<<ticks<<","<<std::setprecision(12)<<elapsedNs<<","<<(t1-t0)<<","<<aux0<<","<<aux1<<","<<rate<<","<<bw<<","<<(shortSample?"INVALID_TIMER_TOO_SHORT":"OK")<<"\n";
                        if(shortSample)out.tooShort=true;else{out.rates.push_back(rate);out.secondaryRates.push_back(bw);}
                    }
                    return out;
                });
            const Stats p=summarize(outcome.rates),b=summarize(outcome.secondaryRates);
            summary<<orderIndex<<","<<core.key<<","<<core.type<<","<<routeLabel<<",1,1,1,conventional_MAC_per_second,"<<p.median<<","<<p.iqr<<","<<p.mad<<","<<p.min<<","<<p.max<<","<<b.median<<","<<kN<<","<<kWarmup<<",0,"<<weights.payload_bytes()<<","<<weights.payload_bytes()*outcome.fixedUnits<<","<<weights.physical_allocated_bytes()<<","<<outcome.fixedUnits<<","<<outcome.attempt<<"\n";
        }
    }
}

void writeFrequencyPinstrLoad(const std::string& topology,const std::string& coreKey,std::uint64_t fixedUnits,const std::string& markerPath){
    if(!t0q4::avx2_available())throw std::runtime_error("AVX2 unavailable for preregistered P_instr frequency load");
    if(fixedUnits==0||fixedUnits>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("invalid frozen P_instr fixed_units for frequency load");
    const auto cores=loadCores(topology);const auto it=std::find_if(cores.begin(),cores.end(),[&](const Core& c){return c.key==coreKey;});
    if(it==cores.end()||(it->type!="P-core"&&it->type!="E-core"))throw std::runtime_error("frequency target core key is not in frozen P/E topology");
    if(GetFileAttributesA(markerPath.c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("frequency marker already exists; refusing overwrite");
    AffinityGuard affinity(*it);
    LARGE_INTEGER qpcFrequency{};if(!QueryPerformanceFrequency(&qpcFrequency)||qpcFrequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable for frequency marker");
    const InstrResult result=measureInstrAvx2(static_cast<std::uint32_t>(fixedUnits));
    const std::uint64_t updates=fixedUnits*kInstrUnroll;
    const bool correct=checkInstrResult(result,updates);
    const bool auxOk=result.timer.auxStart==result.timer.auxEnd;
    const double elapsedNs=static_cast<double>(result.timer.qpcTicks)*1e9/static_cast<double>(qpcFrequency.QuadPart);
    std::ofstream marker(markerPath,std::ios::binary);if(!marker)throw std::runtime_error("cannot write frequency-load marker");
    marker<<"{\n  \"record\": \"P_instr_frequency_load_marker\",\n  \"worker_key\": \""<<coreKey<<"\",\n  \"worker_type\": \""<<it->type
          <<"\",\n  \"processor_group\": "<<it->group<<",\n  \"logical_processor\": "<<it->logical
          <<",\n  \"route\": \"AVX2\",\n  \"fixed_units\": "<<fixedUnits<<",\n  \"qpc_frequency_hz\": "<<qpcFrequency.QuadPart
          <<",\n  \"qpc_elapsed_ns\": "<<std::setprecision(12)<<elapsedNs<<",\n  \"qpc_ticks\": "<<result.timer.qpcTicks
          <<",\n  \"rdtscp_ticks_diagnostic_only\": "<<result.timer.tscTicks<<",\n  \"aux_start\": "<<result.timer.auxStart
          <<",\n  \"aux_end\": "<<result.timer.auxEnd<<",\n  \"correctness\": "<<(correct?"true":"false")
          <<",\n  \"aux_stable\": "<<(auxOk?"true":"false")<<",\n  \"throughput_used_for_frequency\": false,\n  \"frequency_derived_in_harness\": false\n}\n";
    if(!marker||!correct||!auxOk)throw std::runtime_error("P_instr frequency-load marker correctness/AUX failed");
    std::cout<<"{\"record\":\"frequency_load_marker\",\"worker_key\":\""<<coreKey<<"\",\"fixed_units\":"<<fixedUnits
             <<",\"elapsed_ns\":"<<std::setprecision(12)<<elapsedNs<<",\"correctness\":true,\"frequency_derived\":false}\n";
}

} // namespace

int main(int argc,char** argv) {
    try {
        if (argc==4 && std::string(argv[1])=="--run-instr") { writeInstr(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-q4-m") { writeQ4Sweep(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-q4-8p-m1") { writeQ4EightP(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-dram-read-8p") { writeDramRead8P(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--prepare-e-order") { prepareECoreOrder(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--check-e-order") { checkPreregisteredECoreOrder(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--prepare-pcore-m1-order") { preparePCoreM1Order(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--check-pcore-m1-order") { checkPreregisteredPCoreM1Order(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-e-confirm") { writeECoreConfirmation(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-pcore-m1") { writePCoreQ4M1(argv[2],argv[3]); return 0; }
        if (argc==6 && std::string(argv[1])=="--run-frequency-pinstr") { writeFrequencyPinstrLoad(argv[2],argv[3],std::stoull(argv[4]),argv[5]); return 0; }
        std::cerr<<"usage: phase1_measure_harness.exe --prepare-e-order/--check-e-order/--prepare-pcore-m1-order/--check-pcore-m1-order <topology.csv> <output_dir> | --run-instr <topology.csv> <output_dir> | --run-q4-m <topology.csv> <output_dir> | --run-q4-8p-m1 <topology.csv> <output_dir> | --run-dram-read-8p <topology.csv> <output_dir> | --run-e-confirm <topology.csv> <output_dir> | --run-pcore-m1 <topology.csv> <output_dir> | --run-frequency-pinstr <topology.csv> <core_key> <fixed_units> <marker.csv>\n";
        return 2;
    } catch (const std::exception& ex) {
        std::cerr<<"phase1_measure_harness_error: "<<ex.what()<<"\n";
        return 1;
    }
}
