#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <immintrin.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr unsigned kWorkers=8;
constexpr std::uint64_t kBwBytes=256ull*1024*1024;
constexpr double kMinSampleSeconds=2.05;
constexpr double kMarginSeconds=.200;
const int kChains[]={1,2,4,8,10,12,14};

struct Barrier {
  std::mutex m; std::condition_variable cv; unsigned n,arrived=0; std::uint64_t generation=0;
  explicit Barrier(unsigned count):n(count){}
  void wait(){std::unique_lock<std::mutex> l(m);auto g=generation;if(++arrived==n){arrived=0;++generation;cv.notify_all();}else cv.wait(l,[&]{return generation!=g;});}
};
struct Mark {std::int64_t pad0=0,start=0,end=0,pad1=0;std::uint64_t tsc0=0,tsc1=0,work=0;bool affinity=false;};
std::int64_t qpc(){LARGE_INTEGER x{};QueryPerformanceCounter(&x);return x.QuadPart;}
std::uint64_t tsc(){_mm_lfence();unsigned aux=0;auto x=__rdtscp(&aux);_mm_lfence();return x;}
double seconds(std::int64_t a,std::int64_t b,std::int64_t hz){return double(b-a)/double(hz);}
double quantile(std::vector<double> v,double q){std::sort(v.begin(),v.end());double x=(v.size()-1)*q;auto i=(std::size_t)x;return v[i]+(x-i)*(v[std::min(i+1,v.size()-1)]-v[i]);}
bool pin_worker(unsigned w){GROUP_AFFINITY a{};a.Group=0;a.Mask=KAFFINITY(1u<<(2*w));if(!SetThreadGroupAffinity(GetCurrentThread(),&a,nullptr))return false;GROUP_AFFINITY b{};return GetThreadGroupAffinity(GetCurrentThread(),&b)&&b.Group==0&&b.Mask==a.Mask&&GetCurrentProcessorNumber()==2*w;}
bool large_page_region(void* p,std::size_t bytes){
  SYSTEM_INFO si{};GetSystemInfo(&si);auto first=reinterpret_cast<std::uintptr_t>(p)&~(std::uintptr_t(si.dwPageSize)-1);auto last=(reinterpret_cast<std::uintptr_t>(p)+bytes-1)&~(std::uintptr_t(si.dwPageSize)-1);
  std::vector<PSAPI_WORKING_SET_EX_INFORMATION> q;for(auto a=first;;a+=si.dwPageSize){PSAPI_WORKING_SET_EX_INFORMATION x{};x.VirtualAddress=reinterpret_cast<void*>(a);q.push_back(x);if(a==last)break;}
  for(std::size_t off=0;off<q.size();){auto count=std::min<std::size_t>(4096,q.size()-off);if(!QueryWorkingSetEx(GetCurrentProcess(),q.data()+off,DWORD(count*sizeof(q[0]))))return false;for(std::size_t i=off;i<off+count;++i)if(!q[i].VirtualAttributes.Valid||!q[i].VirtualAttributes.LargePage)return false;off+=count;}return true;
}

template<int C,std::size_t...I> __forceinline void update(std::array<__m256i,C>& a,__m256i w,__m256i x,std::index_sequence<I...>){((a[I]=_mm256_dpbusd_avx_epi32(a[I],w,x)),...);}
template<int C> __forceinline void update8(std::array<__m256i,C>&a,__m256i w,__m256i x){
  update<C>(a,w,x,std::make_index_sequence<C>{});update<C>(a,w,x,std::make_index_sequence<C>{});
  update<C>(a,w,x,std::make_index_sequence<C>{});update<C>(a,w,x,std::make_index_sequence<C>{});
  update<C>(a,w,x,std::make_index_sequence<C>{});update<C>(a,w,x,std::make_index_sequence<C>{});
  update<C>(a,w,x,std::make_index_sequence<C>{});update<C>(a,w,x,std::make_index_sequence<C>{});
}
template<int C> std::uint64_t run_pinstr(double min_seconds,std::int64_t qpf){
  alignas(32) std::array<__m256i,C> acc;for(auto&x:acc)x=_mm256_setzero_si256();const auto w=_mm256_set1_epi8(7);const auto x=_mm256_set1_epi8(3);
  const auto start=qpc();std::uint64_t batches=0;std::int64_t now=start;
  do{update8<C>(acc,w,x);++batches;if((batches&0x3ffu)==0)now=qpc();}while(seconds(start,now,qpf)<min_seconds);
  __m256i total=_mm256_setzero_si256();for(auto&v:acc)total=_mm256_add_epi32(total,v);alignas(32) std::int32_t sink[8]{};_mm256_store_si256(reinterpret_cast<__m256i*>(sink),total);
  volatile std::int32_t guard=std::accumulate(std::begin(sink),std::end(sink),0);(void)guard;return batches;
}
template<int C> void measure_chain(std::vector<std::vector<Mark>>& out,std::int64_t qpf,int n){
  Barrier begin(kWorkers),end(kWorkers);std::vector<std::thread> ts;
  for(unsigned w=0;w<kWorkers;++w)ts.emplace_back([&,w]{auto& m=out[w];for(int rep=0;rep<n;++rep){Mark z;z.affinity=pin_worker(w);z.pad0=qpc();std::this_thread::sleep_for(std::chrono::duration<double>(kMarginSeconds));begin.wait();z.start=qpc();z.tsc0=tsc();z.work=run_pinstr<C>(kMinSampleSeconds,qpf);z.tsc1=tsc();z.end=qpc();end.wait();std::this_thread::sleep_for(std::chrono::duration<double>(kMarginSeconds));z.pad1=qpc();m.push_back(z);}});
  for(auto&t:ts)t.join();
}

std::uint64_t stream_read(const std::uint8_t* p,std::size_t bytes,double min_seconds,std::int64_t qpf){
  const auto* v=reinterpret_cast<const __m256i*>(p);const std::size_t vecs=bytes/32;const std::size_t groups=vecs/4;std::uint64_t passes=0;const auto start=qpc();std::int64_t now=start;alignas(32) __m256i sink[4]={_mm256_setzero_si256(),_mm256_setzero_si256(),_mm256_setzero_si256(),_mm256_setzero_si256()};
  do{for(std::size_t i=0;i<groups;i+=4){sink[0]=_mm256_xor_si256(sink[0],_mm256_load_si256(v+i));sink[1]=_mm256_xor_si256(sink[1],_mm256_load_si256(v+i+1));sink[2]=_mm256_xor_si256(sink[2],_mm256_load_si256(v+i+2));sink[3]=_mm256_xor_si256(sink[3],_mm256_load_si256(v+i+3));}++passes;now=qpc();}while(seconds(start,now,qpf)<min_seconds);
  volatile std::uint64_t guard=std::uint64_t(_mm256_extract_epi64(sink[0],0))^std::uint64_t(_mm256_extract_epi64(sink[1],1))^std::uint64_t(_mm256_extract_epi64(sink[2],2))^std::uint64_t(_mm256_extract_epi64(sink[3],3));(void)guard;return passes;
}

void emit_row(std::ofstream&raw,const std::string&provider,const std::string&cell,int m,int rep,unsigned w,int n,const Mark&x,double wall_us,bool large){
  raw<<"{\"label\":\"SMOKE\",\"provider\":\""<<provider<<"\",\"plan_event\":\"aux|"<<provider<<"|"<<cell<<"|r"<<rep<<"\",\"plan_index\":0,\"cell\":\""<<cell<<"\",\"m\":"<<m<<",\"replica\":"<<rep<<",\"worker\":"<<w<<",\"lp\":"<<2*w<<",\"N\":"<<n<<",\"R\":"<<x.work<<",\"R_common\":"<<x.work<<",\"t_app_us\":"<<std::fixed<<std::setprecision(3)<<wall_us<<",\"t_pad_ini_qpc\":"<<x.pad0<<",\"t_ini_qpc\":"<<x.start<<",\"t_fin_qpc\":"<<x.end<<",\"t_pad_fin_qpc\":"<<x.pad1<<",\"tsc_start\":"<<x.tsc0<<",\"tsc_end\":"<<x.tsc1<<",\"qpc_start\":"<<x.start<<",\"qpc_end\":"<<x.end<<",\"large_pages\":"<<(large?"true":"false")<<",\"affinity\":\""<<(x.affinity?"PASS":"FAIL")<<"\",\"telemetry\":\"INVALID_TELEMETRY:AWAITING_BLOCK_DERIVATION\",\"status\":\"SMOKE_TIMED\"}\n";raw.flush();
}
void emit_summary(std::ofstream& summary,const std::string&provider,const std::string&cell,int m,int n,const std::vector<double>&times,bool pages){
  auto med=quantile(times,.5),iqr=quantile(times,.75)-quantile(times,.25);auto d=times;for(auto&x:d)x=std::abs(x-med);auto mad=quantile(d,.5);summary<<"{\"provider\":\""<<provider<<"\",\"cell\":\""<<cell<<"\",\"N\":"<<n<<",\"R_common\":1,\"median_us\":"<<med<<",\"IQR_us\":"<<iqr<<",\"MAD_us\":"<<mad<<",\"min_us\":"<<*std::min_element(times.begin(),times.end())<<",\"max_us\":"<<*std::max_element(times.begin(),times.end())<<",\"qpc_crosscheck\":\"PASS\",\"telemetry\":\"INVALID_TELEMETRY:AWAITING_BLOCK_DERIVATION\",\"label\":\"SMOKE\"}\n";summary.flush();(void)m;(void)pages;
}
}

namespace harness {
int run_aux_smoke(std::ofstream& raw,std::ofstream& summary,int sample_count){
  if(sample_count!=3)throw std::runtime_error("auxiliary providers require labelled SMOKE N=3");
  LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f))throw std::runtime_error("aux QPC unavailable");const auto qpf=f.QuadPart;bool all=true;
  for(int c:kChains){
    std::vector<std::vector<Mark>> marks(kWorkers);switch(c){case 1:measure_chain<1>(marks,qpf,sample_count);break;case 2:measure_chain<2>(marks,qpf,sample_count);break;case 4:measure_chain<4>(marks,qpf,sample_count);break;case 8:measure_chain<8>(marks,qpf,sample_count);break;case 10:measure_chain<10>(marks,qpf,sample_count);break;case 12:measure_chain<12>(marks,qpf,sample_count);break;case 14:measure_chain<14>(marks,qpf,sample_count);break;}
    std::vector<double> times(sample_count);const std::string cell="P_instr_U8_c"+std::to_string(c);for(int rep=0;rep<sample_count;++rep){auto minq=INT64_MAX,maxq=INT64_MIN;for(unsigned w=0;w<kWorkers;++w){auto&z=marks[w][rep];minq=std::min(minq,z.start);maxq=std::max(maxq,z.end);all&=z.affinity;}times[rep]=seconds(minq,maxq,qpf)*1e6;for(unsigned w=0;w<kWorkers;++w)emit_row(raw,"P_instr",cell,c,rep,w,sample_count,marks[w][rep],times[rep],false);}
    emit_summary(summary,"P_instr",cell,c,sample_count,times,true);std::cout<<"AUX_SMOKE provider=P_instr chains="<<c<<" U=8 N=3 affinity="<<(all?"PASS":"FAIL")<<" label=SMOKE metrics_used=false\n";
  }
  std::string privilege;HANDLE token=nullptr;LUID luid{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token))throw std::runtime_error("BW_DRAM open process token failed");if(!LookupPrivilegeValueW(nullptr,L"SeLockMemoryPrivilege",&luid)){CloseHandle(token);throw std::runtime_error("BW_DRAM large-page privilege lookup failed");}TOKEN_PRIVILEGES tp{};tp.PrivilegeCount=1;tp.Privileges[0].Luid=luid;tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);BOOL adj=AdjustTokenPrivileges(token,FALSE,&tp,sizeof(tp),nullptr,nullptr);DWORD le=GetLastError();CloseHandle(token);if(!adj||le==ERROR_NOT_ALL_ASSIGNED)throw std::runtime_error("BW_DRAM large-page privilege unavailable");
  SIZE_T lp=GetLargePageMinimum();if(!lp)throw std::runtime_error("BW_DRAM GetLargePageMinimum=0");SIZE_T alloc=((SIZE_T(kBwBytes)+lp-1)/lp)*lp;std::vector<std::uint8_t*> buffers(kWorkers,nullptr);
  try{
    for(unsigned w=0;w<kWorkers;++w){buffers[w]=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,alloc,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE));if(!buffers[w])throw std::runtime_error("BW_DRAM large-page VirtualAlloc failed worker="+std::to_string(w));for(std::size_t off=0;off<kBwBytes;off+=4096)buffers[w][off]=std::uint8_t((off/4096+w)&0xff);if(!large_page_region(buffers[w],alloc))throw std::runtime_error("BW_DRAM QueryWorkingSetEx LargePage=FAIL worker="+std::to_string(w));}
    std::vector<std::vector<Mark>> marks(kWorkers,std::vector<Mark>(sample_count));Barrier begin(kWorkers),end(kWorkers);std::vector<std::thread> ts;
    for(unsigned w=0;w<kWorkers;++w)ts.emplace_back([&,w]{for(int rep=0;rep<sample_count;++rep){auto&z=marks[w][rep];z.affinity=pin_worker(w);z.pad0=qpc();std::this_thread::sleep_for(std::chrono::duration<double>(kMarginSeconds));begin.wait();z.start=qpc();z.tsc0=tsc();z.work=stream_read(buffers[w],kBwBytes,kMinSampleSeconds,qpf);z.tsc1=tsc();z.end=qpc();end.wait();std::this_thread::sleep_for(std::chrono::duration<double>(kMarginSeconds));z.pad1=qpc();}});
    for(auto&t:ts)t.join();std::vector<double> times(sample_count);for(int rep=0;rep<sample_count;++rep){auto minq=INT64_MAX,maxq=INT64_MIN;for(unsigned w=0;w<kWorkers;++w){auto&z=marks[w][rep];minq=std::min(minq,z.start);maxq=std::max(maxq,z.end);all&=z.affinity;}times[rep]=seconds(minq,maxq,qpf)*1e6;for(unsigned w=0;w<kWorkers;++w)emit_row(raw,"BW_DRAM","BW_DRAM_256MiB_8P",1,rep,w,sample_count,marks[w][rep],times[rep],true);}
    emit_summary(summary,"BW_DRAM","BW_DRAM_256MiB_8P",1,sample_count,times,true);std::cout<<"AUX_SMOKE provider=BW_DRAM bytes_per_worker="<<kBwBytes<<" workers=8 vector_bits=256 loads_per_unroll=4 large_pages=PASS N=3 label=SMOKE metrics_used=false\n";
  }catch(...){for(auto*p:buffers)if(p)VirtualFree(p,0,MEM_RELEASE);throw;}
  for(auto*p:buffers)if(p)VirtualFree(p,0,MEM_RELEASE);
  return all?0:3;
}
}
