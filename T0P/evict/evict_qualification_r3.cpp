#define NOMINMAX
#include "evict.hpp"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kShardBytes = 294912;
constexpr std::size_t kLine = 64;
constexpr double kRegionSeconds = 2.5;
constexpr double kMarginSeconds = 0.200;
constexpr unsigned kWorkers = 8;
struct LargeShard {
  std::uint8_t* data=nullptr;
  std::size_t allocation=0;
  LargeShard() {
    const SIZE_T minimum=GetLargePageMinimum();
    if(!minimum) throw std::runtime_error("GetLargePageMinimum_failed");
    allocation=((kShardBytes+minimum-1)/minimum)*minimum;
    data=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,allocation,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE));
    if(!data) throw std::runtime_error("large_page_allocation_failed_win32="+std::to_string(GetLastError()));
    if(reinterpret_cast<std::uintptr_t>(data)%minimum) throw std::runtime_error("large_page_alignment_failed");
    std::uint32_t x=0x9e3779b9u;
    for(std::size_t i=0;i<kShardBytes;i+=4){x^=x<<13;x^=x>>17;x^=x<<5;const auto n=std::min<std::size_t>(4,kShardBytes-i);for(std::size_t j=0;j<n;++j)data[i+j]=std::uint8_t(x>>(8*j));}
  }
  ~LargeShard(){if(data)VirtualFree(data,0,MEM_RELEASE);}
  LargeShard(const LargeShard&)=delete; LargeShard& operator=(const LargeShard&)=delete;
};
std::uint64_t qpc(){LARGE_INTEGER v{};if(!QueryPerformanceCounter(&v))throw std::runtime_error("QueryPerformanceCounter_failed");return std::uint64_t(v.QuadPart);}
std::uint64_t qpf(){LARGE_INTEGER v{};if(!QueryPerformanceFrequency(&v))throw std::runtime_error("QueryPerformanceFrequency_failed");return std::uint64_t(v.QuadPart);}
std::uint64_t tsc(){_mm_lfence();unsigned aux=0;const auto v=__rdtscp(&aux);_mm_lfence();return v;}
double calibrate_tsc(){const auto f=qpf();const auto q0=qpc(),t0=tsc(),end=q0+f/4;while(qpc()<end){}const auto t1=tsc(),q1=qpc();if(q1<=q0||t1<=t0)throw std::runtime_error("TSC_QPC_calibration_invalid");return double(t1-t0)*double(f)/double(q1-q0);}
void pin_pcore(unsigned core){GROUP_AFFINITY req{},got{};req.Group=0;req.Mask=KAFFINITY(1u<<(2*core));if(!SetThreadGroupAffinity(GetCurrentThread(),&req,nullptr)||!GetThreadGroupAffinity(GetCurrentThread(),&got)||got.Group!=0||got.Mask!=req.Mask)throw std::runtime_error("worker_affinity_set_or_readback_failed_core="+std::to_string(core));PROCESSOR_NUMBER pn{};GetCurrentProcessorNumberEx(&pn);if(pn.Group!=0||pn.Number!=2*core)throw std::runtime_error("worker_not_on_expected_P_LP_core="+std::to_string(core));}
void verify_p_topology(){DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!bytes)throw std::runtime_error("processor_topology_query_failed");std::vector<unsigned char> buf(bytes);if(!GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),&bytes))throw std::runtime_error("processor_topology_read_failed");unsigned maxEff=0;struct C{std::set<unsigned> lp;unsigned eff;};std::vector<C> cores;for(DWORD off=0;off<bytes;){auto*p=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()+off);if(p->Relationship==RelationProcessorCore){C c{};c.eff=p->Processor.EfficiencyClass;maxEff=(std::max)(maxEff,c.eff);for(WORD g=0;g<p->Processor.GroupCount;++g)for(unsigned b=0;b<sizeof(KAFFINITY)*8;++b)if(p->Processor.GroupMask[g].Mask&(KAFFINITY(1)<<b))c.lp.insert(unsigned(g)*64+b);cores.push_back(std::move(c));}off+=p->Size;}for(unsigned core=0;core<kWorkers;++core){unsigned matches=0;for(const auto&c:cores)if(c.lp.count(core*2)){++matches;if(c.eff!=maxEff||!c.lp.count(core*2+1))throw std::runtime_error("target_LP_not_verified_P_core_SMT_pair="+std::to_string(core*2));}if(matches!=1)throw std::runtime_error("target_P_core_LP_not_unique="+std::to_string(core*2));}std::cout<<"TOPOLOGY=PASS P_LP=0,2,4,6,8,10,12,14 workers=8 SMT_pairs=verified\n";}
struct Recorder{std::vector<std::uintptr_t> addresses;};
void record_line(std::uintptr_t p,void*ctx){static_cast<Recorder*>(ctx)->addresses.push_back(p);}
void verify_shard_coverage(const std::array<LargeShard,kWorkers>&shards){
  for(unsigned core=0;core<kWorkers;++core){Recorder r;r.addresses.reserve(kShardBytes/kLine);omega_evict::evict_range_record(shards[core].data,kShardBytes,record_line,&r);
    std::set<std::uintptr_t> got(r.addresses.begin(),r.addresses.end());const auto base=reinterpret_cast<std::uintptr_t>(shards[core].data);std::set<std::uintptr_t>want;for(std::size_t i=0;i<kShardBytes;i+=kLine)want.insert(base+i);
    if(got!=want||r.addresses.size()!=kShardBytes/kLine)throw std::runtime_error("weight_shard_real_line_coverage_failed_core="+std::to_string(core));
    std::cout<<"WEIGHT_COVERAGE core="<<core<<" LP="<<2*core<<" bytes="<<kShardBytes<<" lines="<<r.addresses.size()<<" expected_lines="<<want.size()<<" status=PASS\n";
  }
}
std::uint64_t load_lines(const LargeShard&s){volatile const std::uint64_t*p=reinterpret_cast<volatile const std::uint64_t*>(s.data);std::uint64_t sum=0;for(std::size_t i=0;i<kShardBytes;i+=kLine)sum+=p[i/sizeof(std::uint64_t)];return sum;}
struct Marks{std::uint64_t padStart=0,start=0,end=0,padEnd=0;};
struct WorkerSamples{std::vector<std::uint64_t> cycles;std::uint64_t sink=0;};
void wait_until(std::uint64_t target,std::uint64_t){while(qpc()<target)Sleep(1);}
Marks run_condition(const std::array<LargeShard,kWorkers>&shards,bool cold,std::array<WorkerSamples,kWorkers>&samples,std::uint64_t freq){
  std::atomic<int>phase{0};std::atomic<unsigned>ready{0};std::atomic<bool>failed{false};std::mutex errMu;std::string err;std::array<std::thread,kWorkers>threads;
  for(unsigned core=0;core<kWorkers;++core)threads[core]=std::thread([&,core]{try{pin_pcore(core);ready.fetch_add(1,std::memory_order_release);while(phase.load(std::memory_order_acquire)>=0){const int p=phase.load(std::memory_order_relaxed);if(p<0)break;if(cold)omega_evict::evict_range(shards[core].data,kShardBytes);const auto begin=tsc();samples[core].sink^=load_lines(shards[core]);const auto elapsed=tsc()-begin;if(p==1)samples[core].cycles.push_back(elapsed);PROCESSOR_NUMBER here{};GetCurrentProcessorNumberEx(&here);if(here.Group!=0||here.Number!=2*core)throw std::runtime_error("worker_migrated_core="+std::to_string(core));}}catch(const std::exception&e){failed=true;{std::lock_guard<std::mutex>l(errMu);if(err.empty())err=e.what();}ready.fetch_add(1,std::memory_order_release);}});
  while(ready.load(std::memory_order_acquire)<kWorkers)Sleep(1);
  if(failed.load()){phase=-1;for(auto&t:threads)t.join();throw std::runtime_error(err);}
  Sleep(10);Marks m;m.padStart=qpc();wait_until(m.padStart+std::uint64_t(kMarginSeconds*freq),freq);m.start=qpc();phase.store(1,std::memory_order_release);wait_until(m.start+std::uint64_t(kRegionSeconds*freq),freq);m.end=qpc();phase.store(2,std::memory_order_release);wait_until(m.end+std::uint64_t(kMarginSeconds*freq),freq);m.padEnd=qpc();phase.store(-1,std::memory_order_release);for(auto&t:threads)t.join();if(failed.load())throw std::runtime_error(err);return m;
}
std::uint64_t quantile(std::vector<std::uint64_t>v,double p){if(v.empty())return 0;std::sort(v.begin(),v.end());const auto i=std::size_t(std::ceil(p*double(v.size())))-1;return v[(std::min)(i,v.size()-1)];}
void save_latencies(const std::string&condition,const std::array<WorkerSamples,kWorkers>&s,std::ofstream&out){for(unsigned core=0;core<kWorkers;++core){for(std::size_t i=0;i<s[core].cycles.size();++i)out<<core<<','<<2*core<<','<<condition<<','<<i<<','<<s[core].cycles[i]<<'\n';}}
void report_latency(unsigned core,const char*condition,const WorkerSamples&s){const auto med=quantile(s.cycles,.50),p95=quantile(s.cycles,.95);std::cout<<"CACHE_LATENCY Pcore="<<core<<" LP="<<2*core<<" condition="<<condition<<" samples="<<s.cycles.size()<<" median_cycles="<<med<<" p95_cycles="<<p95<<" sink="<<s.sink<<"\n";}
void write_marks(std::ofstream&f,const char*condition,const Marks&m){for(unsigned core=0;core<kWorkers;++core)f<<core<<','<<condition<<','<<m.padStart<<','<<m.start<<','<<m.end<<','<<m.padEnd<<'\n';f.flush();}
}

int main(int argc,char**argv){
  try{
    if(argc!=2||(std::string(argv[1])!="--qualification"&&std::string(argv[1])!="--revalidation"))throw std::runtime_error("usage: evict_qualification_r3.exe --qualification|--revalidation");
    const std::string kind=std::string(argv[1])=="--qualification"?"QUALIFICATION":"FINAL_REVALIDATION";
    const auto freq=qpf();const double ftsc=calibrate_tsc();verify_p_topology();const auto choice=omega_evict::selected_flush();
    std::cout<<std::setprecision(12)<<"RUN_KIND="<<kind<<"\nQPC_FREQ_HZ="<<freq<<"\nFTSC_QPC_HZ="<<ftsc<<"\nFLUSH_SELECTION="<<choice.name<<"\n";
    std::array<LargeShard,kWorkers>shards;verify_shard_coverage(shards);
    std::ofstream marks("eviction_windows.csv",std::ios::trunc),lat("latencies.csv",std::ios::trunc);if(!marks||!lat)throw std::runtime_error("result_file_open_failed");
    marks<<"Pcore,condition,qpc_pad_start,qpc_start,qpc_end,qpc_pad_end\n";lat<<"Pcore,LP,condition,sample,cycles\n";
    std::array<WorkerSamples,kWorkers>cold{},hot{};
    const Marks cm=run_condition(shards,true,cold,freq);write_marks(marks,"cold_shards8",cm);save_latencies("cold_shards8",cold,lat);
    const Marks hm=run_condition(shards,false,hot,freq);write_marks(marks,"hot_shards8",hm);save_latencies("hot_shards8",hot,lat);
    bool ratioPass=true;
    for(unsigned core=0;core<kWorkers;++core){report_latency(core,"cold_shards8",cold[core]);report_latency(core,"hot_shards8",hot[core]);const auto coldMed=quantile(cold[core].cycles,.50),hotMed=quantile(hot[core].cycles,.50);const double ratio=hotMed?double(coldMed)/double(hotMed):0.0;const bool ok=cold[core].cycles.size()>=20&&hot[core].cycles.size()>=20&&std::isfinite(ratio)&&ratio>=1.50;ratioPass=ratioPass&&ok;std::cout<<"COLD_HOT_RATIO Pcore="<<core<<" LP="<<2*core<<" cold_median_cycles="<<coldMed<<" hot_median_cycles="<<hotMed<<" ratio="<<ratio<<" cold_samples="<<cold[core].cycles.size()<<" hot_samples="<<hot[core].cycles.size()<<" status="<<(ok?"PASS":"HOLD")<<"\n";}
    std::cout<<"CONDITIONS shards=8 workers=8 bytes_per_shard=294912 total_bytes=2359296 margins_s=0.200 measured_s=2.500\n";
    std::cout<<"LATENCY_RATIO_CONTROL="<<(ratioPass?"PASS":"HOLD_EVICTION_NOT_QUALIFIED")<<"\n";
    return ratioPass?0:2;
  }catch(const std::exception&e){std::cerr<<"HOLD_EVICTION_NOT_QUALIFIED cause="<<e.what()<<"\n";return 2;}
}
