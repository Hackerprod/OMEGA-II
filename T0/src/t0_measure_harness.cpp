#define NOMINMAX
#include "q4_kernel.hpp"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Frozen prospectively for each family; this harness is compiled but NOT run in stage 1.
static constexpr int WARMUP_COUNT=5;
static constexpr int N_MEASURED=31;
static constexpr int KERNEL_CALLS_PER_SAMPLE=128;
static constexpr std::uint64_t LLC_BYTES_OBSERVED=31457280ULL;
static constexpr std::uint64_t BW_D_BUFFER_BYTES_PER_WORKER=4ULL*LLC_BYTES_OBSERVED;
static const int BW_D_N_SWEEP[]={1,2,4,8,16};
static const char* FIXED_SERIES_ORDER="d_api_core_ascending; e_route_AVX2_then_VNNI_per_api_core; f_BW_D_n_ascending; f_BW_L2_i_api_core_ascending; f_BW_L2_Sigma_A_then_B_then_C; i_dynamic_P_then_E_then_MIXED";

struct Core { std::string key,type; WORD group; DWORD logical; };
struct WorkerState {
    Core core; t0q4::WeightMatrix weights; t0q4::Activations activations; t0q4::Evaluation eval;
    std::vector<std::uint8_t> source,destination;
};
struct Sample { long long qpc_start=0,qpc_end=0;unsigned long long tsc_start=0,tsc_end=0;double worker_ns=0,group_ns=0;std::uint64_t macs=0,logical_bytes=0; };
enum class WorkKind { Q4, DRAM_COPY };

static std::vector<std::string> splitCsv(const std::string&line){
    std::vector<std::string> out;std::string cell;bool quoted=false;
    for(std::size_t i=0;i<line.size();++i){char c=line[i];if(c=='"'){quoted=!quoted;continue;}if(c==','&&!quoted){out.push_back(cell);cell.clear();}else cell.push_back(c);}out.push_back(cell);return out;
}
static std::vector<Core> loadCores(const std::string&path){
    std::ifstream f(path);if(!f)throw std::runtime_error("cannot open topology CSV: "+path);
    std::string line;if(!std::getline(f,line))throw std::runtime_error("empty topology CSV");
    std::vector<Core> all;std::string last;
    while(std::getline(f,line)){auto c=splitCsv(line);if(c.size()<7)continue;if(c[2]==last)continue;last=c[2];all.push_back({c[2],c[5],static_cast<WORD>(std::stoul(c[0])),static_cast<DWORD>(std::stoul(c[1]))});}
    std::sort(all.begin(),all.end(),[](const Core&a,const Core&b){return a.key<b.key;});return all;
}
static void pinCurrent(const Core&c){GROUP_AFFINITY target{},old{};target.Group=c.group;target.Mask=(KAFFINITY(1)<<c.logical);if(!SetThreadGroupAffinity(GetCurrentThread(),&target,&old))throw std::runtime_error("SetThreadGroupAffinity failed");}
static long long qpc(){LARGE_INTEGER x{};QueryPerformanceCounter(&x);return x.QuadPart;}
static unsigned long long tsc(){unsigned aux=0;_mm_lfence();auto v=__rdtscp(&aux);_mm_lfence();return v;}
static WorkerState makeWorker(const Core&c,WorkKind kind,std::uint64_t seed){
    WorkerState w{};w.core=c;
    if(kind==WorkKind::Q4){w.weights=t0q4::generate_weights(512,512,seed);w.activations=t0q4::generate_activations(1,512,seed^0xA0761D6478BD642FULL);t0q4::execute_into(w.weights,w.activations,t0q4::Route::AVX2,w.eval);}
    else{w.source.resize(static_cast<std::size_t>(BW_D_BUFFER_BYTES_PER_WORKER));w.destination.resize(static_cast<std::size_t>(BW_D_BUFFER_BYTES_PER_WORKER));for(std::size_t i=0;i<w.source.size();++i)w.source[i]=static_cast<std::uint8_t>((i*131u+seed)&255u);std::fill(w.destination.begin(),w.destination.end(),0);}
    return w;
}
static void doWork(WorkerState&w,WorkKind kind,t0q4::Route route,int calls,std::uint64_t&macs,std::uint64_t&bytes){
    thread_local volatile std::uint8_t sink=0;
    if(kind==WorkKind::DRAM_COPY){for(int i=0;i<calls;++i){w.source[static_cast<std::size_t>(i)%w.source.size()]^=static_cast<std::uint8_t>(i+1);std::memcpy(w.destination.data(),w.source.data(),w.source.size());sink^=w.destination[static_cast<std::size_t>(i)%w.destination.size()];}bytes=2ULL*w.source.size()*calls;macs=0;}
    else{for(int i=0;i<calls;++i)t0q4::execute_into_prechecked(w.weights,w.activations,route,w.eval);macs=2ULL*w.weights.O*w.weights.I*calls;bytes=w.weights.payload_bytes()*calls;}
}
struct TeamRun { std::vector<Sample> worker;double group_wall_ns=0; };
struct Gate { std::mutex m;std::condition_variable cv;int ready=0,done=0;bool go=false; };
static TeamRun runOneSample(std::vector<WorkerState>&states,WorkKind kind,t0q4::Route route,int warmups,int calls){
    Gate gate;TeamRun result;result.worker.resize(states.size());std::vector<std::thread> threads;threads.reserve(states.size());
    for(std::size_t i=0;i<states.size();++i)threads.emplace_back([&,i](){
        pinCurrent(states[i].core);std::uint64_t m=0,b=0;
        for(int w=0;w<warmups;++w)doWork(states[i],kind,route,calls,m,b);
        {std::unique_lock<std::mutex> lock(gate.m);++gate.ready;gate.cv.notify_all();gate.cv.wait(lock,[&]{return gate.go;});}
        Sample s{};s.qpc_start=qpc();s.tsc_start=tsc();doWork(states[i],kind,route,calls,s.macs,s.logical_bytes);s.tsc_end=tsc();s.qpc_end=qpc();
        LARGE_INTEGER fq{};QueryPerformanceFrequency(&fq);s.worker_ns=1e9*(double)(s.qpc_end-s.qpc_start)/(double)fq.QuadPart;result.worker[i]=s;
        {std::lock_guard<std::mutex> lock(gate.m);++gate.done;gate.cv.notify_all();}
    });
    {std::unique_lock<std::mutex> lock(gate.m);gate.cv.wait(lock,[&]{return gate.ready==(int)states.size();});}
    LARGE_INTEGER fq{};QueryPerformanceFrequency(&fq);long long start=qpc();
    {std::lock_guard<std::mutex> lock(gate.m);gate.go=true;gate.cv.notify_all();}
    {std::unique_lock<std::mutex> lock(gate.m);gate.cv.wait(lock,[&]{return gate.done==(int)states.size();});}
    long long end=qpc();result.group_wall_ns=1e9*(double)(end-start)/(double)fq.QuadPart;
    for(auto&t:threads)t.join();for(auto&s:result.worker)s.group_ns=result.group_wall_ns;return result;
}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static double quantile(std::vector<double> v,double p){std::sort(v.begin(),v.end());std::size_t i=static_cast<std::size_t>(std::floor(p*(v.size()-1)));return v[i];}
static double writeRawSeries(const std::string&name,const std::vector<Core>&cores,WorkKind kind,t0q4::Route route,int calls,const std::string&outdir,bool byteMetric=false){
    std::vector<WorkerState> states;for(std::size_t i=0;i<cores.size();++i)states.push_back(makeWorker(cores[i],kind,0x5430302D4D454153ULL+i));
    std::string rawPath=outdir+"\\"+name+"_raw.csv";if(GetFileAttributesA(rawPath.c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("raw CSV already exists; refusing overwrite");std::ofstream csv(rawPath,std::ios::binary);if(!csv)throw std::runtime_error("cannot write raw measurement CSV");
    csv<<"series,rep,warmup_count,N,worker_key,worker_type,processor_group,logical_processor,route,qpc_start_ticks,qpc_end_ticks,worker_elapsed_ns,group_elapsed_ns,rdtscp_start,rdtscp_end,macs,logical_bytes,discarded\n";
    std::vector<double> throughput;throughput.reserve(N_MEASURED);
    for(int rep=0;rep<N_MEASURED;++rep){TeamRun sample=runOneSample(states,kind,route,WARMUP_COUNT,calls);std::uint64_t macs=0,bytes=0;for(std::size_t i=0;i<sample.worker.size();++i){auto&s=sample.worker[i];macs+=s.macs;bytes+=s.logical_bytes;csv<<name<<","<<rep<<","<<WARMUP_COUNT<<","<<N_MEASURED<<","<<states[i].core.key<<","<<states[i].core.type<<","<<states[i].core.group<<","<<states[i].core.logical<<","<<t0q4::route_name(route)<<","<<s.qpc_start<<","<<s.qpc_end<<","<<s.worker_ns<<","<<s.group_ns<<","<<s.tsc_start<<","<<s.tsc_end<<","<<s.macs<<","<<s.logical_bytes<<",false\n";}
        double elapsed=sample.group_wall_ns;double amount=(kind==WorkKind::DRAM_COPY||byteMetric)?static_cast<double>(bytes):static_cast<double>(macs);throughput.push_back(amount/(elapsed*1e-9));}
    std::vector<double> absolute;double med=median(throughput);for(double x:throughput)absolute.push_back(std::fabs(x-med));double iqr=quantile(throughput,0.75)-quantile(throughput,0.25);double mad=median(absolute);
    std::string summaryPath=outdir+"\\"+name+"_summary.csv";if(GetFileAttributesA(summaryPath.c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("summary CSV already exists; refusing overwrite");std::ofstream summary(summaryPath,std::ios::binary);summary<<"series,metric,median,iqr,mad,min,max,N,warmup_count,outlier_discard_count\n";summary<<name<<","<<((kind==WorkKind::DRAM_COPY||byteMetric)?"bytes_per_second":"MAC_per_second")<<","<<med<<","<<iqr<<","<<mad<<","<<*std::min_element(throughput.begin(),throughput.end())<<","<<*std::max_element(throughput.begin(),throughput.end())<<","<<N_MEASURED<<","<<WARMUP_COUNT<<",0\n";return med;
}

int main(int argc,char**argv){
    if(argc<3){std::cerr<<"usage: t0_measure_harness.exe --plan <topology.csv> | --run-d <topology.csv> <outdir> | --run-e <topology.csv> <outdir> | --run-f <topology.csv> <outdir> | --dynamic-load <topology.csv> <P|E|MIXED> <worker_count> <seconds>\n";return 2;}
    std::string mode=argv[1];auto all=loadCores(argv[2]);std::vector<Core> p,e;for(auto c:all)(c.type=="P-core"?p:e).push_back(c);
    if(mode=="--plan"){
        std::cout<<"{\"status\":\"PLAN_ONLY_NO_MEASUREMENT\",\"N\":"<<N_MEASURED<<",\"warmup_count\":"<<WARMUP_COUNT<<",\"statistic\":\"median\",\"dispersion\":\"IQR+MAD\",\"outlier_discard_count\":0,\"fixed_order\":\""<<FIXED_SERIES_ORDER<<"\",\"BW_D_n_sweep\":[1,2,4,8,16],\"BW_D_bytes_per_worker\":"<<BW_D_BUFFER_BYTES_PER_WORKER<<",\"Q4_shape\":\"O=512,I=512,m=1; same Q4 operator, AVX2 for d and AVX2+VNNI when available for e\",\"worker_sets\":{\"A\":\"first 4 P-core API keys\",\"B\":\"all P-core API keys\",\"C\":\"all P and E core API keys\"},\"i_telemetry\":\"frequency via Windows PDH Processor Performance counters; power/temperature UNAVAILABLE_WITH_ALLOWED_INTERFACES unless an already-available source is found\"}\n";return 0;
    }
    if(mode=="--dynamic-load"){
        if(argc!=6)throw std::runtime_error("dynamic-load requires topology, P|E|MIXED, worker_count, seconds");std::string set=argv[3];int workerCount=std::stoi(argv[4]);int seconds=std::stoi(argv[5]);std::vector<Core>w= set=="P"?p:(set=="E"?e:(set=="MIXED"?all:std::vector<Core>{}));if(w.empty()||workerCount<=0||workerCount>(int)w.size()||seconds<=0)throw std::runtime_error("invalid dynamic load request");w.resize(workerCount);std::atomic<bool> go{false};std::vector<std::thread> threads;
        for(std::size_t i=0;i<w.size();++i)threads.emplace_back([&,i](){pinCurrent(w[i]);if(!t0q4::avx2_available())throw std::runtime_error("AVX2 unavailable on worker");auto weights=t0q4::generate_weights(512,512,0x493044594E414D49ULL+i);auto acts=t0q4::generate_activations(1,512,0x493044594E414D49ULL^i);t0q4::Evaluation eval;t0q4::execute_into_prechecked(weights,acts,t0q4::Route::AVX2,eval);while(!go.load(std::memory_order_acquire))YieldProcessor();ULONGLONG end=GetTickCount64()+static_cast<ULONGLONG>(seconds)*1000;while(GetTickCount64()<end)t0q4::execute_into_prechecked(weights,acts,t0q4::Route::AVX2,eval);});
        go.store(true,std::memory_order_release);for(auto&t:threads)t.join();std::cout<<"dynamic_load_complete worker_set="<<set<<" worker_count="<<w.size()<<" seconds="<<seconds<<" route=AVX2\n";return 0;
    }
    if(argc<4)throw std::runtime_error("measurement mode requires topology CSV and output directory");const std::string outdir=argv[3];
    if(mode=="--run-d"){for(const auto&c:all)writeRawSeries("d_vi_"+c.key,{c},WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir);return 0;}
    if(mode=="--run-e"){for(const auto&c:all){writeRawSeries("e_pico_avx2_"+c.key,{c},WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir);if(t0q4::avxvnni256_available())writeRawSeries("e_pico_vnni256_"+c.key,{c},WorkKind::Q4,t0q4::Route::AVXVNNI256,KERNEL_CALLS_PER_SAMPLE,outdir);}return 0;}
    if(mode=="--run-f"){
        const int ns[]={1,2,4,8,16};std::vector<Core>ordered=p;ordered.insert(ordered.end(),e.begin(),e.end());std::map<int,double>bwD;
        for(int n:ns)bwD[n]=writeRawSeries("f_bw_d_n"+std::to_string(n),std::vector<Core>(ordered.begin(),ordered.begin()+std::min<int>(n,(int)ordered.size())),WorkKind::DRAM_COPY,t0q4::Route::AVX2,1,outdir);
        for(const auto&c:all)(void)writeRawSeries("f_bw_l2_i_"+c.key,{c},WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir,true);
        std::vector<Core>A(p.begin(),p.begin()+std::min<std::size_t>(4,p.size())),B=p,C=p;C.insert(C.end(),e.begin(),e.end());
        double bwA=writeRawSeries("f_bw_l2_sigma_A_4P",A,WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir,true);double bwB=writeRawSeries("f_bw_l2_sigma_B_allP",B,WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir,true);double bwC=writeRawSeries("f_bw_l2_sigma_C_P_plus_E",C,WorkKind::Q4,t0q4::Route::AVX2,KERNEL_CALLS_PER_SAMPLE,outdir,true);
        std::string rPath=outdir+"\\f_r_summary.csv";if(GetFileAttributesA(rPath.c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("r summary already exists; refusing overwrite");std::ofstream rfile(rPath,std::ios::binary);rfile<<"candidate,l2_sigma_median_bytes_per_second,bw_d_n_median_bytes_per_second,r\n";rfile<<"A_4P,"<<bwA<<","<<bwD[4]<<","<<(bwA/bwD[4])<<"\n";rfile<<"B_allP,"<<bwB<<","<<bwD[8]<<","<<(bwB/bwD[8])<<"\n";rfile<<"C_P_plus_E,"<<bwC<<","<<bwD[16]<<","<<(bwC/bwD[16])<<"\n";return 0;
    }
    throw std::runtime_error("unrecognized harness mode");
}
