#define NOMINMAX
#include "q4_kernel.hpp"
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kN=31;
constexpr int kWarmup=5;
constexpr int kMValues[]={1,2,4,8,16,32};
constexpr std::uint32_t kInstrOuterIterations=250000;
constexpr std::uint32_t kInstrUnroll=4;
constexpr std::uint32_t kInstrAccumulatorUpdates=kInstrOuterIterations*kInstrUnroll;
constexpr std::uint64_t kInstrMacsPerIteration=8ULL*16ULL;
constexpr int kIndependentAccumulators=8;
constexpr int kQ4CallsPerSample=32;
constexpr std::size_t kO=512, kI=512;
constexpr float kAtol=1.0e-5f, kRtol=1.0e-5f;

struct Core { std::string key,type; WORD group; DWORD logical; };
struct TimerResult { long long qpcTicks=0; unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; };
struct InstrResult { TimerResult timer; std::array<std::int32_t,64> lanes{}; };
struct Sample { long long qpcTicks=0; unsigned long long tscTicks=0; unsigned auxStart=0,auxEnd=0; };

long long qpc() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value)) throw std::runtime_error("QueryPerformanceCounter failed");
    return value.QuadPart;
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

#define PHASE0_AVX2_BATCH \
    x0=_mm256_add_epi32(x0,_mm256_madd_epi16(a,b)); \
    x1=_mm256_add_epi32(x1,_mm256_madd_epi16(a,b)); \
    x2=_mm256_add_epi32(x2,_mm256_madd_epi16(a,b)); \
    x3=_mm256_add_epi32(x3,_mm256_madd_epi16(a,b)); \
    x4=_mm256_add_epi32(x4,_mm256_madd_epi16(a,b)); \
    x5=_mm256_add_epi32(x5,_mm256_madd_epi16(a,b)); \
    x6=_mm256_add_epi32(x6,_mm256_madd_epi16(a,b)); \
    x7=_mm256_add_epi32(x7,_mm256_madd_epi16(a,b));
#define PHASE0_VNNI_BATCH \
    x0=_mm256_dpwssd_avx_epi32(x0,a,b); \
    x1=_mm256_dpwssd_avx_epi32(x1,a,b); \
    x2=_mm256_dpwssd_avx_epi32(x2,a,b); \
    x3=_mm256_dpwssd_avx_epi32(x3,a,b); \
    x4=_mm256_dpwssd_avx_epi32(x4,a,b); \
    x5=_mm256_dpwssd_avx_epi32(x5,a,b); \
    x6=_mm256_dpwssd_avx_epi32(x6,a,b); \
    x7=_mm256_dpwssd_avx_epi32(x7,a,b);

// Operands are loaded into YMM registers before QPC/RDTSCP starts. Eight independent accumulators
// hide instruction latency; only the arithmetic used by the corresponding real Q4 path is timed.
__declspec(noinline) InstrResult measureInstrAvx2(std::uint32_t loops) {
    const __m256i a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(activation_values));
    const __m256i b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(weight_values));
    __m256i x0=_mm256_setzero_si256(),x1=_mm256_setzero_si256(),x2=_mm256_setzero_si256(),x3=_mm256_setzero_si256();
    __m256i x4=_mm256_setzero_si256(),x5=_mm256_setzero_si256(),x6=_mm256_setzero_si256(),x7=_mm256_setzero_si256();
    LARGE_INTEGER q0{},q1{};
    InstrResult result{};
    QueryPerformanceCounter(&q0);
    const auto t0=rdtscp(result.timer.auxStart);
    for (std::uint32_t i=0;i<loops;++i) {
        PHASE0_AVX2_BATCH
        PHASE0_AVX2_BATCH
        PHASE0_AVX2_BATCH
        PHASE0_AVX2_BATCH
    }
    const auto t1=rdtscp(result.timer.auxEnd);
    QueryPerformanceCounter(&q1);
    result.timer.qpcTicks=q1.QuadPart-q0.QuadPart;
    result.timer.tscTicks=t1-t0;
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+0),x0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+8),x1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+16),x2);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+24),x3);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+32),x4);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+40),x5);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+48),x6);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+56),x7);
    return result;
}

__declspec(noinline) InstrResult measureInstrVnni(std::uint32_t loops) {
    const __m256i a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(activation_values));
    const __m256i b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(weight_values));
    __m256i x0=_mm256_setzero_si256(),x1=_mm256_setzero_si256(),x2=_mm256_setzero_si256(),x3=_mm256_setzero_si256();
    __m256i x4=_mm256_setzero_si256(),x5=_mm256_setzero_si256(),x6=_mm256_setzero_si256(),x7=_mm256_setzero_si256();
    LARGE_INTEGER q0{},q1{};
    InstrResult result{};
    QueryPerformanceCounter(&q0);
    const auto t0=rdtscp(result.timer.auxStart);
    for (std::uint32_t i=0;i<loops;++i) {
        PHASE0_VNNI_BATCH
        PHASE0_VNNI_BATCH
        PHASE0_VNNI_BATCH
        PHASE0_VNNI_BATCH
    }
    const auto t1=rdtscp(result.timer.auxEnd);
    QueryPerformanceCounter(&q1);
    result.timer.qpcTicks=q1.QuadPart-q0.QuadPart;
    result.timer.tscTicks=t1-t0;
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+0),x0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+8),x1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+16),x2);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+24),x3);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+32),x4);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+40),x5);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+48),x6);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.lanes.data()+56),x7);
    return result;
}

std::array<std::int32_t,8> scalarDotLanes() {
    std::array<std::int32_t,8> result{};
    for (int lane=0;lane<8;++lane)
        result[lane]=static_cast<std::int32_t>(activation_values[2*lane])*weight_values[2*lane]+
                     static_cast<std::int32_t>(activation_values[2*lane+1])*weight_values[2*lane+1];
    return result;
}

bool checkInstrResult(const InstrResult& result,std::uint32_t loops) {
    const auto expected=scalarDotLanes();
    for (int acc=0;acc<kIndependentAccumulators;++acc)
        for (int lane=0;lane<8;++lane) {
            const auto want=static_cast<std::int32_t>(static_cast<std::int64_t>(expected[lane])*loops);
            if (result.lanes[acc*8+lane]!=want) return false;
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

int runCorrectness() {
    if (!t0q4::avx2_available()) throw std::runtime_error("AVX2 unavailable for Phase 0 correctness");
    bool passed=true;
    const auto expected=scalarDotLanes();
    __m256i av=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(activation_values));
    __m256i wv=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(weight_values));
    alignas(32) std::int32_t avx2Lanes[8]{},vnniLanes[8]{};
    _mm256_store_si256(reinterpret_cast<__m256i*>(avx2Lanes),_mm256_madd_epi16(av,wv));
    if (!t0q4::avxvnni256_available()) throw std::runtime_error("AVX-VNNI-256 required by Phase 0 is unavailable");
    _mm256_store_si256(reinterpret_cast<__m256i*>(vnniLanes),_mm256_dpwssd_avx_epi32(_mm256_setzero_si256(),av,wv));
    for (int lane=0;lane<8;++lane) passed=passed && (avx2Lanes[lane]==expected[lane]) && (vnniLanes[lane]==expected[lane]);
    if (!passed) { std::cerr<<"instruction primitive correctness FAIL\n"; return 1; }

    constexpr float atol=kAtol,rtol=kRtol;
    const auto weights=t0q4::generate_weights(kO,kI,0x5048415345305134ULL);
    for (int m:kMValues) {
        const auto acts=t0q4::generate_activations(static_cast<std::size_t>(m),kI,0x5048415345300000ULL+static_cast<unsigned>(m));
        const auto ref=t0q4::execute(weights,acts,t0q4::Route::Scalar);
        for (const auto route:{t0q4::Route::AVX2,t0q4::Route::AVXVNNI256}) {
            const auto got=t0q4::execute(weights,acts,route);
            bool exact=got.group_dot==ref.group_dot;
            bool within=got.output.size()==ref.output.size();
            if (within) for (std::size_t i=0;i<ref.output.size();++i) {
                const double diff=std::fabs(static_cast<double>(got.output[i])-ref.output[i]);
                const double bound=atol+rtol*std::fabs(static_cast<double>(ref.output[i]));
                if (!(diff<=bound)) { within=false; break; }
            }
            passed=passed&&exact&&within;
            std::cout<<"correctness route="<<t0q4::route_name(route)<<" m="<<m
                     <<" integer_group_exact="<<(exact?"true":"false")
                     <<" final_tolerance_pass="<<(within?"true":"false")<<"\n";
        }
    }
    std::cout<<"summary status="<<(passed?"PASS":"FAIL")<<" measurement=false atol=1e-5 rtol=1e-5\n";
    return passed?0:1;
}

void writeInstr(const std::string& topology,const std::string& outputDirectory) {
    if (!t0q4::avx2_available() || !t0q4::avxvnni256_available()) throw std::runtime_error("both AVX2 and AVX-VNNI-256 must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_instr_raw.csv",summaryPath=outputDirectory+"\\p_instr_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if (!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0) throw std::runtime_error("QPC frequency unavailable");
    const auto cores=orderedCoreTypes(topology);
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if (!raw||!summary) throw std::runtime_error("cannot create P_instr CSV output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,route,rep,sample_phase,warmup_count,N,outer_loop_iterations,unroll_factor,accumulator_updates_per_accumulator,independent_accumulators,conventional_MACs,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,conventional_MAC_per_second,correctness,status\n";
    summary<<"worker_key,worker_type,route,metric,median_conventional_MAC_per_second,iqr,mad,min,max,N,warmup_count,outlier_discard_count\n";
    for (const auto& core:cores) {
        AffinityGuard affinity(core);
        for (int route=0;route<2;++route) {
            std::vector<double> rates;
            for (int rep=-kWarmup;rep<kN;++rep) {
                const InstrResult result=route==0?measureInstrAvx2(kInstrOuterIterations):measureInstrVnni(kInstrOuterIterations);
                const bool correct=checkInstrResult(result,kInstrAccumulatorUpdates);
                const double elapsedNs=static_cast<double>(result.timer.qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                const std::uint64_t macs=static_cast<std::uint64_t>(kInstrAccumulatorUpdates)*kInstrMacsPerIteration;
                const double rate=result.timer.qpcTicks>0?static_cast<double>(macs)/(static_cast<double>(result.timer.qpcTicks)/frequency.QuadPart):0.0;
                const bool good=correct && result.timer.auxStart==result.timer.auxEnd && result.timer.qpcTicks>0;
                const std::string status=!correct?"CORRECTNESS_FAIL":((result.timer.auxStart!=result.timer.auxEnd)?"INVALID_AUX_MIGRATION":(good?"OK":"INVALID_TIMER"));
                if (rep>=0 && good) rates.push_back(rate);
                raw<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<routeName(route)<<","<<rep<<","<<(rep<0?"warmup":"measured")<<","<<kWarmup<<","<<kN<<","<<kInstrOuterIterations<<","<<kInstrUnroll<<","<<kInstrAccumulatorUpdates<<","<<kIndependentAccumulators<<","<<macs<<","<<result.timer.qpcTicks<<","<<std::setprecision(12)<<elapsedNs<<","<<result.timer.tscTicks<<","<<result.timer.auxStart<<","<<result.timer.auxEnd<<","<<rate<<","<<(correct?"true":"false")<<","<<status<<"\n";
                if (!good) throw std::runtime_error("P_instr correctness/timer validation failed; no retry: "+core.key+" "+routeName(route));
            }
            const Stats s=summarize(rates);
            summary<<core.key<<","<<core.type<<","<<routeName(route)<<",conventional_MAC_per_second,"<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<kN<<","<<kWarmup<<",0\n";
        }
    }
}

void writeQ4Sweep(const std::string& topology,const std::string& outputDirectory) {
    if (!t0q4::avx2_available() || !t0q4::avxvnni256_available()) throw std::runtime_error("both AVX2 and AVX-VNNI-256 must be available");
    ensureOutputDirectory(outputDirectory);
    const std::string rawPath=outputDirectory+"\\p_q4_m_raw.csv",summaryPath=outputDirectory+"\\p_q4_m_summary.csv";
    refuseOverwrite({rawPath,summaryPath});
    LARGE_INTEGER frequency{};if (!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0) throw std::runtime_error("QPC frequency unavailable");
    const auto cores=orderedCoreTypes(topology);
    std::ofstream raw(rawPath,std::ios::binary),summary(summaryPath,std::ios::binary);
    if (!raw||!summary) throw std::runtime_error("cannot create P_Q4 CSV output");
    raw<<"worker_key,worker_type,processor_group,logical_processor,route,m,rep,warmup_count,N,calls_per_sample,conventional_MACs,logical_weight_bytes_per_sample,weight_payload_bytes,weight_physical_allocated_bytes,qpc_ticks,elapsed_ns,rdtscp_ticks,aux_start,aux_end,P_Q4_conventional_MAC_per_second,status\n";
    summary<<"worker_key,worker_type,route,m,metric,median_conventional_MAC_per_second,iqr,mad,min,max,N,warmup_count,outlier_discard_count,weight_payload_bytes,weight_physical_allocated_bytes\n";
    for (const auto& core:cores) {
        AffinityGuard affinity(core);
        const auto weights=t0q4::generate_weights(kO,kI,0x5030513452455349ULL+core.logical);
        for (int routeIndex=0;routeIndex<2;++routeIndex) {
            const auto route=routeIndex==0?t0q4::Route::AVX2:t0q4::Route::AVXVNNI256;
            for (int m:kMValues) {
                const auto acts=t0q4::generate_activations(static_cast<std::size_t>(m),kI,0x5030513441435400ULL+(static_cast<std::uint64_t>(core.logical)<<8)+static_cast<unsigned>(m));
                t0q4::Evaluation eval;
                for (int warm=0;warm<kWarmup;++warm)
                    for (int call=0;call<kQ4CallsPerSample;++call) t0q4::execute_into_prechecked(weights,acts,route,eval);
                std::vector<double> rates;
                for (int rep=0;rep<kN;++rep) {
                    LARGE_INTEGER q0{},q1{};QueryPerformanceCounter(&q0);unsigned aux0=0,aux1=0;
                    const auto t0=rdtscp(aux0);
                    for (int call=0;call<kQ4CallsPerSample;++call) t0q4::execute_into_prechecked(weights,acts,route,eval);
                    const auto t1=rdtscp(aux1);QueryPerformanceCounter(&q1);
                    const long long qpcTicks=q1.QuadPart-q0.QuadPart;
                    const double elapsedNs=static_cast<double>(qpcTicks)*1e9/static_cast<double>(frequency.QuadPart);
                    const std::uint64_t macs=static_cast<std::uint64_t>(kO)*kI*m*kQ4CallsPerSample;
                    const std::uint64_t logicalBytes=weights.payload_bytes()*kQ4CallsPerSample;
                    const double rate=qpcTicks>0?static_cast<double>(macs)/(static_cast<double>(qpcTicks)/frequency.QuadPart):0.0;
                    const bool good=qpcTicks>0 && aux0==aux1;
                    const std::string status=good?"OK":(aux0!=aux1?"INVALID_AUX_MIGRATION":"INVALID_TIMER");
                    if (good) rates.push_back(rate);
                    raw<<core.key<<","<<core.type<<","<<core.group<<","<<core.logical<<","<<t0q4::route_name(route)<<","<<m<<","<<rep<<","<<kWarmup<<","<<kN<<","<<kQ4CallsPerSample<<","<<macs<<","<<logicalBytes<<","<<weights.payload_bytes()<<","<<weights.physical_allocated_bytes()<<","<<qpcTicks<<","<<std::setprecision(12)<<elapsedNs<<","<<(t1-t0)<<","<<aux0<<","<<aux1<<","<<rate<<","<<status<<"\n";
                    if (!good) throw std::runtime_error("P_Q4 timer/AUX validation failed; no retry: "+core.key+" "+std::string(t0q4::route_name(route))+" m="+std::to_string(m));
                }
                const Stats s=summarize(rates);
                summary<<core.key<<","<<core.type<<","<<t0q4::route_name(route)<<","<<m<<",conventional_MAC_per_second,"<<s.median<<","<<s.iqr<<","<<s.mad<<","<<s.min<<","<<s.max<<","<<kN<<","<<kWarmup<<",0,"<<weights.payload_bytes()<<","<<weights.physical_allocated_bytes()<<"\n";
            }
        }
    }
}

} // namespace

int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="--correctness") return runCorrectness();
        if (argc==4 && std::string(argv[1])=="--run-instr") { writeInstr(argv[2],argv[3]); return 0; }
        if (argc==4 && std::string(argv[1])=="--run-q4-m") { writeQ4Sweep(argv[2],argv[3]); return 0; }
        std::cerr<<"usage: phase0_bench.exe --correctness | --run-instr <topology.csv> <output_dir> | --run-q4-m <topology.csv> <output_dir>\n";
        return 2;
    } catch (const std::exception& ex) {
        std::cerr<<"phase0_bench_error: "<<ex.what()<<"\n";
        return 1;
    }
}
