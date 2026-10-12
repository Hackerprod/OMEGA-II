#define NOMINMAX
#include "evict.hpp"
#include "../common/common.hpp"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <numeric>
#include <atomic>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <atomic>

using namespace omega_evict;
static std::atomic<size_t> gLargeAllocations{0};
struct Mem { t0common::PageRegion r; size_t n; explicit Mem(size_t z):r(z),n(z){SIZE_T min=GetLargePageMinimum();if(!r.data||!r.large||!min||((uintptr_t)r.data%min)!=0||r.bytes<z)throw std::runtime_error("real large-page allocation/alignment unavailable");++gLargeAllocations;std::memset(r.data,0xA5,z);} };
static size_t wb(int o){return size_t(o)*288;}
struct Recorder { std::vector<uintptr_t> v; std::atomic<size_t> used{0}; explicit Recorder(size_t n):v(n){} };
static void record_line(uintptr_t p,void*c){auto*r=static_cast<Recorder*>(c);auto i=r->used.fetch_add(1,std::memory_order_relaxed);if(i<r->v.size())r->v[i]=p;}
static std::set<uintptr_t> lines(const void*p,size_t n){std::set<uintptr_t>s;auto a=(uintptr_t)p&~uintptr_t(63),z=((uintptr_t)p+n-1)&~uintptr_t(63);for(;a<=z;a+=64)s.insert(a);return s;}
static void check_map(int o,Mem& w,const std::vector<Region>&rs,const std::vector<Region>&non) {
  std::set<uintptr_t> want,foreign;
  for(auto&r:rs){auto s=lines(r.ptr,r.bytes);want.insert(s.begin(),s.end());}
  for(auto&r:non){auto s=lines(r.ptr,r.bytes);foreign.insert(s.begin(),s.end());}
  size_t max_attempts=0;for(auto&r:rs)max_attempts+=size_t((((uintptr_t)r.ptr+r.bytes-1&~uintptr_t(63))-((uintptr_t)r.ptr&~uintptr_t(63)))/64)+1;
  Recorder raw(max_attempts);evict_weights_regions_record(rs,8,record_line,&raw);
  std::set<uintptr_t> got(raw.v.begin(),raw.v.begin()+raw.used.load());std::vector<uintptr_t> overlap;
  std::set_intersection(want.begin(),want.end(),foreign.begin(),foreign.end(),std::back_inserter(overlap));
  if(got!=want||!overlap.empty())throw std::runtime_error("coverage mismatch or non-weight cache-line sharing");
  printf("COVERAGE O=%d expected_lines=%zu touched_unique=%zu attempts=%zu foreign_lines=%zu shared_lines=%zu PASS\n",o,want.size(),got.size(),raw.used.load(),foreign.size(),overlap.size());
}
static std::vector<Region> shard_regions(void*base,int o,int shards=1){
  std::vector<Region> r; r.reserve(size_t(o)*2*shards);
  for(int s=0;s<shards;++s){auto*b=(unsigned char*)base+size_t(s)*wb(o);for(int row=0;row<o;++row){r.push_back({b+size_t(row)*288,256,RegionKind::WeightCodes,"q4_codes"});r.push_back({b+size_t(row)*288+256,32,RegionKind::WeightScales,"q4_scales"});}}
  return r;
}
static void negative_tests(void*base,size_t bytes,const Region&x,const std::vector<Region>&weights){
  bool omit=false,shift=false,reject=false;
  auto expected=lines(base,bytes);Recorder v(bytes/64+2);evict_range_record(base,bytes-64,record_line,&v);std::set<uintptr_t>a(v.v.begin(),v.v.begin()+v.used.load());omit=a!=expected;
  v.used=0;evict_range_record((char*)base+1,bytes,record_line,&v);a.clear();a.insert(v.v.begin(),v.v.begin()+v.used.load());shift=a!=expected;
  auto bad=weights;bad.push_back(x);try{evict_weights_regions(bad,8);}catch(const std::invalid_argument&){reject=true;}
  printf("NEGATIVE omit_last_line=%s shift_byte=%s include_X=%s\n",omit?"REJECTED":"FAIL",shift?"REJECTED":"FAIL",reject?"REJECTED":"FAIL");
  if(!omit||!shift||!reject)throw std::runtime_error("negative control accepted");
}
static void verify_topology(){
  DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!bytes)throw std::runtime_error("cannot query physical-core topology");
  std::vector<unsigned char>b(bytes);if(!GetLogicalProcessorInformationEx(RelationProcessorCore,(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)b.data(),&bytes))throw std::runtime_error("topology query failed");
  struct Core{std::set<unsigned> lp;unsigned eff;};std::vector<Core> cores;unsigned off=0,maxeff=0;
  while(off<bytes){auto*p=(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(b.data()+off);if(p->Relationship==RelationProcessorCore){Core c{};c.eff=p->Processor.EfficiencyClass;for(WORD g=0;g<p->Processor.GroupCount;++g)for(unsigned bit=0;bit<sizeof(KAFFINITY)*8;++bit)if(p->Processor.GroupMask[g].Mask&(KAFFINITY(1)<<bit))c.lp.insert(bit+g*64);maxeff=std::max(maxeff,c.eff);cores.push_back(c);}off+=p->Size;}
  for(unsigned lp=0;lp<16;lp+=2){unsigned found=0;for(auto&c:cores)if(c.lp.count(lp)){found++;if(c.eff!=maxeff||!c.lp.count(lp+1))throw std::runtime_error("specified LP is not an SMT pair on a maximum-efficiency P-core");}if(found!=1)throw std::runtime_error("specified P-core LP mapping is not unique");}
  printf("TOPOLOGY=PASS P_LP=0,2,4,6,8,10,12,14 max_EfficiencyClass=%u SMT_pairs_verified=8\n",maxeff);
}
static void pin(unsigned lp){GROUP_AFFINITY req{},got{};req.Group=0;req.Mask=KAFFINITY(1)<<lp;if(!SetThreadGroupAffinity(GetCurrentThread(),&req,nullptr)||!GetThreadGroupAffinity(GetCurrentThread(),&got)||got.Group!=0||got.Mask!=req.Mask)throw std::runtime_error("thread affinity could not be set/read back");PROCESSOR_NUMBER pn{};GetCurrentProcessorNumberEx(&pn);if(pn.Group!=0||pn.Number!=lp)throw std::runtime_error("current processor does not match verified LP");}
static uint32_t rng(uint32_t&x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
struct Chase { Mem mem; std::vector<uint32_t> next; explicit Chase():mem(8u*1024u*1024u),next(mem.n/64){
  std::vector<uint32_t> p(next.size());std::iota(p.begin(),p.end(),0);uint32_t s=0x53493139u;
  for(size_t i=p.size()-1;i;i--)std::swap(p[i],p[rng(s)%(i+1)]);
  for(size_t i=0;i<p.size();i++)next[p[i]]=p[(i+1)%p.size()];
  auto*base=(uint32_t*)mem.r.data;for(size_t i=0;i<next.size();i++)*reinterpret_cast<uint32_t*>((char*)base+i*64)=next[i];
 }};
static double walk(Chase&c){LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);volatile uint32_t ix=0;for(size_t i=0;i<c.next.size();++i)ix=*reinterpret_cast<volatile uint32_t*>((char*)c.mem.r.data+size_t(ix)*64);QueryPerformanceCounter(&b);(void)ix;return double(b.QuadPart-a.QuadPart)/f.QuadPart;}
static double med(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static uint64_t qpc(){LARGE_INTEGER x;QueryPerformanceCounter(&x);return(uint64_t)x.QuadPart;}
static uint64_t utcft(){FILETIME f{};GetSystemTimePreciseAsFileTime(&f);return(uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
static void phase(FILE*f,unsigned core,int rep,const char*condition,uint64_t q0,uint64_t u0,uint64_t q1,uint64_t u1){std::fprintf(f,"%u,%d,%s,%llu,%llu,%llu,%llu\n",core,rep,condition,(unsigned long long)q0,(unsigned long long)q1,(unsigned long long)u0,(unsigned long long)u1);std::fflush(f);}
static void timed_condition(Chase&c,unsigned core,const char*label,bool cold,FlushKind kind,FILE*marks,std::vector<double>&samples){
  const double target=2.5/31.0;uint64_t q0=qpc(),u0=utcft();
  for(int rep=0;rep<31;++rep){double sum=0;unsigned count=0;do{if(cold){if(kind==selected_flush().kind)evict_range(c.mem.r.data,c.mem.n);else evict_range_diagnostic(c.mem.r.data,c.mem.n,kind);}sum+=walk(c);++count;}while(sum<target);samples.push_back(sum/count);}
  uint64_t q1=qpc(),u1=utcft();phase(marks,core,-1,label,q0,u0,q1,u1);
}
int main(){try{
  std::string lp; bool priv=t0common::enable_large_page_privilege(&lp);printf("LARGE_PAGE_PRIVILEGE=%s\n",priv?"AVAILABLE":lp.c_str());if(!priv)throw std::runtime_error("SeLockMemoryPrivilege unavailable in current process");printf("LARGE_PAGE_MIN_BYTES=%llu\n",(unsigned long long)GetLargePageMinimum());
  verify_topology();auto choice=selected_flush();printf("FLUSH_SELECTION=%s CPUID.07H:EBX[23] frozen\n",choice.name);
  FILE*marks=std::fopen("eviction_windows.csv","wt");if(!marks)throw std::runtime_error("cannot create eviction_windows.csv");std::fprintf(marks,"Pcore,replica,condition,qpc_start,qpc_end,utc_filetime_start,utc_filetime_end\n");
  const int outs[3]={912,1024,58256};
  for(int o:outs){Mem w(wb(o));auto r=shard_regions(w.r.data,o);Mem x1(672),x16(672*16),y1(4096),y16(4096*16),prep1(672),prep16(672*16),sums1(64),sums16(64*16),scratch(65536);std::vector<Region> non={{x1.r.data,x1.n,RegionKind::X,"X_m1"},{x16.r.data,x16.n,RegionKind::X,"X_m16"},{y1.r.data,y1.n,RegionKind::Y,"Y_m1"},{y16.r.data,y16.n,RegionKind::Y,"Y_m16"},{prep1.r.data,prep1.n,RegionKind::PreparedActivations,"prepared_m1"},{prep16.r.data,prep16.n,RegionKind::PreparedActivations,"prepared_m16"},{sums1.r.data,sums1.n,RegionKind::CorrectiveSums,"sums_m1"},{sums16.r.data,sums16.n,RegionKind::CorrectiveSums,"sums_m16"},{scratch.r.data,scratch.n,RegionKind::Scratch,"scratch"}};check_map(o,w,r,non);if(o==1024)negative_tests(w.r.data,w.n,non[0],r);}
  // Independent cache control uses only DUMMY pointer chasing, no Q4/ggml/Torch kernel.
  for(unsigned core=0;core<8;++core){pin(core*2);Chase c;std::vector<double> cs,hs,cd,hd,rs,rd;(void)walk(c);timed_condition(c,core,"cold_selected",true,choice.kind,marks,cs);(void)walk(c);timed_condition(c,core,"hot_selected",false,choice.kind,marks,hs);timed_condition(c,core,"cold_clflush_diag",true,FlushKind::CLFLUSH,marks,cd);(void)walk(c);timed_condition(c,core,"hot_clflush_diag",false,FlushKind::CLFLUSH,marks,hd);for(int i=0;i<31;++i){rs.push_back(cs[i]/hs[i]);rd.push_back(cd[i]/hd[i]);}printf("P%u LP%u N=31 selected=%s t_cold=%.9g t_hot=%.9g ratio=%.6f CLFLUSH_diag_t_cold=%.9g t_hot=%.9g ratio=%.6f LLC_events=AWAITING_EMON_WINDOW_BINDING\n",core,core*2,choice.name,med(cs),med(hs),med(rs),med(cd),med(hd),med(rd));}
  // Architectural repeat occurs only after the independent victim-buffer validation.
  constexpr int O=1024;Mem all(wb(O)*8);auto ar=shard_regions(all.r.data,O,8);std::set<uintptr_t> archWant;size_t archAttempts=0;for(auto&r:ar){auto q=lines(r.ptr,r.bytes);archWant.insert(q.begin(),q.end());archAttempts+=size_t((((uintptr_t)r.ptr+r.bytes-1&~uintptr_t(63))-((uintptr_t)r.ptr&~uintptr_t(63)))/64)+1;}Recorder archRaw(archAttempts);evict_weights_regions_record(ar,8,record_line,&archRaw);std::set<uintptr_t> archGot(archRaw.v.begin(),archRaw.v.begin()+archRaw.used.load());if(archGot!=archWant)throw std::runtime_error("architectural eight-shard coverage mismatch");printf("ARCH_COVERAGE expected_lines=%zu touched_unique=%zu attempts=%zu nonweight_lines=0 PASS\n",archWant.size(),archGot.size(),archRaw.used.load());volatile uint64_t ctl=0;for(size_t i=0;i<all.n;i+=64)ctl+=*((volatile uint8_t*)all.r.data+i);printf("ARCH_CONTROL_READ bytes=%zu sink=%llu\n",all.n,(unsigned long long)ctl);evict_weights_regions(ar,8);printf("ARCH_EVICTION shards=8 workers=8 bytes=%zu barrier=PASS\n",all.n);ctl=0;for(size_t i=0;i<all.n;i+=64)ctl+=*((volatile uint8_t*)all.r.data+i);evict_weights_regions_diagnostic(ar,8,FlushKind::CLFLUSH);printf("ARCH_EVICTION_DIAGNOSTIC method=CLFLUSH shards=8 workers=8 control_sink=%llu\n",(unsigned long long)ctl);
  std::fclose(marks);printf("LARGE_PAGE_ALLOCATIONS_CHECKED=%zu address_alignment=PASS materialized=DUMMY\n",gLargeAllocations.load());printf("EXECUTION=PASS QA=PASS CACHE_QUALIFICATION=AWAITING_EMON_ANALYSIS\n");return 0;
 }catch(const std::exception&e){fprintf(stderr,"EXECUTION=HOLD cause=%s\n",e.what());return 2;}}
