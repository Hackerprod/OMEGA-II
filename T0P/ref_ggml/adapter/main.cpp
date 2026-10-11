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
bool large_page_privilege=false;
void* watched_dot=nullptr; BYTE watched_byte=0; volatile LONG dot_hit=0; uintptr_t dot_return=0; void* watched_sgemm=nullptr; BYTE sgemm_byte=0; volatile LONG sgemm_hit=0; uintptr_t sgemm_return=0;
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
bool check_large_page_privilege(){std::string state;bool ok=t0common::enable_large_page_privilege(&state);std::cout<<"LARGE_PAGE_PRIVILEGE="<<state<<"\n";return ok;}
bool page_query(const char*name,const void*ptr,size_t bytes){SYSTEM_INFO si{};GetSystemInfo(&si);const uintptr_t page=si.dwPageSize,start=reinterpret_cast<uintptr_t>(ptr)&~(page-1),end=(reinterpret_cast<uintptr_t>(ptr)+bytes+page-1)&~(page-1);std::vector<PSAPI_WORKING_SET_EX_INFORMATION> ws;for(uintptr_t p=start;p<end;p+=page){PSAPI_WORKING_SET_EX_INFORMATION q{};q.VirtualAddress=reinterpret_cast<void*>(p);ws.push_back(q);}BOOL ok=!ws.empty()&&QueryWorkingSetEx(GetCurrentProcess(),ws.data(),DWORD(ws.size()*sizeof(ws[0])));size_t large=0,valid=0;if(ok)for(const auto&q:ws){large+=q.VirtualAttributes.LargePage!=0;valid+=q.VirtualAttributes.Valid!=0;}std::cout<<"PAGES buffer="<<name<<" address="<<hx((uintptr_t)ptr)<<" bytes="<<bytes<<" queried_pages="<<ws.size()<<" valid_pages="<<valid<<" large_pages="<<large<<" QueryWorkingSetEx="<<(ok?"PASS":"FAIL")<<" LargePage="<<(large==ws.size()?"true":"false")<<" allocation="<<(large_page_privilege?"requested_large":"normal_fallback")<<"\n";return ok!=FALSE&&valid==ws.size();}
LONG CALLBACK dot_breakpoint_handler(EXCEPTION_POINTERS* ep){if(!ep||ep->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT)return EXCEPTION_CONTINUE_SEARCH;void* hit=nullptr;BYTE saved=0;volatile LONG* flag=nullptr;uintptr_t* ret=nullptr;if(watched_sgemm&&(ep->ContextRecord->Rip==(DWORD64)(uintptr_t)watched_sgemm+1||ep->ExceptionRecord->ExceptionAddress==watched_sgemm)){hit=watched_sgemm;saved=sgemm_byte;flag=&sgemm_hit;ret=&sgemm_return;}else if(watched_dot&&(ep->ContextRecord->Rip==(DWORD64)(uintptr_t)watched_dot+1||ep->ExceptionRecord->ExceptionAddress==watched_dot)){hit=watched_dot;saved=watched_byte;flag=&dot_hit;ret=&dot_return;}if(!hit)return EXCEPTION_CONTINUE_SEARCH;DWORD old=0;VirtualProtect(hit,1,PAGE_EXECUTE_READWRITE,&old);*(volatile BYTE*)hit=saved;FlushInstructionCache(GetCurrentProcess(),hit,1);DWORD tmp=0;VirtualProtect(hit,1,old,&tmp);*ret=(uintptr_t)(*(uintptr_t*)ep->ContextRecord->Rsp);ep->ContextRecord->Rip=(DWORD64)(uintptr_t)hit;InterlockedExchange(flag,1);return EXCEPTION_CONTINUE_EXECUTION;}
bool arm_dot_breakpoint(void* address){watched_dot=address;dot_hit=0;dot_return=0;DWORD old=0;if(!VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&old))return false;watched_byte=*(BYTE*)address;*(volatile BYTE*)address=0xCC;FlushInstructionCache(GetCurrentProcess(),address,1);DWORD tmp=0;VirtualProtect(address,1,old,&tmp);return true;}
void disarm_dot_breakpoint(){if(!watched_dot)return;if(!dot_hit){DWORD old=0;if(VirtualProtect(watched_dot,1,PAGE_EXECUTE_READWRITE,&old)){*(volatile BYTE*)watched_dot=watched_byte;FlushInstructionCache(GetCurrentProcess(),watched_dot,1);DWORD tmp=0;VirtualProtect(watched_dot,1,old,&tmp);}}watched_dot=nullptr;}
bool arm_sgemm_breakpoint(void* address){watched_sgemm=address;sgemm_hit=0;sgemm_return=0;DWORD old=0;if(!VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&old))return false;sgemm_byte=*(BYTE*)address;*(volatile BYTE*)address=0xCC;FlushInstructionCache(GetCurrentProcess(),address,1);DWORD tmp=0;VirtualProtect(address,1,old,&tmp);return true;}
void disarm_sgemm_breakpoint(){if(!watched_sgemm)return;if(!sgemm_hit){DWORD old=0;if(VirtualProtect(watched_sgemm,1,PAGE_EXECUTE_READWRITE,&old)){*(volatile BYTE*)watched_sgemm=sgemm_byte;FlushInstructionCache(GetCurrentProcess(),watched_sgemm,1);DWORD tmp=0;VirtualProtect(watched_sgemm,1,old,&tmp);}}watched_sgemm=nullptr;}

struct Prepared {
  t0common::Cell c; t0common::Buffer<uint8_t> flatw,flata,packw,packa; t0common::Buffer<std::byte> work; t0common::Buffer<float> out; ggml_context* ctx=nullptr; ggml_tensor* weight=nullptr; ggml_tensor* input=nullptr; ggml_tensor* result=nullptr; ggml_cgraph* graph=nullptr; ggml_threadpool* threadpool=nullptr; ggml_cplan plan{};
  Prepared()=default; Prepared(const Prepared&)=delete; Prepared& operator=(const Prepared&)=delete;
  Prepared(Prepared&& o) noexcept : c(std::move(o.c)),flatw(std::move(o.flatw)),flata(std::move(o.flata)),packw(std::move(o.packw)),packa(std::move(o.packa)),work(std::move(o.work)),out(std::move(o.out)),ctx(o.ctx),weight(o.weight),input(o.input),result(o.result),graph(o.graph),threadpool(o.threadpool),plan(o.plan){o.ctx=nullptr;o.threadpool=nullptr;}
  ~Prepared(){if(threadpool)ggml_threadpool_free(threadpool);if(ctx)ggml_free(ctx);}
};
enum class CandidateRoute { Flat, Repack, QA };
void prepare_weights(const t0common::Cell& c, Prepared& p, CandidateRoute route) {
    if(route!=CandidateRoute::Repack)p.flatw=t0common::ggml_q4_0(c);
    // Upstream block_q4_0x8 transform: preserve half scales, interleave eight rows,
    // XOR every packed byte with 0x88. Inverse below proves all codes and half bits.
    if(route!=CandidateRoute::Flat)p.packw.resize(size_t(O/8)*G*sizeof(Q4));
    for(int ot=0;route!=CandidateRoute::Flat&&ot<O/8;++ot) for(int g=0;g<G;++g){
      auto* d=reinterpret_cast<Q4*>(p.packw.data())+size_t(ot)*G+g;
      for(int j=0;j<8;++j)d->d[j]=c.ws_half[(ot*8+j)*G+g];
      for(int i=0;i<16;++i){int row=i%8,off=(i/8)*8,o=ot*8+row;for(int b=0;b<8;++b){int j=off+b;uint8_t q=uint8_t((unsigned(c.wq[o*I+g*32+j]+8)&15)|((unsigned(c.wq[o*I+g*32+j+16]+8)&15)<<4));d->qs[i*8+b]=q^0x88;}}
    }
}
// Dynamic per-application preparation. Charge this plus run_once for the dynamic boundary.
void prepare_dynamic(Prepared& p,CandidateRoute route) {
 const auto& c=p.c;const bool make_flat=route!=CandidateRoute::Repack,make_pack=route!=CandidateRoute::Flat;
 if(make_flat||c.M==1){p.flata.resize(size_t(c.M)*G*34);for(int m=0;m<c.M;++m)for(int g=0;g<G;++g){size_t d=(size_t(m)*G+g)*34;p.flata[d]=uint8_t(c.xs_half[m*G+g]);p.flata[d+1]=uint8_t(c.xs_half[m*G+g]>>8);for(int k=0;k<32;++k)p.flata[d+2+k]=uint8_t(c.xq[m*I+g*32+k]);}if(p.input)p.input->data=p.flata.data();}
 if(!make_pack||c.M==1)return;
 p.packa.resize(size_t(c.M/4)*G*sizeof(Q8x4));
 for(int rt=0;rt<c.M/4;++rt)for(int g=0;g<G;++g){auto*d=reinterpret_cast<Q8x4*>(p.packa.data())+size_t(rt)*G+g;for(int r=0;r<4;++r)d->d[r]=c.xs_half[(rt*4+r)*G+g];for(int i=0;i<16;++i){int row=i%4,off=(i/4)*8;for(int b=0;b<8;++b)d->qs[i*8+b]=uint8_t(c.xq[(rt*4+row)*I+g*32+off+b]);}}
}bool storage_controls(const Prepared& p){
 auto flat_ok=t0common::exact_representation(p.c,[&](auto&w,auto&ws,auto&x,auto&xs){w.resize(O*I);ws.resize(O*G);x.resize(p.c.M*I);xs.resize(p.c.M*G);for(int o=0;o<O;++o)for(int g=0;g<G;++g){size_t b=(size_t(o)*G+g)*18;ws[o*G+g]=uint16_t(p.flatw[b]|(uint16_t(p.flatw[b+1])<<8));for(int j=0;j<16;++j){int lo=(p.flatw[b+2+j]&15)-8,hi=(p.flatw[b+2+j]>>4)-8;w[o*I+g*32+j]=int8_t(lo);w[o*I+g*32+j+16]=int8_t(hi);}}for(int m=0;m<p.c.M;++m)for(int g=0;g<G;++g){size_t b=(size_t(m)*G+g)*34;xs[m*G+g]=uint16_t(p.flata[b]|(uint16_t(p.flata[b+1])<<8));for(int k=0;k<32;++k)x[m*I+g*32+k]=int8_t(p.flata[b+2+k]);}return true;});
 auto repack_ok=t0common::exact_representation(p.c,[&](auto&w,auto&ws,auto&x,auto&xs){w.resize(O*I);ws.resize(O*G);x.resize(p.c.M*I);xs.resize(p.c.M*G);for(int ot=0;ot<O/8;++ot)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q4*>(p.packw.data())+size_t(ot)*G+g;for(int j=0;j<8;++j)ws[(ot*8+j)*G+g]=d->d[j];for(int i=0;i<16;++i){int row=i%8,off=(i/8)*8;uint64_t v;std::memcpy(&v,d->qs+i*8,8);v^=0x8888888888888888ULL;for(int b=0;b<8;++b){uint8_t q=uint8_t(v>>(8*b));w[(ot*8+row)*I+g*32+off+b]=int8_t((q&15)-8);w[(ot*8+row)*I+g*32+off+b+16]=int8_t((q>>4)-8);}}}if(p.c.M==1){for(int m=0;m<p.c.M;++m)for(int g=0;g<G;++g){size_t b=(size_t(m)*G+g)*34;xs[m*G+g]=uint16_t(p.flata[b]|(uint16_t(p.flata[b+1])<<8));for(int k=0;k<32;++k)x[m*I+g*32+k]=int8_t(p.flata[b+2+k]);}}else for(int rt=0;rt<p.c.M/4;++rt)for(int g=0;g<G;++g){auto*d=reinterpret_cast<const Q8x4*>(p.packa.data())+size_t(rt)*G+g;for(int r=0;r<4;++r){xs[(rt*4+r)*G+g]=d->d[r];for(int k=0;k<32;++k)x[(rt*4+r)*I+g*32+k]=d->qs[((k/8)*4+r)*8+(k%8)];}}return true;});return flat_ok&&repack_ok;
}
Prepared prepare_static(const fs::path& data,int m,t0common::Fixture f,CandidateRoute route=CandidateRoute::QA) {
    Prepared p; auto name=std::string("m")+std::to_string(m)+"_"+t0common::fixture_name(f)+".bin"; p.c=t0common::load_cell(data/name);
    const auto generated=t0common::generate(m,f); if(p.c.sha256!=generated.sha256)throw std::runtime_error("hash canÃƒÂ³nico no coincide con generador: "+name);
    if(sha_file(data/name)!=p.c.sha256)throw std::runtime_error("hash del archivo no coincide: "+name);
    prepare_weights(p.c,p,route); p.out.resize(size_t(m)*O);
    if(route==CandidateRoute::Repack)return p;
    ggml_init_params ip{32*1024*1024,nullptr,false};p.ctx=ggml_init(ip);if(!p.ctx)throw std::runtime_error("ggml_init");
    auto*w=ggml_new_tensor_2d(p.ctx,GGML_TYPE_Q4_0,I,O);p.weight=w;auto*x=ggml_new_tensor_2d(p.ctx,GGML_TYPE_Q8_0,I,m);w->data=p.flatw.data();p.input=x;p.result=ggml_mul_mat(p.ctx,w,x);p.graph=ggml_new_graph(p.ctx);ggml_build_forward_expand(p.graph,p.result);
    auto tp=ggml_threadpool_params_default(1);p.threadpool=ggml_threadpool_new(&tp);if(!p.threadpool)throw std::runtime_error("threadpool create");p.plan=ggml_graph_plan(p.graph,1,p.threadpool);p.plan.use_ref=false;p.work.resize(p.plan.work_size);p.plan.work_data=p.work.empty()?nullptr:reinterpret_cast<uint8_t*>(p.work.data());
    return p;
}
// run_once uses preallocated graph, buffers and output; it performs no adapter allocation.
void run_once_flat(Prepared& p){if(ggml_graph_compute(p.graph,&p.plan)!=GGML_STATUS_SUCCESS)throw std::runtime_error("ggml graph compute");}
void run_once_repack(Prepared& p,repack_fn fn){const void* act=p.c.M==1?(const void*)p.flata.data():(const void*)p.packa.data();fn(I,p.out.data(),O,p.packw.data(),act,p.c.M,O);}
struct Err {double abs=0,rel=0,norm=0,error_over_B=0;bool finite=true,pass=true,old_pass=true,exact_single=true;size_t bad=0,old_bad=0;std::vector<t0common::Failure> failures;};
Err assess(const t0common::Cell&c,const float* y,size_t n){Err e;auto ref=t0common::reference64(c);if(n!=ref.r64.size())throw std::runtime_error("output size mismatch");std::vector<float> out(y,y+n);auto v=t0common::verify_s12(c,out);e.pass=v.pass;e.bad=v.failures;e.failures=v.failed;e.error_over_B=v.max_error_over_B;auto old=t0common::verify(c,out);e.old_pass=old.pass;e.old_bad=old.failures;if(c.fixture==t0common::Fixture::SingleCoordinate)for(size_t i=0;i<n;++i)if(double(y[i])!=ref.r64[i]){e.pass=false;e.exact_single=false;++e.bad;}
 for(size_t ix=0;ix<n;++ix){double a=y[ix],b=ref.r64[ix],d=std::abs(a-b);if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d)){e.finite=false;continue;}e.abs=std::max(e.abs,d);if(b!=0)e.rel=std::max(e.rel,d/std::abs(b));double terms=ref.S[ix];if(terms!=0)e.norm=std::max(e.norm,d/terms);}if(!e.finite)e.pass=false;return e;}
size_t exact_group_dot_isolation(const fs::path&data,repack_fn gemv,repack_fn gemm){
 const t0common::Fixture fixtures[]={t0common::Fixture::Uniform,t0common::Fixture::MixedCancellation,t0common::Fixture::VariedScales,t0common::Fixture::Sub127,t0common::Fixture::Extremes};size_t flat_checks=0,flat_fail=0,repack_checks=0,repack_fail=0;
 for(int m:{1,4,8,16})for(auto f:fixtures){auto p=prepare_static(data,m,f);auto base=p.c;auto fn=m==1?gemv:gemm;for(int g=0;g<G;++g){p.c=base;std::fill(p.c.ws_half.begin(),p.c.ws_half.end(),0x3c00);std::fill(p.c.xs_half.begin(),p.c.xs_half.end(),0x3c00);std::fill(p.c.ws.begin(),p.c.ws.end(),1.0f);std::fill(p.c.xs.begin(),p.c.xs.end(),1.0f);for(int row=0;row<m;++row)for(int j=0;j<I;++j)if(j/32!=g)p.c.xq[row*I+j]=0;prepare_weights(p.c,p,CandidateRoute::QA);p.weight->data=p.flatw.data();prepare_dynamic(p,CandidateRoute::QA);run_once_flat(p);auto*fy=(float*)p.result->data;for(int row=0;row<m;++row)for(int o=0;o<O;++o){int32_t oracle=0;for(int k=0;k<32;++k)oracle+=int32_t(p.c.wq[o*I+g*32+k])*int32_t(p.c.xq[row*I+g*32+k]);++flat_checks;if(double(fy[size_t(row)*O+o])!=double(oracle))++flat_fail;}run_once_repack(p,fn);for(int row=0;row<m;++row)for(int o=0;o<O;++o){int32_t oracle=0;for(int k=0;k<32;++k)oracle+=int32_t(p.c.wq[o*I+g*32+k])*int32_t(p.c.xq[row*I+g*32+k]);++repack_checks;if(double(p.out[size_t(row)*O+o])!=double(oracle))++repack_fail;}}}
 std::cout<<"EXACT_GROUP_DOT_ISOLATION route=ggml_flat checks="<<flat_checks<<" failures="<<flat_fail<<" status="<<(flat_fail?"FAIL":"PASS")<<"\n";std::cout<<"EXACT_GROUP_DOT_ISOLATION route=ggml_repack checks="<<repack_checks<<" failures="<<repack_fail<<" status="<<(repack_fail?"FAIL":"PASS")<<"\n";return flat_fail+repack_fail;
}void inventory(){HMODULE mods[512];DWORD needed=0;if(!EnumProcessModules(GetCurrentProcess(),mods,sizeof(mods),&needed))throw std::runtime_error("EnumProcessModules");std::cout<<"DLL_INVENTORY_BEGIN\n";for(unsigned i=0;i<needed/sizeof(HMODULE);++i){wchar_t path[32768];DWORD n=GetModuleFileNameExW(GetCurrentProcess(),mods[i],path,32768);if(!n)continue;MODULEINFO mi{};GetModuleInformation(GetCurrentProcess(),mods[i],&mi,sizeof(mi));fs::path p(path);std::cout<<"DLL "<<p.string()<<" size="<<mi.SizeOfImage<<" sha256="<<sha_file(p)<<"\n";}std::cout<<"DLL_INVENTORY_END\n";}
}

int main(int argc,char**argv){try{
 large_page_privilege=check_large_page_privilege();const char* omp=getenv("OMP_NUM_THREADS"),*dyn=getenv("OMP_DYNAMIC");if(!omp||std::string(omp)!="1"||!dyn||_stricmp(dyn,"FALSE")!=0)throw std::runtime_error("Se requiere OMP_NUM_THREADS=1 y OMP_DYNAMIC=FALSE antes de cargar ggml");
 if(argc!=2)throw std::runtime_error("uso: adapter.exe <ruta T0P/common/data>");fs::path data=fs::absolute(argv[1]);
 for(unsigned i=0;i<(1u<<16);++i)ggml_table_f32_f16[i]=ggml_fp16_to_fp32((ggml_fp16_t)i);
 HMODULE cpu=GetModuleHandleW(L"ggml-cpu.dll");if(!cpu)throw std::runtime_error("ggml-cpu.dll no cargada");
 auto gemv=&ggml_gemv_q4_0_8x8_q8_0;auto gemm=&ggml_gemm_q4_0_8x8_q8_0;
 auto traits=ggml_get_type_traits_cpu(GGML_TYPE_Q4_0);auto dot=traits?reinterpret_cast<void*>(traits->vec_dot):nullptr;MODULEINFO cmi{};GetModuleInformation(GetCurrentProcess(),cpu,&cmi,sizeof(cmi));
 std::cout<<"repack_object_vpdpbusd=218 source=upstream_repack.cpp object_sha256="<<sha_file(fs::path("obj/repack_x86.obj"))<<"\n"; std::cout<<"ENV OMP_NUM_THREADS="<<omp<<" OMP_DYNAMIC="<<dyn<<" n_threads=1 trait_vec_dot="<<hx((uintptr_t)dot)<<" trait_offset="<<hx((uintptr_t)dot-(uintptr_t)cmi.lpBaseOfDll)<<" ggml_cpu_base="<<hx((uintptr_t)cmi.lpBaseOfDll)<<" repack_gemv="<<hx((uintptr_t)gemv)<<" repack_gemm="<<hx((uintptr_t)gemm)<<" flat_evidence=Q4_0_cpu_trait_callback_address_from_loaded_ggml-cpu.dll; symbol export/PDB absent\n";
 if(!gemv||!gemm||!dot)throw std::runtime_error("evidencia/sÃƒÂ­mbolos de despacho incompletos");wchar_t loaded_cpu_path[32768]{};GetModuleFileNameW(cpu,loaded_cpu_path,DWORD(std::size(loaded_cpu_path)));if(sha_file(fs::path(loaded_cpu_path))!="cfce3924a4de5ef82317089937edc1a4ae494515af1f1d9ee8ef05f6e921ba23")throw std::runtime_error("ggml-cpu.dll hash no coincide con el RVA de sgemm");void* sgemm=reinterpret_cast<void*>((uintptr_t)cmi.lpBaseOfDll+0xc4dc0);
 auto veh=AddVectoredExceptionHandler(1,dot_breakpoint_handler);if(!veh)throw std::runtime_error("AddVectoredExceptionHandler failed");bool callback_seen[17]={};bool callback_checked[17]={};t0common::verifier_unit_tests();const t0common::Fixture fs[]={t0common::Fixture::Uniform,t0common::Fixture::MixedCancellation,t0common::Fixture::VariedScales,t0common::Fixture::Sub127,t0common::Fixture::Extremes,t0common::Fixture::ZeroInput,t0common::Fixture::SingleCoordinate,t0common::Fixture::CancellationHigh,t0common::Fixture::CancellationExtreme,t0common::Fixture::ExactCancellation};
 bool mandatory_pass=true;{
 auto both=prepare_static(data,4,t0common::Fixture::Uniform,CandidateRoute::QA);
 auto flat=prepare_static(data,4,t0common::Fixture::Uniform,CandidateRoute::Flat);
 auto repack=prepare_static(data,4,t0common::Fixture::Uniform,CandidateRoute::Repack);
 bool static_ok=!flat.flatw.empty()&&flat.packw.empty()&&flat.ctx&&repack.flatw.empty()&&!repack.packw.empty()&&!repack.ctx&&flat.flatw==both.flatw&&repack.packw==both.packw;
 prepare_dynamic(both,CandidateRoute::QA);prepare_dynamic(flat,CandidateRoute::Flat);prepare_dynamic(repack,CandidateRoute::Repack);
 bool dynamic_ok=!flat.flata.empty()&&flat.packa.empty()&&!repack.flata.size()&&!repack.packa.empty()&&both.flata==flat.flata&&both.packa==repack.packa;
 std::cout<<"STATIC_ROUTE_ISOLATION flat="<<(flat.packw.empty()?"PASS":"FAIL")<<" repack="<<(repack.flatw.empty()&&!repack.ctx?"PASS":"FAIL")<<" status="<<(static_ok?"PASS":"FAIL")<<"\n";
 std::cout<<"DYNAMIC_ROUTE_ISOLATION flat="<<(flat.packa.empty()?"PASS":"FAIL")<<" repack="<<(repack.flata.empty()?"PASS":"FAIL")<<" status="<<(dynamic_ok?"PASS":"FAIL")<<"\n";
 mandatory_pass&=static_ok&&dynamic_ok;
}for(auto f:fs)for(int m:{1,4,8,16}){auto p=prepare_static(data,m,f);prepare_dynamic(p,CandidateRoute::QA);bool repr=storage_controls(p);std::cout<<"control_representation flat_and_effective_repack="<<(repr?"PASS":"FAIL")<<"\n";if(!repr)throw std::runtime_error("effective-storage representation mismatch");mandatory_pass&=repr;prepare_dynamic(p,CandidateRoute::Flat);bool capture=!callback_checked[m];if(capture&&(!arm_dot_breakpoint(dot)||!arm_sgemm_breakpoint(sgemm)))throw std::runtime_error("failed to arm dispatch breakpoints");run_once_flat(p);if(capture){disarm_dot_breakpoint();disarm_sgemm_breakpoint();callback_checked[m]=true;callback_seen[m]=dot_hit!=0;wchar_t module_path[32768]{};GetModuleFileNameW(cpu,module_path,32768);std::cout<<"S14_FLAT_CALLBACK m="<<m<<" reached="<<(dot_hit?"true":"false")<<" module="<<fs::path(module_path).string()<<" sha256="<<sha_file(fs::path(module_path))<<" base="<<hx((uintptr_t)cmi.lpBaseOfDll)<<" rva="<<hx((uintptr_t)dot-(uintptr_t)cmi.lpBaseOfDll)<<" callback="<<hx((uintptr_t)dot)<<" caller_return="<<hx(dot_return)<<" evidence=INT3_vectored_exception_during_ggml_mul_mat\n";const char* route=dot_hit?"ggml_vec_dot_q4_0_q8_0":(sgemm_hit?"llamafile_sgemm":"unresolved");std::cout<<"S14_DISPATCH m="<<m<<" route="<<route<<" module=ggml-cpu.dll sha256="<<sha_file(fs::path(module_path))<<" base="<<hx((uintptr_t)cmi.lpBaseOfDll)<<" callback_rva="<<hx((uintptr_t)dot-(uintptr_t)cmi.lpBaseOfDll)<<" sgemm_rva=0xc4dc0 callback_hit="<<(dot_hit?"true":"false")<<" sgemm_hit="<<(sgemm_hit?"true":"false")<<" caller_return="<<hx(dot_hit?dot_return:sgemm_return)<<" evidence=INT3_entry_breakpoint_from_COFF_symbol_matched_to_loaded_image\n";mandatory_pass&=(dot_hit||sgemm_hit);}auto ef=assess(p.c,(float*)p.result->data,p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=flat executed=ggml_mul_mat trait_callback="<<hx((uintptr_t)dot)<<" finite="<<ef.finite<<" S12_pass="<<ef.pass<<" S12_failures="<<ef.bad<<" single_coordinate_exact="<<ef.exact_single<<" max_error_over_B="<<std::setprecision(12)<<ef.error_over_B<<" old_tolerance_pass="<<ef.old_pass<<" old_tolerance_failures="<<ef.old_bad<<" abs="<<ef.abs<<" rel="<<ef.rel<<" normalized="<<ef.norm<<" hash="<<p.c.sha256<<"\n";mandatory_pass&=ef.pass;if(f==t0common::Fixture::ExactCancellation){size_t zfail=0;for(size_t z=0;z<size_t(m)*O;++z)if(((float*)p.result->data)[z]!=0.0f)++zfail;mandatory_pass&=(zfail==0);std::cout<<"EXACT_CANCELLATION route=flat m="<<m<<" checks="<<size_t(m)*O<<" failures="<<zfail<<" status="<<(zfail?"FAIL":"PASS")<<"\\n";}for(const auto& f:ef.failures)std::cout<<"S12_FAIL route=flat m="<<f.m<<" o="<<f.o<<" r64="<<f.r64<<" y="<<f.y<<" abs="<<f.error<<" S="<<f.S<<" B="<<f.B<<" error_over_B="<<f.error_over_B<<" S_over_abs_r="<<f.S_over_abs_r<<"\n";
 auto fn=m==1?gemv:gemm;prepare_dynamic(p,CandidateRoute::Repack);run_once_repack(p,fn);auto er=assess(p.c,p.out.data(),p.out.size());std::cout<<"CELL fixture="<<t0common::fixture_name(f)<<" m="<<m<<" route=repack executed="<<(m==1?"ggml_gemv_q4_0_8x8_q8_0":"ggml_gemm_q4_0_8x8_q8_0")<<" address="<<hx((uintptr_t)fn)<<" input_pack=lossless finite="<<er.finite<<" S12_pass="<<er.pass<<" S12_failures="<<er.bad<<" single_coordinate_exact="<<er.exact_single<<" max_error_over_B="<<er.error_over_B<<" old_tolerance_pass="<<er.old_pass<<" old_tolerance_failures="<<er.old_bad<<" abs="<<er.abs<<" rel="<<er.rel<<" normalized="<<er.norm<<" hash="<<p.c.sha256<<"\n";mandatory_pass&=er.pass;if(f==t0common::Fixture::ExactCancellation){size_t zfail=0;for(float v:p.out)if(v!=0.0f)++zfail;mandatory_pass&=(zfail==0);std::cout<<"EXACT_CANCELLATION route=repack m="<<m<<" checks="<<p.out.size()<<" failures="<<zfail<<" status="<<(zfail?"FAIL":"PASS")<<"\\n";}for(const auto& f:er.failures)std::cout<<"S12_FAIL route=repack m="<<f.m<<" o="<<f.o<<" r64="<<f.r64<<" y="<<f.y<<" abs="<<f.error<<" S="<<f.S<<" B="<<f.B<<" error_over_B="<<f.error_over_B<<" S_over_abs_r="<<f.S_over_abs_r<<"\n";bool page_ok=page_query("flat_weights",p.flatw.data(),p.flatw.size())&&page_query("flat_activations",p.flata.data(),p.flata.size())&&page_query("flat_output",p.result->data,size_t(m)*O*sizeof(float))&&page_query("repack_weights",p.packw.data(),p.packw.size())&&page_query("repack_output",p.out.data(),p.out.size()*sizeof(float));if(m>1)page_ok=page_ok&&page_query("repack_activations",p.packa.data(),p.packa.size());mandatory_pass&=page_ok;}
 for(int m:{1,4,8,16}){auto p=prepare_static(data,m,t0common::Fixture::SingleCoordinate);prepare_dynamic(p,CandidateRoute::QA);if(!storage_controls(p))throw std::runtime_error("single sweep baseline representation mismatch");std::size_t flat_fail=0,repack_fail=0;double flat_ratio=0,repack_ratio=0;auto fn=m==1?gemv:gemm;for(int pos=0;pos<I;++pos){p.c=t0common::generate_single_coordinate(m,pos);prepare_dynamic(p,CandidateRoute::Flat);run_once_flat(p);auto a=assess(p.c,(float*)p.result->data,p.out.size());flat_fail+=a.bad;flat_ratio=std::max(flat_ratio,a.error_over_B);prepare_dynamic(p,CandidateRoute::Repack);run_once_repack(p,fn);auto b=assess(p.c,p.out.data(),p.out.size());repack_fail+=b.bad;repack_ratio=std::max(repack_ratio,b.error_over_B);}std::cout<<"SINGLE_SWEEP M="<<m<<" positions=512 batches=512 flat_failures="<<flat_fail<<" flat_max_error_over_B="<<flat_ratio<<" repack_failures="<<repack_fail<<" repack_max_error_over_B="<<repack_ratio<<" exact_integer_outputs="<<((flat_fail==0&&repack_fail==0)?"PASS":"FAIL")<<"\n";mandatory_pass&=(flat_fail==0&&repack_fail==0);}
 size_t group_fail=exact_group_dot_isolation(data,gemv,gemm);mandatory_pass&=(group_fail==0);RemoveVectoredExceptionHandler(veh);inventory();std::cout<<"MANDATORY_GATE="<<(mandatory_pass?"PASS":"FAIL")<<"\n";if(!mandatory_pass)throw std::runtime_error("mandatory QA control failed");return 0;
 }catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 2;}}











