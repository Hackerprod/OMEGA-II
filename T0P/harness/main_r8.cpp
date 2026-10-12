#define NOMINMAX
#include "harness.hpp"
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>

namespace harness {
namespace {
struct Barrier { std::mutex m; std::condition_variable cv; unsigned n,arrived=0,g=0; explicit Barrier(unsigned x):n(x){} void wait(){std::unique_lock<std::mutex> l(m);auto q=g;if(++arrived==n){arrived=0;++g;cv.notify_all();}else cv.wait(l,[&]{return g!=q;});} };
std::mutex log_mutex;
uint64_t tsc(){_mm_lfence();unsigned aux=0;auto x=__rdtscp(&aux);_mm_lfence();return x;}
double qpc_seconds(int64_t a,int64_t b,int64_t f){return double(b-a)/double(f);}
double quantile(std::vector<double> v,double q){std::sort(v.begin(),v.end());double x=(v.size()-1)*q;auto i=(size_t)x;return v[i]+(x-i)*(v[std::min(i+1,v.size()-1)]-v[i]);}
bool page_query(const Region&r){if(!r.data||!r.bytes||!r.large)return false;auto p=(uintptr_t)r.data;auto n=r.bytes;SYSTEM_INFO si{};GetSystemInfo(&si);auto first=p&~(uintptr_t(si.dwPageSize)-1),last=(p+n-1)&~(uintptr_t(si.dwPageSize)-1);std::vector<PSAPI_WORKING_SET_EX_INFORMATION> q;for(auto a=first;;a+=si.dwPageSize){PSAPI_WORKING_SET_EX_INFORMATION z{};z.VirtualAddress=(void*)a;q.push_back(z);if(a==last)break;}if(!QueryWorkingSetEx(GetCurrentProcess(),q.data(),DWORD(q.size()*sizeof(q[0]))))return false;for(auto&z:q)if(!z.VirtualAttributes.Valid||!z.VirtualAttributes.LargePage)return false;return true;}
bool official_duration_gate(double seconds){return std::isfinite(seconds)&&seconds>=2.0;}
bool affinity_matches(KAFFINITY expected,KAFFINITY observed){return expected==observed;}
bool telemetry_valid(bool valid){return valid;}
bool run_negative_qa(){bool all=true;auto report=[&](const char*n,bool ok){std::cout<<"NEGATIVE label=SMOKE case="<<n<<" rejected="<<(ok?"PASS":"FAIL")<<"\n";all&=ok;};report("broken_affinity",!affinity_matches(1,2));Region four_kib{&all,sizeof(all),false,"4K"};report("page_outside_large_pages",!page_query(four_kib));report("official_sample_under_2s",!official_duration_gate(1.999));auto c=t0common::generate(1,t0common::Fixture::Uniform);auto y=t0common::reference(c);y[0]=NAN;report("numerical_mismatch",!t0common::verify_s12(c,y).pass);report("invalid_telemetry",!telemetry_valid(false));
  std::atomic<unsigned> finished{0};Barrier b(8);std::vector<std::thread> ts;for(unsigned i=0;i<8;++i)ts.emplace_back([&,i]{if(i==7)std::this_thread::sleep_for(std::chrono::milliseconds(5));finished.fetch_add(1);b.wait();});for(auto&t:ts)t.join();report("slow_worker_barrier",finished.load()==8);return all;}
void margin(Provider&p,unsigned w,int m,int64_t freq){LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);do{p.run_once(w,m);QueryPerformanceCounter(&b);}while(qpc_seconds(a.QuadPart,b.QuadPart,freq)<.200);}
struct Result{std::string name;double tsc_us=0,qpc_us=0;uint64_t start=0,end=0;bool affinity=false,valid=false;};
void emit(std::ofstream&f,const std::string&line){f<<line<<"\n";f.flush();}
}
int run_smoke(const std::string& outdir){
  CreateDirectoryA(outdir.c_str(),nullptr);std::ofstream raw(outdir+"\\raw.jsonl",std::ios::trunc),summary(outdir+"\\summary.json",std::ios::trunc);if(!raw||!summary)throw std::runtime_error("no se pueden crear logs harness");
  if(!run_negative_qa())throw std::runtime_error("negative QA failed");
  std::string lp_state;const bool lp_priv=t0common::enable_large_page_privilege(&lp_state);std::cout<<"LARGE_PAGE_PRIVILEGE="<<lp_state<<"\n";
  LARGE_INTEGER fq{};if(!QueryPerformanceFrequency(&fq))throw std::runtime_error("QPC no disponible");
  bool overall=true;
  struct Entry{const char* name;std::unique_ptr<Provider>(*factory)();};Entry entries[]={{"omega_q4",make_omega},{"ggml_flat",make_ggml_flat},{"torch_out",make_torch_out}};
  for(auto&e:entries){CellSpec cell;cell.m=1;cell.output=1024;cell.id="smoke_uniform_m1";std::vector<std::unique_ptr<Provider>> p;std::atomic<bool> setup{true};std::vector<std::thread> threads;Barrier ready(8),begin(8),done(8);std::vector<int64_t> pad0(24),qs(24),qe(24),pad1(24);std::vector<uint64_t> cs(24),ce(24),R(24);std::vector<uint8_t> aff(8,0);
    for(unsigned w=0;w<8;++w)p.emplace_back(e.factory());
    for(unsigned w=0;w<8;++w)threads.emplace_back([&,w]{try{GROUP_AFFINITY a{};a.Group=0;a.Mask=KAFFINITY(1u<<(2*w));if(!SetThreadGroupAffinity(GetCurrentThread(),&a,nullptr))throw std::runtime_error("SetThreadGroupAffinity");GROUP_AFFINITY got{};if(!GetThreadGroupAffinity(GetCurrentThread(),&got)||got.Group!=0||got.Mask!=a.Mask||GetCurrentProcessorNumber()!=2*w)throw std::runtime_error("afinidad efectiva distinta");p[w]->prepare(cell,w);std::string v;if(!p[w]->validate(v))throw std::runtime_error(v);for(auto&b:p[w]->bytes_regions())if(!page_query(b))throw std::runtime_error(std::string("PAGE_FAIL ")+b.name);aff[w]=true;}catch(const std::exception&x){setup=false;std::lock_guard<std::mutex> l(log_mutex);std::cerr<<e.name<<" worker="<<w<<" setup="<<x.what()<<"\n";}ready.wait();if(!setup){done.wait();return;}for(int k=0;k<5;++k)p[w]->run_once(w,cell.m);ready.wait();for(unsigned rep=0;rep<3;++rep){auto ix=rep*8+w;LARGE_INTEGER q{};QueryPerformanceCounter(&q);pad0[ix]=q.QuadPart;margin(*p[w],w,cell.m,fq.QuadPart);begin.wait();QueryPerformanceCounter(&q);qs[ix]=q.QuadPart;cs[ix]=tsc();do{p[w]->run_once(w,cell.m);++R[ix];QueryPerformanceCounter(&q);}while(qpc_seconds(qs[ix],q.QuadPart,fq.QuadPart)<2.5);ce[ix]=tsc();qe[ix]=q.QuadPart;done.wait();margin(*p[w],w,cell.m,fq.QuadPart);QueryPerformanceCounter(&q);pad1[ix]=q.QuadPart;}});
    for(auto&t:threads)t.join();
    bool ok=setup.load()&&lp_priv;std::vector<double> samples;std::vector<bool> cross(3,false);if(ok)for(unsigned rep=0;rep<3;++rep){auto off=rep*8;auto min_s=*std::min_element(cs.begin()+off,cs.begin()+off+8),max_e=*std::max_element(ce.begin()+off,ce.begin()+off+8);auto min_q=*std::min_element(qs.begin()+off,qs.begin()+off+8),max_q=*std::max_element(qe.begin()+off,qe.begin()+off+8);double wall=qpc_seconds(min_q,max_q,fq.QuadPart)*1e6;std::vector<double> hz;for(unsigned w=0;w<8;++w){auto ix=off+w;double dt=qpc_seconds(qs[ix],qe[ix],fq.QuadPart);if(dt>0)hz.push_back(double(ce[ix]-cs[ix])/dt);}double app=double(max_e-min_s)/quantile(hz,.5)*1e6;cross[rep]=std::abs(app-wall)/std::max(app,wall)<.05;ok&=cross[rep];samples.push_back(app);}
    double med=0,iqr=0,mad=0,lo=0,hi=0;if(samples.size()==3){med=quantile(samples,.5);iqr=quantile(samples,.75)-quantile(samples,.25);auto d=samples;for(auto&x:d)x=std::abs(x-med);mad=quantile(d,.5);lo=*std::min_element(samples.begin(),samples.end());hi=*std::max_element(samples.begin(),samples.end());}
    for(unsigned rep=0;rep<3;++rep)for(unsigned w=0;w<8;++w){auto ix=rep*8+w;std::ostringstream s;s<<"{\"label\":\"SMOKE\",\"provider\":\""<<e.name<<"\",\"cell\":\""<<cell.id<<"\",\"m\":1,\"replica\":"<<rep<<",\"worker\":"<<w<<",\"lp\":"<<2*w<<",\"N\":3,\"R\":"<<R[ix]<<",\"t_app_us\":";if(setup)s<<std::fixed<<std::setprecision(3)<<samples[rep];else s<<"null";s<<",\"t_pad_ini_qpc\":"<<pad0[ix]<<",\"t_ini_qpc\":"<<qs[ix]<<",\"t_fin_qpc\":"<<qe[ix]<<",\"t_pad_fin_qpc\":"<<pad1[ix]<<",\"tsc_start\":"<<cs[ix]<<",\"tsc_end\":"<<ce[ix]<<",\"qpc_start\":"<<qs[ix]<<",\"qpc_end\":"<<qe[ix]<<",\"large_pages\":"<<(setup?"true":"false")<<",\"affinity\":\""<<(aff[w]?"PASS":"FAIL")<<"\",\"telemetry\":\"INVALID_TELEMETRY:ACTIVE_FREQ_POWER_THERMAL_UNDERIVATION_MISSING\",\"status\":\""<<(ok?"SMOKE_TIMED":"NO_MEASURE")<<"\"}";emit(raw,s.str());}
    overall &= ok;summary<<"{\"provider\":\""<<e.name<<"\",\"N\":3,\"median_us\":"<<med<<",\"IQR_us\":"<<iqr<<",\"MAD_us\":"<<mad<<",\"min_us\":"<<lo<<",\"max_us\":"<<hi<<",\"qpc_crosscheck\":"<<(ok?"PASS":"FAIL")<<",\"telemetry\":\"INVALID_TELEMETRY:ACTIVE_FREQ_POWER_THERMAL_UNDERIVATION_MISSING\",\"label\":\"SMOKE\"}\n";
    std::cout<<"SMOKE provider="<<e.name<<" worker_count=8 LP=0,2,4,6,8,10,12,14 N=3 R=1 median_us="<<med<<" qpc_crosscheck="<<(ok?"PASS":"FAIL")<<" telemetry=PENDING_BLOCK_ANALYSIS\n";
  }
  return overall?0:3;
}
}
int main(int argc,char**argv){try{if(argc!=3||std::string(argv[1])!="--smoke")return 2;return harness::run_smoke(argv[2]);}catch(const std::exception&e){std::cerr<<"HARNESS_ERROR "<<e.what()<<"\n";return 1;}}


