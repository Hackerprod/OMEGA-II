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
bool storage_controls(const Prepared& p){
 auto flat_ok=t0common::exact_representation(p.c,[&](auto&w,auto&ws,auto&x,auto&xs){w.resize(O*I);ws.resize(O*G);x.resize(p.c.M*I);xs.resize(p.c.M*G);for(int o=0;o<O;++o)for(int g=0;g<G;++g){size_t b=(size_t(o)*G+g)*18;ws[o*G+g]=uint16_t(p.flatw[b]|(uint16_t(p.flatw[b+1])<<8));for(int j=0;j<16;++j){int lo=(p.flatw[b+2+j]&15)-8,hi=(p.flatw[b+2+j]>>4)-8;w[o*I+g*32+j]=int8_t(lo);w[o*I+g*32+j+16]=int8_t(hi);}}for(int m=0;m<p.c.M;++m)for(int g=0;g<G;++g){size_t b=(size_t(m)*G+g)*34;xs[m*G+g]=uint16_t(p.flata[b]|(uint16_t(p.flata[b+1])<<8));for(int k=0;k<32;++k)x[m*I+g*32+k]=int8_t(p.flata[b+2+k]);}return true;});
 auto repack_ok=t0common::exact_representation(p.c,[&](auto&w,auto&ws,auto&x,auto&xs){w.resize(O*I);ws.resize(O*G);x.resize(p.c.M*I);xs.resize(p.c.M*G);for(int ot=0;ot<O/8;++ot)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q4*>(p.packw.data())+size_t(ot)*G+g;for(int j=0;j<8;++j)ws[(ot*8+j)*G+g]=d->d[j];for(int i=0;i<16;++i){int row=i%8,off=(i/8)*8;uint64_t v;std::memcpy(&v,d->qs+i*8,8);v^=0x8888888888888888ULL;for(int b=0;b<8;++b){uint8_t q=uint8_t(v>>(8*b));w[(ot*8+row)*I+g*32+off+b]=int8_t((q&15)-8);w[(ot*8+row)*I+g*32+off+b+16]=int8_t((q>>4)-8);}}}if(p.c.M==1){for(int m=0;m<p.c.M;++m)for(int g=0;g<G;++g){size_t b=(size_t(m)*G+g)*34;xs[m*G+g]=uint16_t(p.flata[b]|(uint16_t(p.flata[b+1])<<8));for(int k=0;k<32;++k)x[m*I+g*32+k]=int8_t(p.flata[b+2+k]);}}else for(int rt=0;rt<p.c.M/4;++rt)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q8x4*>(p.packa.data())+size_t(rt)*G+g;for(int r=0;r<4;++r){xs[(rt*4+r)*G+g]=d->d[r];for(int k=0;k<32;++k)x[(rt*4+r)*I+g*32+k]=d->qs[((k/8)*4+r)*8+(k%8)];}}return true;});return flat_ok&&repack_ok;
}
Prepared prepare(const fs::path& data,int m,t0common::Fixture f) {
    Prepared p; auto name=std::string("m")+std::to_string(m)+"_"+t0common::fixture_name(f)+".bin"; p.c=t0common::load_cell(data/name);
    const auto generated=t0common::generate(m,f); if(p.c.sha256!=generated.sha256)throw std::runtime_error("hash canÃ³nico no coincide con generador: "+name);
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
struct Err {double abs=0,rel=0,norm=0,error_over_B=0;bool finite=true,pass=true,old_pass=true,exact_single=true;size_t bad=0,old_bad=0;std::vector<t0common::Failure> failures;};
Err assess(const t0common::Cell&c,const float* y,size_t n){Err e;auto ref=t0common::reference64(c);if(n!=ref.r64.size())throw std::runtime_error("output size mismatch");std::vector<float> out(y,y+n);auto v=t0common::verify_s12(c,out);e.pass=v.pass;e.bad=v.failures;e.failures=v.failed;e.error_over_B=v.max_error_over_B;auto old=t0common::verify(c,out);e.old_pass=old.pass;e.old_bad=old.failures;if(c.fixture==t0common::Fixture::SingleCoordinate)for(size_t i=0;i<n;++i)if(double(y[i])!=ref.r64[i]){e.pass=false;e.exact_single=false;++e.bad;}
 for(size_t ix=0;ix<n;++ix){double a=y[ix],b=ref.r64[ix],d=std::abs(a-b);if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d)){e.finite=false;continue;}e.abs=std::max(e.abs,d);if(b!=0)e.rel=std::max(e.rel,d/std::abs(b));double terms=ref.S[ix];if(terms!=0)e.norm=std::max(e.norm,d/terms);}if(!e.finite)e.pass=false;return e;}
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
 if(!gemv||!gemm||!dot)throw std::runtime_error("evidencia/sÃ­mbolos de despacho incompletos");
 t0common::verifier_unit_tests();const t0common::Fixture fs[]={t0common::Fixture::Uniform,t0common::Fixture::MixedCancellation,t0common::Fixture::VariedScales,t0common::Fixture::Sub127,t0common::Fixture::Extremes,t0common::Fixture::ZeroInput,t0common::Fixture::SingleCoordinate,t0common::Fixture::CancellationHigh,t0common::Fixture::CancellationExtreme};
 for(auto f:fs)for(int m:{1,4,8,16}){auto p=prepare(data,m,f);prepare_activations(p);bool repr=storage_controls(p);std::cout<<"control_representation flat_and_effective_repack="<<(repr?"PASS":"FAIL")<<"\n";if(!repr)throw std::runtime_error("effective-storage representation mismatch");run_once_flat(p);auto ef=assess(p.c,(float*)p.result->data,p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=flat executed=ggml_mul_mat trait_callback="<<hx((uintptr_t)dot)<<" finite="<<ef.finite<<" S12_pass="<<ef.pass<<" S12_failures="<<ef.bad<<" single_coordinate_exact="<<ef.exact_single<<" max_error_over_B="<<std::setprecision(12)<<ef.error_over_B<<" old_tolerance_pass="<<ef.old_pass<<" old_tolerance_failures="<<ef.old_bad<<" abs="<<ef.abs<<" rel="<<ef.rel<<" normalized="<<ef.norm<<" hash="<<p.c.sha256<<"\n";for(const auto& f:ef.failures)std::cout<<"S12_FAIL route=flat m="<<f.m<<" o="<<f.o<<" r64="<<f.r64<<" y="<<f.y<<" abs="<<f.error<<" S="<<f.S<<" B="<<f.B<<" error_over_B="<<f.error_over_B<<" S_over_abs_r="<<f.S_over_abs_r<<"\n";
 auto fn=m==1?gemv:gemm;run_once_repack(p,fn);auto er=assess(p.c,p.out.data(),p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=repack executed="<<(m==1?"ggml_gemv_q4_0_8x8_q8_0":"ggml_gemm_q4_0_8x8_q8_0")<<" address="<<hx((uintptr_t)fn)<<" input_pack=lossless finite="<<er.finite<<" S12_pass="<<er.pass<<" S12_failures="<<er.bad<<" single_coordinate_exact="<<er.exact_single<<" max_error_over_B="<<er.error_over_B<<" old_tolerance_pass="<<er.old_pass<<" old_tolerance_failures="<<er.old_bad<<" abs="<<er.abs<<" rel="<<er.rel<<" normalized="<<er.norm<<" hash="<<p.c.sha256<<"\n";for(const auto& f:er.failures)std::cout<<"S12_FAIL route=repack m="<<f.m<<" o="<<f.o<<" r64="<<f.r64<<" y="<<f.y<<" abs="<<f.error<<" S="<<f.S<<" B="<<f.B<<" error_over_B="<<f.error_over_B<<" S_over_abs_r="<<f.S_over_abs_r<<"\n";}
 for(int m:{1,4,8,16}){auto p=prepare(data,m,t0common::Fixture::SingleCoordinate);prepare_activations(p);if(!storage_controls(p))throw std::runtime_error("single sweep baseline representation mismatch");std::size_t flat_fail=0,repack_fail=0;double flat_ratio=0,repack_ratio=0;auto fn=m==1?gemv:gemm;for(int pos=0;pos<I;++pos){p.c=t0common::generate_single_coordinate(m,pos);prepare_activations(p);run_once_flat(p);auto a=assess(p.c,(float*)p.result->data,p.out.size());flat_fail+=a.bad;flat_ratio=std::max(flat_ratio,a.error_over_B);run_once_repack(p,fn);auto b=assess(p.c,p.out.data(),p.out.size());repack_fail+=b.bad;repack_ratio=std::max(repack_ratio,b.error_over_B);}std::cout<<"SINGLE_SWEEP M="<<m<<" positions=512 batches=512 flat_failures="<<flat_fail<<" flat_max_error_over_B="<<flat_ratio<<" repack_failures="<<repack_fail<<" repack_max_error_over_B="<<repack_ratio<<" exact_integer_outputs="<<((flat_fail==0&&repack_fail==0)?"PASS":"FAIL")<<"\n";}
 inventory();return 0;
 }catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 2;}}



