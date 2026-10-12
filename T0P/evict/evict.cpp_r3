#define NOMINMAX
#include "evict.hpp"
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <limits>

namespace omega_evict {
namespace {
struct Gate {
  std::mutex m; std::condition_variable cv; unsigned arrived=0, generation=0, count;
  explicit Gate(unsigned n):count(n){}
  void wait(){std::unique_lock<std::mutex> l(m);unsigned g=generation;if(++arrived==count){arrived=0;++generation;cv.notify_all();}else cv.wait(l,[&]{return generation!=g;});}
};
std::uintptr_t first_line(const void* p, std::size_t n) {
  if (!p || !n) throw std::invalid_argument("evict range requires non-null pointer and positive bytes");
  auto a=reinterpret_cast<std::uintptr_t>(p);
  if (n-1 > std::numeric_limits<std::uintptr_t>::max()-a) throw std::overflow_error("range end overflow");
  return a & ~std::uintptr_t(kLineBytes-1);
}
std::size_t line_count(const void* p,std::size_t n) {
  auto f=first_line(p,n); auto last=(reinterpret_cast<std::uintptr_t>(p)+n-1)&~std::uintptr_t(63);
  return std::size_t((last-f)/64)+1;
}
void flush_line(std::uintptr_t a, FlushKind k) {
  if(k==FlushKind::CLFLUSHOPT) _mm_clflushopt(reinterpret_cast<const void*>(a));
  else _mm_clflush(reinterpret_cast<const void*>(a));
}
void validate(const std::vector<Region>& rs) {
  if(rs.empty()) throw std::invalid_argument("empty weight region list");
  for(const auto&r:rs) {
    if(r.kind!=RegionKind::WeightCodes && r.kind!=RegionKind::WeightScales)
      throw std::invalid_argument(std::string("non-weight region rejected: ")+(r.name?r.name:kind_name(r.kind)));
    (void)first_line(r.ptr,r.bytes);
  }
}
void parallel(const std::vector<Region>&rs,unsigned nw,LineSink sink,void*ctx,FlushKind force) {
  validate(rs); if(nw!=8) throw std::invalid_argument("worker count is fixed at 8");
  std::vector<std::size_t> offsets(rs.size()+1,0);
  for(size_t i=0;i<rs.size();++i) offsets[i+1]=offsets[i]+line_count(rs[i].ptr,rs[i].bytes);
  Gate gate(nw); std::vector<std::thread> ts; ts.reserve(nw); std::atomic<bool> affinity_ok{true};
  for(unsigned w=0;w<nw;++w) ts.emplace_back([&,w]{
    GROUP_AFFINITY req{};req.Group=0;req.Mask=KAFFINITY(1)<<(w*2);GROUP_AFFINITY observed{};
    if(!SetThreadGroupAffinity(GetCurrentThread(),&req,nullptr)||!GetThreadGroupAffinity(GetCurrentThread(),&observed)||observed.Group!=0||observed.Mask!=req.Mask||GetCurrentProcessorNumber()!=w*2)affinity_ok=false;
    gate.wait(); if(!affinity_ok.load()){gate.wait();return;}
    for(size_t g=w;g<offsets.back();g+=nw) {
      auto it=std::upper_bound(offsets.begin(),offsets.end(),g)-offsets.begin()-1;
      const auto&r=rs[it]; auto idx=g-offsets[it];
      auto a=first_line(r.ptr,r.bytes)+idx*64;
      flush_line(a,force); if(sink)sink(a,ctx);
    }
    _mm_sfence(); gate.wait();
  });
  for(auto&t:ts)t.join(); if(!affinity_ok.load())throw std::runtime_error("worker affinity verification failed"); _mm_mfence();
}
}
Choice selected_flush() noexcept {
  static const Choice c=[] { int r[4]{}; __cpuidex(r,7,0); return (unsigned(r[1])&(1u<<23))?Choice{FlushKind::CLFLUSHOPT,"CLFLUSHOPT"}:Choice{FlushKind::CLFLUSH,"CLFLUSH"}; }();
  return c;
}
void evict_range_record(const void*p,std::size_t n,LineSink sink,void*ctx) {
  auto a=first_line(p,n); auto count=line_count(p,n); auto k=selected_flush().kind;
  for(size_t i=0;i<count;++i){auto x=a+i*64;flush_line(x,k);if(sink)sink(x,ctx);} _mm_sfence();_mm_mfence();
}
void evict_range(const void*p,std::size_t n){evict_range_record(p,n,nullptr,nullptr);}
void evict_range_diagnostic(const void*p,std::size_t n,FlushKind kind){auto a=first_line(p,n);auto count=line_count(p,n);for(size_t i=0;i<count;++i)flush_line(a+i*64,kind);_mm_sfence();_mm_mfence();}
void evict_weights_regions_record(const std::vector<Region>&r,unsigned n,LineSink s,void*c){parallel(r,n,s,c,selected_flush().kind);}
void evict_weights_regions(const std::vector<Region>&r,unsigned n){parallel(r,n,nullptr,nullptr,selected_flush().kind);}
void evict_weights_regions_diagnostic(const std::vector<Region>&r,unsigned n,FlushKind k){parallel(r,n,nullptr,nullptr,k);}
const char* kind_name(RegionKind k) noexcept {switch(k){case RegionKind::WeightCodes:return"codes";case RegionKind::WeightScales:return"scales";case RegionKind::X:return"X";case RegionKind::Y:return"Y";case RegionKind::PreparedActivations:return"prepared_activations";case RegionKind::CorrectiveSums:return"sums";case RegionKind::Scratch:return"scratch";case RegionKind::Output:return"output";}return"unknown";}
}
