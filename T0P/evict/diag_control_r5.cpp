#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <intrin.h>
static constexpr unsigned N=8; static constexpr size_t B=294912;
static LARGE_INTEGER fq; static uint64_t qpc(){LARGE_INTEGER x{};QueryPerformanceCounter(&x);return uint64_t(x.QuadPart);}
static void pin(unsigned w){GROUP_AFFINITY a{};a.Group=0;a.Mask=KAFFINITY(1u<<(2*w));if(!SetThreadGroupAffinity(GetCurrentThread(),&a,nullptr)||GetCurrentProcessorNumber()!=2*w)throw 1;}
static void barrier(std::atomic<unsigned>&c,std::atomic<unsigned>&g){auto x=g.load(std::memory_order_acquire);if(c.fetch_add(1,std::memory_order_acq_rel)==N-1){c.store(0,std::memory_order_relaxed);g.fetch_add(1,std::memory_order_release);}else while(g.load(std::memory_order_acquire)==x)_mm_pause();}
int main(int argc,char**argv){
 if(argc!=2)return 2;std::string mode=argv[1];if(mode!="weights"&&mode!="nonweights"&&mode!="idle")return 2;QueryPerformanceFrequency(&fq);
 std::array<std::vector<unsigned char>,N>w,x;std::array<std::vector<float>,N>y;for(unsigned i=0;i<N;i++){w[i].resize(B);x[i].resize(640);y[i].resize(1024);for(size_t j=0;j<B;j+=64)w[i][j]=unsigned char(j/64+i);for(size_t j=0;j<x[i].size();j+=64)x[i][j]=unsigned char(j/64+i);}
 std::atomic<unsigned>ready{0},bc{0},bg{0};std::atomic<int>phase{0};std::array<uint64_t,N>sinks{};uint64_t start=qpc()+uint64_t(fq.QuadPart),end=start+uint64_t(fq.QuadPart*2.5),finish=end+uint64_t(fq.QuadPart*0.2);
 auto work=[&](unsigned i){if(mode=="weights"){uint64_t z=0;auto*p=(volatile unsigned char*)w[i].data();for(size_t j=0;j<B;j+=64)z+=p[j];sinks[i]^=z;}else if(mode=="nonweights"){uint64_t z=0;auto*p=(volatile unsigned char*)x[i].data();for(size_t j=0;j<x[i].size();j+=64)z+=p[j];std::fill(y[i].begin(),y[i].end(),0.5f);_mm_mfence();sinks[i]^=z;}else Sleep(1);};
 std::array<std::thread,N>ts;
 for(unsigned i=0;i<N;i++)ts[i]=std::thread([&,i]{try{pin(i);ready.fetch_add(1,std::memory_order_release);barrier(bc,bg);while(qpc()<start-uint64_t(fq.QuadPart*0.2))Sleep(1);while(qpc()<start)work(i);while(phase.load(std::memory_order_acquire)==0)Sleep(0);while(phase.load(std::memory_order_acquire)==1)work(i);while(phase.load(std::memory_order_acquire)==2)work(i);
 }catch(...){sinks[i]=UINT64_MAX;}});
 while(ready.load(std::memory_order_acquire)<N)Sleep(1);while(qpc()<start)Sleep(0);phase.store(1,std::memory_order_release);while(qpc()<end)Sleep(0);phase.store(2,std::memory_order_release);while(qpc()<finish)Sleep(0);phase.store(3,std::memory_order_release);for(auto&t:ts)t.join();uint64_t sink=0;for(auto s:sinks)sink^=s;std::cout<<"CONTROL="<<mode<<" workers=8 LP=0,2,4,6,8,10,12,14 shards=8 shard_bytes=294912 x_bytes=640 y_bytes=4096 sync=shared_phase_poll_local_sink qpc_pad_start="<<start-uint64_t(fq.QuadPart*0.2)<<" qpc_start="<<start<<" qpc_end="<<end<<" qpc_pad_end="<<finish<<" measured_s=2.500 margins_s=0.200 sink="<<sink<<"\n";return sink==UINT64_MAX?2:0;
}
