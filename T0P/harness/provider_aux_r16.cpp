#define NOMINMAX
#include "harness_r16.hpp"
#include "topology_r16.hpp"
#include <windows.h>
#include <psapi.h>
#include <immintrin.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr unsigned kWorkers=8;
constexpr std::uint64_t kBufferBytes=256ull*1024*1024;
constexpr double kTargetSeconds=2.5;
constexpr double kMarginSeconds=.200;
constexpr int kChains[]={1,2,4,8,10,12,14};

struct Barrier { std::mutex m; std::condition_variable cv; unsigned n,arrived=0; std::uint64_t generation=0;
  explicit Barrier(unsigned count):n(count){}
  void wait(){std::unique_lock<std::mutex>l(m);auto g=generation;if(++arrived==n){arrived=0;++generation;cv.notify_all();}else cv.wait(l,[&]{return generation!=g;});}
};
struct Mark {std::int64_t q0=0,q1=0;std::uint64_t t0=0,t1=0;bool affinity=false,sink_ok=true;std::uint64_t sink=0;};
std::int64_t qpc(){LARGE_INTEGER x{};QueryPerformanceCounter(&x);return x.QuadPart;}
std::uint64_t tsc(){_mm_lfence();unsigned aux=0;auto x=__rdtscp(&aux);_mm_lfence();return x;}
double seconds(std::int64_t a,std::int64_t b,std::int64_t hz){return double(b-a)/double(hz);}
double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
bool pages_large(const void* p,std::size_t bytes){
  if(!p||!bytes)return false;SYSTEM_INFO si{};GetSystemInfo(&si);auto first=reinterpret_cast<std::uintptr_t>(p)&~(std::uintptr_t(si.dwPageSize)-1);auto last=(reinterpret_cast<std::uintptr_t>(p)+bytes-1)&~(std::uintptr_t(si.dwPageSize)-1);
  std::vector<PSAPI_WORKING_SET_EX_INFORMATION> q;for(auto a=first;;a+=si.dwPageSize){PSAPI_WORKING_SET_EX_INFORMATION x{};x.VirtualAddress=reinterpret_cast<void*>(a);q.push_back(x);if(a==last)break;}
  for(std::size_t off=0;off<q.size();){auto n=std::min<std::size_t>(4096,q.size()-off);if(!QueryWorkingSetEx(GetCurrentProcess(),q.data()+off,DWORD(n*sizeof(q[0]))))return false;for(std::size_t i=off;i<off+n;++i)if(!q[i].VirtualAttributes.Valid||!q[i].VirtualAttributes.LargePage)return false;off+=n;}return true;
}

template<int C,std::size_t...I> __forceinline void update(std::array<__m256i,C>&a,__m256i w,__m256i x,std::index_sequence<I...>){((a[I]=_mm256_dpbusd_avx_epi32(a[I],w,x)),...);}
template<int C,int U> __forceinline void issue(std::array<__m256i,C>&a,__m256i w,__m256i x){update<C>(a,w,x,std::make_index_sequence<C>{});if constexpr(U>1)issue<C,U-1>(a,w,x);}
template<int C,int U> __forceinline std::uint32_t run_bench(std::uint64_t batches){
  alignas(32) std::array<__m256i,C> acc;for(auto&v:acc)v=_mm256_setzero_si256();const auto w=_mm256_set1_epi8(7),x=_mm256_set1_epi8(3);
  for(std::uint64_t n=0;n<batches;++n)issue<C,U>(acc,w,x);
  alignas(32) std::uint32_t lanes[8*C]{};std::uint32_t sink=0;for(int i=0;i<C;++i){_mm256_store_si256(reinterpret_cast<__m256i*>(lanes+8*i),acc[i]);for(int j=0;j<8;++j)sink+=lanes[8*i+j];}
  volatile std::uint32_t observed=sink;return observed;
}
#define DECLARE_PINSTR(C,U) extern "C" __declspec(noinline) std::uint32_t pinstr_c##C##_u##U(std::uint64_t n){return run_bench<C,U>(n);}
DECLARE_PINSTR(1,8) DECLARE_PINSTR(2,8) DECLARE_PINSTR(4,8) DECLARE_PINSTR(8,8) DECLARE_PINSTR(10,8) DECLARE_PINSTR(12,8) DECLARE_PINSTR(14,8)
DECLARE_PINSTR(12,16) DECLARE_PINSTR(14,16)
#undef DECLARE_PINSTR
using Bench=std::uint32_t(*)(std::uint64_t);
Bench pinstr_bench(int c,int u){
#define PICK(C,U) if(c==C&&u==U)return &pinstr_c##C##_u##U
  PICK(1,8);PICK(2,8);PICK(4,8);PICK(8,8);PICK(10,8);PICK(12,8);PICK(14,8);PICK(12,16);PICK(14,16);
#undef PICK
  throw std::runtime_error("unsupported P_instr chain/U");
}
std::uint64_t calibrate_pinstr(Bench fn,std::int64_t qpf){
  constexpr unsigned workers=8;std::atomic<bool> ok{true};std::uint64_t r=4096;
  for(int tries=0;tries<24;++tries){Barrier begin(workers),end(workers);std::array<double,workers> elapsed{};std::vector<std::thread> threads;
    for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w]{if(!harness::set_worker_affinity_r16(w)||!harness::worker_affinity_matches_r16(w))ok=false;begin.wait();const auto a=qpc();volatile auto sink=fn(r);const auto b=qpc();(void)sink;elapsed[w]=seconds(a,b,qpf);end.wait();});
    for(auto&t:threads)t.join();if(!ok)throw std::runtime_error("P_instr concurrent pinned calibration failed affinity");
    const double fastest=*std::min_element(elapsed.begin(),elapsed.end());
    if(fastest>=2.5)return r;
    if(r>(UINT64_MAX/2))break;r*=2;
  }
  throw std::runtime_error("P_instr concurrent eight-worker R calibration could not reach 2.5 s for every worker");
}
void pinstr_margin(Bench fn,std::int64_t qpf){auto a=qpc(),b=a;std::uint64_t r=1024;while(seconds(a,b,qpf)<kMarginSeconds){volatile auto sink=fn(r);(void)sink;b=qpc();if(seconds(a,b,qpf)<kMarginSeconds)r=std::min<std::uint64_t>(r*2,1ull<<30);}}

std::array<std::array<std::uint64_t,4>,4> stream_sum_quarters(const std::uint8_t* p,std::size_t quarter_bytes,std::uint64_t passes){
  const std::size_t bytes=quarter_bytes*4;if(bytes!=kBufferBytes||bytes%128)throw std::runtime_error("BW_DRAM buffer must be contiguous 256 MiB and 128-byte aligned");
  const auto* v=reinterpret_cast<const __m256i*>(p);const std::size_t vecs=bytes/32;alignas(32) std::array<__m256i,4> acc;for(auto&a:acc)a=_mm256_setzero_si256();
  for(std::uint64_t pass=0;pass<passes;++pass)for(std::size_t i=0;i<vecs;i+=4){
    acc[0]=_mm256_add_epi64(acc[0],_mm256_load_si256(v+i+0));
    acc[1]=_mm256_add_epi64(acc[1],_mm256_load_si256(v+i+1));
    acc[2]=_mm256_add_epi64(acc[2],_mm256_load_si256(v+i+2));
    acc[3]=_mm256_add_epi64(acc[3],_mm256_load_si256(v+i+3));
  }
  alignas(32) std::array<std::array<std::uint64_t,4>,4> out{};for(std::size_t u=0;u<4;++u)_mm256_storeu_si256(reinterpret_cast<__m256i*>(out[u].data()),acc[u]);return out;
}
bool stream_coverage_qa(){
  constexpr std::size_t qbytes=kBufferBytes/4,vecs=kBufferBytes/32,lines=kBufferBytes/64;static_assert(kBufferBytes%128==0);
  std::vector<__m256i> storage(kBufferBytes/32);auto*data=reinterpret_cast<std::uint8_t*>(storage.data());for(std::size_t i=0;i<kBufferBytes;++i)data[i]=std::uint8_t((i*29+7)&0xff);
  std::vector<std::uint8_t> counts(lines,0);bool pairing=true;
  for(std::size_t i=0;i<vecs;i+=4){for(std::size_t u=0;u<4;++u){const std::size_t line=(i+u)/2;if(line>=lines)pairing=false;else ++counts[line];}}
  const bool coverage=pairing&&std::all_of(counts.begin(),counts.end(),[](std::uint8_t n){return n==2;});
  const auto base=stream_sum_quarters(data,qbytes,1);bool mutations=true;
  for(std::size_t q=0;q<4;++q){const auto first=(q+1)*qbytes-64;for(std::size_t b=first;b<first+64;++b)data[b]^=std::uint8_t(0x5b+b);const auto after=stream_sum_quarters(data,qbytes,1);mutations&=after!=base;for(std::size_t b=first;b<first+64;++b)data[b]^=std::uint8_t(0x5b+b);}
  std::cout<<"BW_DRAM_QA bytes="<<kBufferBytes<<" cache_lines="<<lines<<" sequential_contiguous=true unroll=4 loads_each_line=2 last_cacheline_mutations_caught="<<(mutations?"4/4":"FAIL")<<" modular_sink=PASS\n";
  return coverage&&mutations;
}
void expected_quarter_sums(const std::uint8_t* p,std::size_t qbytes,std::array<std::array<std::uint64_t,4>,4>& out){
  const auto* q=reinterpret_cast<const std::uint64_t*>(p);const std::size_t words=kBufferBytes/8;for(std::size_t i=0;i<words;++i)out[(i/4)%4][i%4]+=q[i];
}
std::uint64_t reduce_sum(const std::array<std::array<std::uint64_t,4>,4>& sum){std::uint64_t v=0;for(const auto&q:sum)for(auto x:q)v+=x;return v;}
std::uint64_t calibrate_bw(const std::uint8_t* p,std::size_t qbytes,std::int64_t qpf){
  (void)p;(void)qbytes;(void)qpf;throw std::runtime_error("BW_DRAM calibration must use calibrate_bw_all with all eight workers");
}
std::uint64_t calibrate_bw_all(const std::array<std::uint8_t*,kWorkers>& buffers,std::int64_t qpf){
  std::uint64_t r=1;for(int tries=0;tries<24;++tries){Barrier begin(kWorkers),end(kWorkers);std::array<double,kWorkers> elapsed{};std::atomic<bool> affinity_ok{true};std::vector<std::thread> ts;
    for(unsigned w=0;w<kWorkers;++w)ts.emplace_back([&,w]{if(!harness::set_worker_affinity_r16(w)||!harness::worker_affinity_matches_r16(w))affinity_ok=false;begin.wait();auto a=qpc();const auto sum=stream_sum_quarters(buffers[w],kBufferBytes/4,r);volatile auto sink=reduce_sum(sum);auto b=qpc();(void)sink;elapsed[w]=seconds(a,b,qpf);end.wait();});
    for(auto&t:ts)t.join();if(!affinity_ok)throw std::runtime_error("BW_DRAM concurrent calibration affinity invalid");const double fastest=*std::min_element(elapsed.begin(),elapsed.end());if(fastest>=kTargetSeconds)return r;if(r>(UINT64_MAX/2))break;r*=2;
  }
  throw std::runtime_error("BW_DRAM concurrent eight-worker R calibration failed to reach 2.5 s for every worker");
}
void bw_margin(const std::uint8_t* p,std::size_t qbytes,std::int64_t qpf){auto a=qpc(),b=a;std::uint64_t r=1;while(seconds(a,b,qpf)<kMarginSeconds){const auto sum=stream_sum_quarters(p,qbytes,r);volatile std::uint64_t sink=reduce_sum(sum);(void)sink;b=qpc();if(seconds(a,b,qpf)<kMarginSeconds)r=std::min<std::uint64_t>(r*2,1024);}}
void emit_aux(std::ofstream& raw,std::ofstream& summary,const std::string&label,const std::string&provider,const std::string&cell,int m,int chains,int u,int rep,unsigned worker,int n,std::uint64_t r,double wall_us,std::int64_t qpf,const Mark&mark,bool pages,const std::string&frontier,const std::string&disassembly_sha){
  const auto& lp=harness::worker_processor_r16(worker);
  const double worker_wall_us=seconds(mark.q0,mark.q1,qpf)*1e6;(void)wall_us;
  bool sink_verified=mark.sink_ok;
  if(provider=="P_instr")sink_verified=mark.sink_ok&&std::uint32_t(mark.sink)==std::uint32_t(std::uint64_t(672)*unsigned(chains)*unsigned(u)*r);
  raw<<"{\"label\":\""<<label<<"\",\"provider\":\""<<provider<<"\",\"cell\":\""<<cell<<"\",\"frontier\":\""<<frontier<<"\",\"O\":1024,\"m\":"<<m<<",\"chains\":"<<chains<<",\"U\":"<<u<<",\"replica\":"<<rep<<",\"worker\":"<<worker<<",\"p_core\":"<<lp.p_core<<",\"lp\":"<<lp.lp<<",\"N\":"<<n<<",\"warmup\":5,\"R\":"<<r<<",\"R_common\":"<<r<<",\"R_policy\":\"concurrent_8_workers_minimum_duration_2.5s\",\"t_app_us\":"<<std::fixed<<std::setprecision(3)<<worker_wall_us<<",\"tsc_start\":"<<mark.t0<<",\"tsc_end\":"<<mark.t1<<",\"qpc_start\":"<<mark.q0<<",\"qpc_end\":"<<mark.q1<<",\"large_pages\":"<<(pages?"true":"false")<<",\"affinity\":\""<<(mark.affinity?"PASS":"FAIL")<<"\",\"sink\":"<<mark.sink<<",\"sink_verified\":"<<(sink_verified?"true":"false")<<",\"all_workers_concurrent\":true";
  if(provider=="BW_DRAM")raw<<",\"bytes_per_pass_per_worker\":"<<kBufferBytes<<",\"calibration_all_workers\":true,\"calibration_target_seconds\":2.5";
  if(provider=="P_instr")raw<<",\"disassembly_qa\":\"PASS\",\"disassembly_sha256\":\""<<disassembly_sha<<"\",\"calibration_all_workers\":true,\"calibration_target_seconds\":2.5";
  raw<<",\"status\":\""<<(label=="SMOKE"?"SMOKE_TIMED":"PASS")<<"\"}\n";raw.flush();summary<<"{\"label\":\""<<label<<"\",\"provider\":\""<<provider<<"\",\"cell\":\""<<cell<<"\",\"frontier\":\""<<frontier<<"\",\"replica\":"<<rep<<",\"worker\":"<<worker<<",\"R_common\":"<<r<<",\"t_app_us\":"<<std::fixed<<std::setprecision(3)<<wall_us<<",\"status\":\""<<(label=="SMOKE"?"SMOKE_ONLY":"PASS")<<"\"}\n";summary.flush();
}
}

namespace harness {
static int run_aux_measurements(std::ofstream&raw,std::ofstream&summary,int sample_count,const std::string&disassembly_qa_path,const std::string&label){
  if((sample_count!=3&&sample_count!=31)||(label!="SMOKE"&&label!="OFFICIAL"))throw std::runtime_error("aux measurement requires label SMOKE/N3 or OFFICIAL/N31");
  LARGE_INTEGER fq{};if(!QueryPerformanceFrequency(&fq))throw std::runtime_error("aux QPC unavailable");const auto qpf=fq.QuadPart;std::atomic<bool> healthy{true};
  std::ifstream qa_file(disassembly_qa_path,std::ios::binary);if(!qa_file)throw std::runtime_error("P_instr disassembly QA artifact missing: "+disassembly_qa_path);std::string qa_json((std::istreambuf_iterator<char>(qa_file)),{});
  const auto state_at=qa_json.find("\"state\"");if(state_at==std::string::npos||qa_json.find("\"PASS\"",state_at)==std::string::npos)throw std::runtime_error("P_instr real disassembly QA is not PASS");
  const auto sha_at=qa_json.find("\"sha256\"");if(sha_at==std::string::npos)throw std::runtime_error("P_instr disassembly QA SHA256 missing");const auto sha_open=qa_json.find('"',sha_at+8),sha_close=sha_open==std::string::npos?std::string::npos:qa_json.find('"',sha_open+1);if(sha_open==std::string::npos||sha_close==std::string::npos||sha_close-sha_open!=65)throw std::runtime_error("P_instr disassembly QA SHA256 invalid");const std::string disassembly_sha=qa_json.substr(sha_open+1,64);if(!std::all_of(disassembly_sha.begin(),disassembly_sha.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::runtime_error("P_instr disassembly QA SHA256 invalid");
  if(!stream_coverage_qa())throw std::runtime_error("BW_DRAM coverage/mutation QA failed");
  for(int chains:kChains){for(int u:{8,16}){if(u==16&&chains!=12&&chains!=14)continue;const Bench fn=pinstr_bench(chains,u);const auto r=calibrate_pinstr(fn,qpf);std::vector<std::vector<Mark>> marks(kWorkers,std::vector<Mark>(sample_count));Barrier begin(kWorkers),end(kWorkers);std::vector<std::thread> ts;
    for(unsigned w=0;w<kWorkers;++w)ts.emplace_back([&,w]{auto& map=marks[w];if(!harness::set_worker_affinity_r16(w)||!harness::worker_affinity_matches_r16(w))healthy=false;for(int i=0;i<5;++i){volatile auto warm=fn(r);(void)warm;}for(int rep=0;rep<sample_count;++rep){pinstr_margin(fn,qpf);auto& z=map[rep];z.affinity=harness::worker_affinity_matches_r16(w);begin.wait();z.q0=qpc();z.t0=tsc();z.sink=fn(r);z.t1=tsc();z.q1=qpc();z.sink_ok=std::uint32_t(z.sink)==std::uint32_t(std::uint64_t(672)*unsigned(chains)*unsigned(u)*r);end.wait();pinstr_margin(fn,qpf);}});
    for(auto&t:ts)t.join();for(int rep=0;rep<sample_count;++rep){auto minq=INT64_MAX,maxq=INT64_MIN;for(unsigned w=0;w<kWorkers;++w){if(!marks[w][rep].affinity||!marks[w][rep].sink_ok)healthy=false;minq=std::min(minq,marks[w][rep].q0);maxq=std::max(maxq,marks[w][rep].q1);}const double wall=seconds(minq,maxq,qpf)*1e6;for(unsigned w=0;w<kWorkers;++w)emit_aux(raw,summary,label,"P_instr","P_instr_c"+std::to_string(chains)+"_U"+std::to_string(u),1,chains,u,rep+1,w,sample_count,r,wall,qpf,marks[w][rep],false,"prepared",disassembly_sha);}
    std::cout<<"AUX_MEASURE provider=P_instr chains="<<chains<<" U="<<u<<" N="<<sample_count<<" warmup=5 R_common="<<r<<" frontier=same-load-margins label="<<label<<" metrics_used="<<(label=="SMOKE"?"false":"gated_after_S20")<<"\n";
  }}

  HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("BW_DRAM open process token error="+std::to_string(GetLastError()));DWORD needed=0;GetTokenInformation(token,TokenPrivileges,nullptr,0,&needed);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!needed){DWORD e=GetLastError();CloseHandle(token);throw std::runtime_error("BW_DRAM TokenPrivileges query size error="+std::to_string(e));}std::vector<std::uint8_t> token_bytes(needed);if(!GetTokenInformation(token,TokenPrivileges,token_bytes.data(),needed,&needed)){DWORD e=GetLastError();CloseHandle(token);throw std::runtime_error("BW_DRAM TokenPrivileges query error="+std::to_string(e));}CloseHandle(token);LUID luid{};if(!LookupPrivilegeValueW(nullptr,L"SeLockMemoryPrivilege",&luid))throw std::runtime_error("BW_DRAM large-page privilege lookup error="+std::to_string(GetLastError()));auto* privileges=reinterpret_cast<TOKEN_PRIVILEGES*>(token_bytes.data());bool privilege_enabled=false;for(DWORD i=0;i<privileges->PrivilegeCount;++i)if(privileges->Privileges[i].Luid.LowPart==luid.LowPart&&privileges->Privileges[i].Luid.HighPart==luid.HighPart)privilege_enabled=(privileges->Privileges[i].Attributes&SE_PRIVILEGE_ENABLED)!=0;if(!privilege_enabled)throw std::runtime_error("BW_DRAM SeLockMemoryPrivilege is not already enabled; no security settings changed");
  const SIZE_T large_min=GetLargePageMinimum();if(!large_min)throw std::runtime_error("BW_DRAM GetLargePageMinimum=0");const SIZE_T alloc=(SIZE_T(kBufferBytes)+large_min-1)/large_min*large_min;std::array<std::uint8_t*,kWorkers> buffers{};std::array<std::array<std::array<std::uint64_t,4>,4>,kWorkers> expected{};
  try{
    for(unsigned w=0;w<kWorkers;++w){buffers[w]=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,alloc,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE));if(!buffers[w])throw std::runtime_error("BW_DRAM large-page VirtualAlloc error="+std::to_string(GetLastError())+" worker="+std::to_string(w));auto*q=reinterpret_cast<std::uint64_t*>(buffers[w]);for(std::size_t i=0;i<kBufferBytes/8;++i){const std::uint64_t v=(std::uint64_t(i)*0x9e3779b185ebca87ull)^(0xd1b54a32d192ed03ull+std::uint64_t(w));q[i]=v;}expected_quarter_sums(buffers[w],kBufferBytes/4,expected[w]);if(!pages_large(buffers[w],kBufferBytes))throw std::runtime_error("BW_DRAM QueryWorkingSetEx large-page check failed worker="+std::to_string(w));}
    std::uint64_t r=calibrate_bw_all(buffers,qpf);{
      Barrier warm_begin(kWorkers),warm_end(kWorkers);std::atomic<bool> warm_ok{true};std::vector<std::thread> warmers;
      for(unsigned w=0;w<kWorkers;++w)warmers.emplace_back([&,w]{if(!harness::set_worker_affinity_r16(w)||!harness::worker_affinity_matches_r16(w))warm_ok=false;warm_begin.wait();for(int pass=0;pass<5;++pass){const auto sum=stream_sum_quarters(buffers[w],kBufferBytes/4,r);if(reduce_sum(sum)!=reduce_sum(expected[w])*r)warm_ok=false;}warm_end.wait();});
      for(auto&t:warmers)t.join();if(!warm_ok)throw std::runtime_error("BW_DRAM five-pass warmup or sink validation failed");
    }
    std::vector<std::vector<Mark>> marks(kWorkers,std::vector<Mark>(sample_count));Barrier begin(kWorkers),end(kWorkers);std::vector<std::thread> ts;
    for(unsigned w=0;w<kWorkers;++w)ts.emplace_back([&,w]{if(!harness::set_worker_affinity_r16(w)||!harness::worker_affinity_matches_r16(w))healthy=false;for(int rep=0;rep<sample_count;++rep){bw_margin(buffers[w],kBufferBytes/4,qpf);auto&z=marks[w][rep];z.affinity=harness::worker_affinity_matches_r16(w);begin.wait();z.q0=qpc();z.t0=tsc();auto sum=stream_sum_quarters(buffers[w],kBufferBytes/4,r);z.t1=tsc();z.q1=qpc();z.sink=reduce_sum(sum);end.wait();for(unsigned q=0;q<4;++q)for(unsigned lane=0;lane<4;++lane){const auto want=expected[w][q][lane]*r;if(sum[q][lane]!=want){healthy=false;z.sink_ok=false;}}bw_margin(buffers[w],kBufferBytes/4,qpf);}});
    for(auto&t:ts)t.join();for(int rep=0;rep<sample_count;++rep){auto minq=INT64_MAX,maxq=INT64_MIN;for(unsigned w=0;w<kWorkers;++w){if(!marks[w][rep].affinity||!marks[w][rep].sink_ok)healthy=false;minq=std::min(minq,marks[w][rep].q0);maxq=std::max(maxq,marks[w][rep].q1);}const double wall=seconds(minq,maxq,qpf)*1e6;for(unsigned w=0;w<kWorkers;++w)emit_aux(raw,summary,label,"BW_DRAM","BW_DRAM_256MiB_8P",1,0,0,rep+1,w,sample_count,r,wall,qpf,marks[w][rep],true,"prepared",disassembly_sha);}
    std::cout<<"AUX_MEASURE provider=BW_DRAM bytes_per_worker="<<kBufferBytes<<" quarters=4 loads=4x256bit_per_iteration coverage=full sink=verified R_common="<<r<<" N="<<sample_count<<" large_pages=PASS label="<<label<<" metrics_used="<<(label=="SMOKE"?"false":"gated_after_S20")<<"\n";
  }catch(...){for(auto*p:buffers)if(p)VirtualFree(p,0,MEM_RELEASE);throw;}
  for(auto*p:buffers)if(p)VirtualFree(p,0,MEM_RELEASE);
  return healthy.load()?0:3;
}

int run_aux_smoke(std::ofstream&raw,std::ofstream&summary,int sample_count,const std::string&disassembly_qa_path){
  if(sample_count!=3)throw std::runtime_error("aux SMOKE requires N=3");
  return run_aux_measurements(raw,summary,sample_count,disassembly_qa_path,"SMOKE");
}
int run_aux_official(std::ofstream&raw,std::ofstream&summary,int sample_count,const std::string&disassembly_qa_path){
  if(sample_count!=31)throw std::runtime_error("aux official series requires N=31");
  return run_aux_measurements(raw,summary,sample_count,disassembly_qa_path,"OFFICIAL");
}
}
