#define NOMINMAX
#include "ggml.h"
#include "ggml-cpu.h"
#include "repack.h"
#include "common.hpp"

#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
extern "C" { float ggml_table_f32_f16[1 << 16] = {}; float ggml_table_f32_e8m0_half[1 << 8] = {}; }
namespace {
constexpr int O=t0common::O, I=t0common::I, G=t0common::G;
struct Q4 { uint16_t d[8]; uint8_t qs[128]; };
struct Q8x4 { uint16_t d[4]; int8_t qs[128]; };
static_assert(sizeof(Q4)==144 && sizeof(Q8x4)==136);
using repack_fn = void (*)(int,float*,size_t,const void*,const void*,int,int);

std::string sha256(const uint8_t* data,size_t size) {
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; DWORD objlen=0,got=0,hlen=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("BCryptOpenAlgorithmProvider");
    BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objlen,sizeof(objlen),&got,0);
    BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&hlen,sizeof(hlen),&got,0);
    std::vector<uint8_t> obj(objlen), out(hlen);
    if(BCryptCreateHash(alg,&hash,obj.data(),objlen,nullptr,0,0)<0 || BCryptHashData(hash,(PUCHAR)data,(ULONG)size,0)<0 || BCryptFinishHash(hash,out.data(),hlen,0)<0) throw std::runtime_error("BCrypt SHA256");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    static const char* hex="0123456789abcdef"; std::string s; s.reserve(hlen*2);
    for(auto b:out){s+=hex[b>>4];s+=hex[b&15];} return s;
}
std::string sha_file(const fs::path& p) { std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("no se pudo leer hash: "+p.string()); std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{}); return sha256(b.data(),b.size()); }
std::string hx(uintptr_t p){std::ostringstream s;s<<"0x"<<std::hex<<p;return s.str();}

struct Prepared {
  t0common::Cell c; std::vector<uint8_t> flatw,flata,packw,packa,work; std::vector<float> out; ggml_context* ctx=nullptr; ggml_tensor* input=nullptr; ggml_tensor* result=nullptr; ggml_cgraph* graph=nullptr; ggml_threadpool* threadpool=nullptr; ggml_cplan plan{};
  Prepared()=default; Prepared(const Prepared&)=delete; Prepared& operator=(const Prepared&)=delete;
  Prepared(Prepared&& o) noexcept : c(std::move(o.c)),flatw(std::move(o.flatw)),flata(std::move(o.flata)),packw(std::move(o.packw)),packa(std::move(o.packa)),work(std::move(o.work)),out(std::move(o.out)),ctx(o.ctx),input(o.input),result(o.result),graph(o.graph),threadpool(o.threadpool),plan(o.plan){o.ctx=nullptr;o.threadpool=nullptr;}
  ~Prepared(){if(threadpool)ggml_threadpool_free(threadpool);if(ctx)ggml_free(ctx);}
};
void prepare_weights(const t0common::Cell& c, Prepared& p) {
    p.flatw=t0common::ggml_q4_0(c);
    // Upstream block_q4_0x8 transform: preserve half scales, interleave eight rows,
    // XOR every packed byte with 0x88. Inverse below proves all codes and half bits.
    p.packw.resize(size_t(O/8)*G*sizeof(Q4));
    for(int ot=0;ot<O/8;++ot) for(int g=0;g<G;++g){
      auto* d=reinterpret_cast<Q4*>(p.packw.data())+size_t(ot)*G+g;
      for(int j=0;j<8;++j){int o=ot*8+j;size_t src=(size_t(o)*G+g)*18;d->d[j]=uint16_t(p.flatw[src]|(uint16_t(p.flatw[src+1])<<8));}
      for(int i=0;i<16;++i){int row=i%8,off=(i/8)*8;uint64_t v;std::memcpy(&v,p.flatw.data()+(size_t(ot*8+row)*G+g)*18+2+off,8);v^=0x8888888888888888ULL;std::memcpy(d->qs+i*8,&v,8);}
    }
}
// Dynamic per-application preparation. Charge this plus run_once for the dynamic boundary.
void prepare_activations(Prepared& p) {
    const auto& c=p.c;p.flata=t0common::ggml_q8_0(c);
    p.input->data=p.flata.data();
    if(c.M==1) return; // GEMV consumes the original single Q8_0 row directly.
    p.packa.resize(size_t(c.M/4)*G*sizeof(Q8x4));
    for(int rt=0;rt<c.M/4;++rt) for(int g=0;g<G;++g){
      auto* d=reinterpret_cast<Q8x4*>(p.packa.data())+size_t(rt)*G+g;
      for(int r=0;r<4;++r){size_t src=(size_t(rt*4+r)*G+g)*34;d->d[r]=uint16_t(p.flata[src]|(uint16_t(p.flata[src+1])<<8));}
      for(int i=0;i<16;++i){int row=i%4,off=(i/4)*8;size_t src=(size_t(rt*4+row)*G+g)*34+2+off;std::memcpy(d->qs+i*8,p.flata.data()+src,8);}
    }
    // Reconstruct source blocks through the inverse interleave and assert bit identity.
    for(int ot=0;ot<O/8;++ot)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q4*>(p.packw.data())+size_t(ot)*G+g;for(int j=0;j<8;++j){size_t src=(size_t(ot*8+j)*G+g)*18;if(d->d[j]!=(uint16_t(p.flatw[src]|(uint16_t(p.flatw[src+1])<<8))) )throw std::runtime_error("Q4 repack scale mismatch");}
      for(int i=0;i<16;++i){int row=i%8,off=(i/8)*8;uint64_t v;std::memcpy(&v,d->qs+i*8,8);v^=0x8888888888888888ULL;uint64_t src;std::memcpy(&src,p.flatw.data()+(size_t(ot*8+row)*G+g)*18+2+off,8);if(v!=src)throw std::runtime_error("Q4 repack code mismatch");}}
    for(int rt=0;rt<c.M/4;++rt)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q8x4*>(p.packa.data())+size_t(rt)*G+g;for(int r=0;r<4;++r){size_t src=(size_t(rt*4+r)*G+g)*34;if(d->d[r]!=(uint16_t(p.flata[src]|(uint16_t(p.flata[src+1])<<8)))||std::memcmp(d->qs+r*0,p.flata.data()+src+2,0)!=0)throw std::runtime_error("Q8 repack scale mismatch");}
      for(int i=0;i<16;++i){int row=i%4,off=(i/4)*8;size_t src=(size_t(rt*4+row)*G+g)*34+2+off;if(std::memcmp(d->qs+i*8,p.flata.data()+src,8))throw std::runtime_error("Q8 repack int8 mismatch");}}
}
Prepared prepare(const fs::path& data,int m,t0common::Fixture f) {
    Prepared p; auto name=std::string("m")+std::to_string(m)+"_"+t0common::fixture_name(f)+".bin"; p.c=t0common::load_cell(data/name);
    const auto generated=t0common::generate(m,f); if(p.c.sha256!=generated.sha256)throw std::runtime_error("hash canónico no coincide con generador: "+name);
    if(sha_file(data/name)!=p.c.sha256)throw std::runtime_error("hash del archivo no coincide: "+name);
    prepare_weights(p.c,p); p.out.resize(size_t(m)*O);
    ggml_init_params ip{32*1024*1024,nullptr,false};p.ctx=ggml_init(ip);if(!p.ctx)throw std::runtime_error("ggml_init");
    auto*w=ggml_new_tensor_2d(p.ctx,GGML_TYPE_Q4_0,I,O);auto*x=ggml_new_tensor_2d(p.ctx,GGML_TYPE_Q8_0,I,m);w->data=p.flatw.data();p.input=x;p.result=ggml_mul_mat(p.ctx,w,x);p.graph=ggml_new_graph(p.ctx);ggml_build_forward_expand(p.graph,p.result);
    auto tp=ggml_threadpool_params_default(1);p.threadpool=ggml_threadpool_new(&tp);if(!p.threadpool)throw std::runtime_error("threadpool create");p.plan=ggml_graph_plan(p.graph,1,p.threadpool);p.work.resize(p.plan.work_size);p.plan.work_data=p.work.empty()?nullptr:p.work.data();
    return p;
}
// run_once uses preallocated graph, buffers and output; it performs no adapter allocation.
void run_once_flat(Prepared& p){if(ggml_graph_compute(p.graph,&p.plan)!=GGML_STATUS_SUCCESS)throw std::runtime_error("ggml graph compute");}
void run_once_repack(Prepared& p,repack_fn fn){std::fill(p.out.begin(),p.out.end(),NAN);const void* act=p.c.M==1?(const void*)p.flata.data():(const void*)p.packa.data();fn(I,p.out.data(),O,p.packw.data(),act,p.c.M,O);}
struct Err {double abs=0,rel=0,norm=0;bool finite=true,pass=true;size_t bad=0;};
Err assess(const t0common::Cell&c,const float* y,size_t n){Err e;auto ref=t0common::reference(c);if(n!=ref.size())throw std::runtime_error("output size mismatch");for(size_t ix=0;ix<n;++ix){int m=int(ix/O),o=int(ix%O);double terms=0;for(int g=0;g<G;++g){if(!std::isfinite(c.ws[o*G+g])||!std::isfinite(c.xs[m*G+g]))e.finite=false;double scale=std::abs(double(c.ws[o*G+g])*double(c.xs[m*G+g]));for(int k=0;k<32;++k)terms+=scale*std::abs(double(c.wq[o*I+g*32+k])*double(c.xq[m*I+g*32+k]));}
 double a=y[ix],b=ref[ix],d=std::abs(a-b);if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d)){e.finite=false;e.pass=false;++e.bad;continue;}e.abs=std::max(e.abs,d);if(b!=0)e.rel=std::max(e.rel,d/std::abs(b));if(terms!=0)e.norm=std::max(e.norm,d/terms);else if(d!=0)e.norm=std::numeric_limits<double>::infinity();double tol=1e-5+1e-5*std::abs(b);if(d>tol){e.pass=false;++e.bad;}}
 if(!e.finite)e.pass=false;return e;}
void inventory(){HMODULE mods[512];DWORD needed=0;if(!EnumProcessModules(GetCurrentProcess(),mods,sizeof(mods),&needed))throw std::runtime_error("EnumProcessModules");std::cout<<"DLL_INVENTORY_BEGIN\n";for(unsigned i=0;i<needed/sizeof(HMODULE);++i){wchar_t path[32768];DWORD n=GetModuleFileNameExW(GetCurrentProcess(),mods[i],path,32768);if(!n)continue;MODULEINFO mi{};GetModuleInformation(GetCurrentProcess(),mods[i],&mi,sizeof(mi));fs::path p(path);std::cout<<"DLL "<<p.string()<<" size="<<mi.SizeOfImage<<" sha256="<<sha_file(p)<<"\n";}std::cout<<"DLL_INVENTORY_END\n";}
}

int main(int argc,char**argv){try{
 const char* omp=getenv("OMP_NUM_THREADS"),*dyn=getenv("OMP_DYNAMIC");if(!omp||std::string(omp)!="1"||!dyn||_stricmp(dyn,"FALSE")!=0)throw std::runtime_error("Se requiere OMP_NUM_THREADS=1 y OMP_DYNAMIC=FALSE antes de cargar ggml");
 if(argc!=2)throw std::runtime_error("uso: adapter.exe <ruta T0P/common/data>");fs::path data=fs::absolute(argv[1]);
 for(unsigned i=0;i<(1u<<16);++i)ggml_table_f32_f16[i]=ggml_fp16_to_fp32((ggml_fp16_t)i);
 HMODULE cpu=GetModuleHandleW(L"ggml-cpu.dll");if(!cpu)throw std::runtime_error("ggml-cpu.dll no cargada");
 auto gemv=&ggml_gemv_q4_0_8x8_q8_0;auto gemm=&ggml_gemm_q4_0_8x8_q8_0;
 auto traits=ggml_get_type_traits_cpu(GGML_TYPE_Q4_0);auto dot=traits?reinterpret_cast<void*>(traits->vec_dot):nullptr;MODULEINFO cmi{};GetModuleInformation(GetCurrentProcess(),cpu,&cmi,sizeof(cmi));
 std::cout<<"ENV OMP_NUM_THREADS="<<omp<<" OMP_DYNAMIC="<<dyn<<" n_threads=1 trait_vec_dot="<<hx((uintptr_t)dot)<<" trait_offset="<<hx((uintptr_t)dot-(uintptr_t)cmi.lpBaseOfDll)<<" ggml_cpu_base="<<hx((uintptr_t)cmi.lpBaseOfDll)<<" repack_gemv="<<hx((uintptr_t)gemv)<<" repack_gemm="<<hx((uintptr_t)gemm)<<" flat_evidence=Q4_0_cpu_trait_callback_address_from_loaded_ggml-cpu.dll; symbol export/PDB absent\n";
 if(!gemv||!gemm||!dot)throw std::runtime_error("evidencia/símbolos de despacho incompletos");
 t0common::verifier_unit_tests();const t0common::Fixture fs[]={t0common::Fixture::Uniform,t0common::Fixture::MixedCancellation,t0common::Fixture::VariedScales,t0common::Fixture::Sub127,t0common::Fixture::Extremes};
 for(auto f:fs)for(int m:{1,4,8,16}){auto p=prepare(data,m,f);prepare_activations(p);run_once_flat(p);auto ef=assess(p.c,(float*)p.result->data,p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=flat executed=ggml_mul_mat trait_callback="<<hx((uintptr_t)dot)<<" finite="<<ef.finite<<" tolerance_pass="<<ef.pass<<" abs="<<std::setprecision(12)<<ef.abs<<" rel="<<ef.rel<<" normalized="<<ef.norm<<" bad="<<ef.bad<<" hash="<<p.c.sha256<<"\n";
 auto fn=m==1?gemv:gemm;run_once_repack(p,fn);auto er=assess(p.c,p.out.data(),p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=repack executed="<<(m==1?"ggml_gemv_q4_0_8x8_q8_0":"ggml_gemm_q4_0_8x8_q8_0")<<" address="<<hx((uintptr_t)fn)<<" input_pack=lossless finite="<<er.finite<<" tolerance_pass="<<er.pass<<" abs="<<er.abs<<" rel="<<er.rel<<" normalized="<<er.norm<<" bad="<<er.bad<<" hash="<<p.c.sha256<<"\n";}
 inventory();return 0;
 }catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 2;}}
