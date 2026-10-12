#define NOMINMAX
#include "harness_r12.hpp"
#include "topology_r12.hpp"
#include "large_pages_r12.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace harness {
namespace {
struct Barrier {
  std::mutex mutex; std::condition_variable cv; unsigned total, arrived=0; std::uint64_t generation=0; bool cancelled=false;
  explicit Barrier(unsigned n):total(n){}
  bool wait(){std::unique_lock<std::mutex> lock(mutex);if(cancelled)return false;const auto g=generation;if(++arrived==total){arrived=0;++generation;cv.notify_all();return true;}cv.wait(lock,[&]{return generation!=g||cancelled;});return !cancelled;}
  void cancel(){std::lock_guard<std::mutex> lock(mutex);cancelled=true;cv.notify_all();}
};
struct Route { const char* id; const char* torch_route; std::unique_ptr<Provider>(*factory)(); };
struct Mark { std::int64_t q0=0,q1=0; std::uint64_t t0=0,t1=0; bool affinity=false; };
struct RunResult { std::uint64_t R=0, prep_bytes=0; std::vector<Mark> marks; std::vector<double> team_us; std::string error; };

std::string read_file(const std::string& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("file open failed: "+path);return {std::istreambuf_iterator<char>(in),{}};}
std::string sha256(const std::string& data){BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;DWORD objn=0,got=0;if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("BCrypt SHA256 provider failed");if(BCryptGetProperty(a,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objn),sizeof(objn),&got,0)<0)throw std::runtime_error("BCrypt SHA256 object length failed");std::vector<UCHAR> object(objn),digest(32);if(BCryptCreateHash(a,&h,object.data(),objn,nullptr,0,0)<0||BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),DWORD(data.size()),0)<0||BCryptFinishHash(h,digest.data(),DWORD(digest.size()),0)<0)throw std::runtime_error("BCrypt SHA256 hash failed");BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto b:digest)out<<std::setw(2)<<unsigned(b);return out.str();}
void verify_plan(const std::string& path,const std::string& expected){const auto raw=read_file(path);const std::string marker=",\"sha256\":\""+expected+"\"";const auto at=raw.rfind(marker);if(expected.size()!=64||at==std::string::npos||at+marker.size()!=raw.size()-1||raw.back()!='}')throw std::runtime_error("plan canonical SHA256 field mismatch");if(sha256(raw.substr(0,at)+"}")!=expected)throw std::runtime_error("plan SHA256 mismatch");}
std::string plan_event(const std::string& bytes,const std::string& provider,const std::string& frontier,int replica){const std::string wanted="competency|8P|m1|"+frontier+"|r"+std::to_string(replica)+"|"+provider;const std::regex rx("\\\"id\\\":\\\"([^\\\"]+)\\\"");std::smatch m;auto begin=bytes.cbegin();while(std::regex_search(begin,bytes.cend(),m,rx)){if(m[1].str()==wanted)return wanted;begin=m.suffix().first;}throw std::runtime_error("frozen plan lacks event "+wanted);}
std::uint64_t qpc(){LARGE_INTEGER x{};QueryPerformanceCounter(&x);return static_cast<std::uint64_t>(x.QuadPart);}
std::uint64_t tsc(){_mm_lfence();unsigned aux=0;const auto v=__rdtscp(&aux);_mm_lfence();return v;}
double seconds(std::uint64_t begin,std::uint64_t end,std::int64_t qpf){return double(end-begin)/double(qpf);}
void workload_margin(Provider& p,unsigned worker,const std::string& frontier,std::int64_t qpf){const auto start=qpc();auto end=start;do{p.run_once_frontier(worker,1,frontier);end=qpc();}while(seconds(start,end,qpf)<.200);}
void create_new_file(const std::string& path){HANDLE h=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CREATE_NEW failed path="+path+" error="+std::to_string(GetLastError()));CloseHandle(h);}
void write_new(const std::string& path,const std::string& content){create_new_file(path);std::ofstream out(path,std::ios::binary|std::ios::app);out.write(content.data(),std::streamsize(content.size()));out.flush();if(!out)throw std::runtime_error("write failed: "+path);}
std::string harness_dir(){char path[MAX_PATH]{};const DWORD n=GetModuleFileNameA(nullptr,path,MAX_PATH);if(!n||n>=MAX_PATH)throw std::runtime_error("GetModuleFileName failed");std::string p(path,n);const auto slash=p.find_last_of("/\\");if(slash==std::string::npos)return ".";p.resize(slash);const std::string marker="\\logs_r12\\obj_r12";const auto at=p.rfind(marker);if(at!=std::string::npos&&at+marker.size()==p.size())p.resize(at);return p;}
void invoke_python(const std::string& script,const std::vector<std::string>& args){std::string command="python -B \""+script+"\"";for(const auto& arg:args)command+=" \""+arg+"\"";std::vector<char> writable(command.begin(),command.end());writable.push_back('\0');STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessA(nullptr,writable.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&si,&pi))throw std::runtime_error("CreateProcess python failed command="+command+" error="+std::to_string(GetLastError()));WaitForSingleObject(pi.hProcess,INFINITE);DWORD exit=1;GetExitCodeProcess(pi.hProcess,&exit);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);if(exit)throw std::runtime_error("python adapter exit="+std::to_string(exit)+" command="+command);}
std::string row_json(const std::string& provider,const std::string& frontier,int replica,int sample_count,const std::string& event,std::uint64_t R,std::uint64_t prep_bytes,const std::vector<Mark>& marks,std::int64_t qpf,double team_us,unsigned worker){const auto& p=worker_processor_r12(worker);const auto& z=marks[worker];const std::uint64_t work=512ull*1024ull;const auto t0=std::min_element(marks.begin(),marks.end(),[](const Mark&a,const Mark&b){return a.t0<b.t0;})->t0;const auto t1=std::max_element(marks.begin(),marks.end(),[](const Mark&a,const Mark&b){return a.t1<b.t1;})->t1;std::ostringstream o;o<<"{\"label\":\"SMOKE\",\"provider\":\""<<provider<<"\",\"plan_event\":\""<<event<<"\",\"cell\":\"O1024_m1\",\"frontier\":\""<<frontier<<"\",\"O\":1024,\"m\":1,\"replica\":"<<replica<<",\"worker\":"<<worker<<",\"p_core\":"<<p.p_core<<",\"lp\":"<<p.lp<<",\"N\":"<<sample_count<<",\"R\":"<<R<<",\"R_common\":"<<R<<",\"mac_per_iteration\":"<<work<<",\"macs_per_call\":"<<work<<",\"macs_total\":"<<work*R<<",\"dynamic_prep_bytes_per_call\":"<<(frontier=="dynamic"?prep_bytes:0)<<",\"timed_preparation_calls\":"<<(frontier=="dynamic"?R:0)<<",\"timed_preparation_bytes\":"<<(frontier=="dynamic"?prep_bytes*R:0)<<",\"team_tsc_start\":"<<t0<<",\"team_tsc_end\":"<<t1<<",\"tsc_start\":"<<z.t0<<",\"tsc_end\":"<<z.t1<<",\"qpc_start\":"<<z.q0<<",\"qpc_end\":"<<z.q1<<",\"worker_qpc_ticks\":"<<(z.q1-z.q0)<<",\"team_qpc_ticks\":"<<std::uint64_t(team_us*double(qpf)/1e6)<<",\"t_app_us\":"<<std::fixed<<std::setprecision(3)<<team_us<<",\"large_pages\":true,\"affinity\":\""<<(z.affinity?"PASS":"FAIL")<<"\",\"numerical\":\"S12_PASS\",\"status\":\"SMOKE_TIMED\"}";return o.str();}

RunResult measure_route(const Route& route,const std::string& frontier,const std::string& plan_bytes,std::ofstream& raw,int sample_count,std::int64_t qpf){
  constexpr unsigned workers=8;constexpr double target_seconds=2.5,pilot_seconds=.2;CellSpec spec;spec.id="O1024_m1";spec.output=1024;spec.m=1;spec.frontier=frontier;if(route.torch_route&&*route.torch_route)spec.variant=route.torch_route;
  RunResult result;std::vector<std::unique_ptr<Provider>> providers(workers);std::vector<double> rates(workers);std::atomic<bool> healthy{true};std::mutex error_mutex;Barrier setup(workers),pilot(workers);std::vector<std::thread> threads;
  for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w]{try{if(!set_worker_affinity_r12(w)||!worker_affinity_matches_r12(w))throw std::runtime_error("preflight LP affinity mismatch worker="+std::to_string(w));providers[w]=route.factory();providers[w]->prepare(spec,w);std::string why;if(!providers[w]->validate(why))throw std::runtime_error(std::string(route.id)+" S12/representation QA: "+why);for(const auto& region:providers[w]->bytes_regions())if(!region.large||!region.data||!region.bytes)throw std::runtime_error(std::string("large-page region missing: ")+region.name);}catch(const std::exception&e){healthy=false;std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();}if(!setup.wait()||!healthy)return;try{for(int i=0;i<5;++i)providers[w]->run_once_frontier(w,1,frontier);if(!pilot.wait())return;const auto a=qpc();auto b=a;std::uint64_t calls=0;do{providers[w]->run_once_frontier(w,1,frontier);++calls;b=qpc();}while(seconds(a,b,qpf)<pilot_seconds);rates[w]=double(calls)/seconds(a,b,qpf);}catch(const std::exception&e){healthy=false;pilot.cancel();std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();}});
  for(auto&t:threads)t.join();if(!healthy)throw std::runtime_error("SMOKE setup/calibration blocked: "+result.error);const double slowest=*std::min_element(rates.begin(),rates.end());if(!(slowest>0&&std::isfinite(slowest)))throw std::runtime_error("SMOKE calibration rate invalid");result.R=std::max<std::uint64_t>(1,std::uint64_t(std::ceil(slowest*target_seconds)));result.prep_bytes=providers[0]->dynamic_prep_bytes();
  result.marks.resize(std::size_t(workers)*sample_count);result.team_us.resize(sample_count);std::atomic<bool> measuring{true};Barrier ready(workers),begin(workers),finish(workers);threads.clear();
  for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w]{try{if(!set_worker_affinity_r12(w)||!worker_affinity_matches_r12(w))throw std::runtime_error("measurement LP affinity mismatch worker="+std::to_string(w));for(int i=0;i<5;++i)providers[w]->run_once_frontier(w,1,frontier);}catch(const std::exception&e){measuring=false;std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();ready.cancel();}
    if(!ready.wait()||!measuring)return;for(int rep=0;rep<sample_count;++rep){try{workload_margin(*providers[w],w,frontier,qpf);}catch(const std::exception&e){measuring=false;std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();begin.cancel();finish.cancel();return;}if(!begin.wait())return;auto& z=result.marks[std::size_t(rep)*workers+w];z.affinity=worker_affinity_matches_r12(w);z.q0=static_cast<std::int64_t>(qpc());z.t0=tsc();try{for(std::uint64_t j=0;j<result.R;++j)providers[w]->run_once_frontier(w,1,frontier);}catch(const std::exception&e){measuring=false;begin.cancel();finish.cancel();std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();return;}z.t1=tsc();z.q1=static_cast<std::int64_t>(qpc());if(!finish.wait())return;try{workload_margin(*providers[w],w,frontier,qpf);}catch(const std::exception&e){measuring=false;finish.cancel();std::lock_guard<std::mutex>lock(error_mutex);if(result.error.empty())result.error=e.what();return;}}});
  for(auto&t:threads)t.join();if(!measuring)throw std::runtime_error("SMOKE timed route failed: "+result.error);
  for(int rep=0;rep<sample_count;++rep){auto first=result.marks.begin()+std::size_t(rep)*workers,last=first+workers;const auto q0=std::min_element(first,last,[](const Mark&a,const Mark&b){return a.q0<b.q0;})->q0;const auto q1=std::max_element(first,last,[](const Mark&a,const Mark&b){return a.q1<b.q1;})->q1;result.team_us[rep]=seconds(std::uint64_t(q0),std::uint64_t(q1),qpf)*1e6;const std::string event=plan_event(plan_bytes,route.id,frontier,rep+1);std::vector<Mark> marks(first,last);for(unsigned w=0;w<workers;++w)raw<<row_json(route.id,frontier,rep+1,sample_count,event,result.R,result.prep_bytes,marks,qpf,result.team_us[rep],w)<<"\n";raw.flush();}
  if(!raw)throw std::runtime_error("SMOKE raw write failed");const auto med=std::max_element(result.team_us.begin(),result.team_us.end());std::cout<<"SMOKE provider="<<route.id<<" frontier="<<frontier<<" operations="<<(frontier=="dynamic"?"activation_pack_per_call":"prepared_activation_reuse")<<" dynamic_prep_bytes_per_call="<<(frontier=="dynamic"?result.prep_bytes:0)<<" MAC_per_call=524288 workers=8 N="<<sample_count<<" R_common="<<result.R<<" max_team_wall_us="<<*med<<" S12=PASS large_pages=PASS label=SMOKE metrics_used=false\n";return result;
}
}

int run_smoke_r12(const std::string& outdir,const std::string& plan_path,const std::string& verified_plan_sha256,int sample_count){
  if(sample_count!=3&&sample_count!=31)throw std::runtime_error("SMOKE sample_count must be 3 or 31");
  if(!CreateDirectoryA(outdir.c_str(),nullptr))throw std::runtime_error("new attempt directory required; CreateDirectory error="+std::to_string(GetLastError()));
  const std::string raw_path=outdir+"\\harness_raw.jsonl",summary_path=outdir+"\\harness_summary.jsonl";create_new_file(raw_path);create_new_file(summary_path);std::ofstream raw(raw_path,std::ios::binary|std::ios::app),summary(summary_path,std::ios::binary|std::ios::app);if(!raw||!summary)throw std::runtime_error("new harness raw files could not be opened");
  std::string topology_why;if(!discover_topology_r12(topology_why))throw std::runtime_error("P-core preflight failed: "+topology_why);std::cout<<"P_CORE_PREFLIGHT "<<topology_why<<" source=GetLogicalProcessorInformationEx(RelationProcessorCore)\n";write_new(outdir+"\\preflight_r12.json","{\"state\":\"PASS\",\""+topology_json_r12().substr(1));
  verify_plan(plan_path,verified_plan_sha256);const auto plan_bytes=read_file(plan_path);std::cout<<"PLAN_PREFLIGHT sha256="<<verified_plan_sha256<<" canonical=PASS before_measurement=true\n";
  const std::string script=harness_dir()+"\\telemetry_r12.py";invoke_python(script,{"self-test-s20","--output",outdir+"\\s20_integration_qa_r12.txt"});
  std::string page_status;if(!enable_pages_only_if_pre_enabled_r12(page_status)){
    raw<<"{\"label\":\"SMOKE\",\"provider\":\"HARNESS_PREFLIGHT\",\"N\":"<<sample_count<<",\"status\":\"BLOCKED_REAL\",\"system_error\":\""<<page_status<<"\",\"numeric_metrics_used\":false}\n";
    summary<<"{\"label\":\"SMOKE\",\"provider\":\"HARNESS_PREFLIGHT\",\"status\":\"BLOCKED_REAL\",\"system_error\":\""<<page_status<<"\"}\n";raw.flush();summary.flush();raw.close();summary.close();
    const std::string attempt=outdir+"\\d13_attempt_smoke";const auto sid="smoke-r12-blocked-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());invoke_python(script,{"archive-harness","--raw",raw_path,"--summary",summary_path,"--attempt-directory",attempt,"--session-id",sid,"--attempt-id","attempt-001"});
    throw std::runtime_error("LARGE_PAGES real blocker: "+page_status+"; D13=write-once archived");
  }std::cout<<"LARGE_PAGE_PRIVILEGE="<<page_status<<" security_state_changed=false\n";
  LARGE_INTEGER freq{};if(!QueryPerformanceFrequency(&freq))throw std::runtime_error("QueryPerformanceFrequency error="+std::to_string(GetLastError()));
  const Route routes[]={{"omega","",make_omega},{"ggml_flat","",make_ggml_flat},{"ggml_repack","",make_ggml_repack},{"torch_mm","mm",make_torch_out},{"torch_matmul","matmul",make_torch_out},{"torch_linear","linear",make_torch_out},{"torch_mv","mv",make_torch_out}};
  bool all=true;for(const auto& route:routes)for(const std::string frontier:{"prepared","dynamic"}){auto measured=measure_route(route,frontier,plan_bytes,raw,sample_count,freq.QuadPart);const double median=*std::max_element(measured.team_us.begin(),measured.team_us.end());summary<<"{\"label\":\"SMOKE\",\"provider\":\""<<route.id<<"\",\"frontier\":\""<<frontier<<"\",\"O\":1024,\"m\":1,\"N\":"<<sample_count<<",\"R_common\":"<<measured.R<<",\"team_wall_us_max\":"<<std::fixed<<std::setprecision(3)<<median<<",\"dynamic_prep_bytes_per_call\":"<<(frontier=="dynamic"?measured.prep_bytes:0)<<",\"numerical\":\"S12_PASS\",\"status\":\"SMOKE_ONLY\"}\n";summary.flush();}
  if(sample_count==3){const std::string disasm=harness_dir()+"\\logs_r12\\obj_r12\\disassembly_qa_r12.json";const int aux=run_aux_smoke(raw,summary,sample_count,disasm);all&=aux==0;}
  raw.flush();summary.flush();raw.close();summary.close();if(!all)throw std::runtime_error("SMOKE provider gate failed; see raw and summary evidence");
  const std::string attempt=outdir+"\\d13_attempt_smoke";const auto sid="smoke-r12-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());invoke_python(script,{"archive-harness","--raw",raw_path,"--summary",summary_path,"--attempt-directory",attempt,"--session-id",sid,"--attempt-id","attempt-001"});
  std::cout<<"HARNESS_SMOKE_R12 PASS providers=7 frontiers=prepared,dynamic N="<<sample_count<<" MAC_per_cell=512*1024*m D13=write-once S20=unique label=SMOKE numeric_metrics_used=false\n";return 0;
}

int run_aux_smoke_entry_r12(const std::string& outdir,const std::string& disassembly_qa_path){
  if(!CreateDirectoryA(outdir.c_str(),nullptr))throw std::runtime_error("new aux attempt directory required; CreateDirectory error="+std::to_string(GetLastError()));
  std::string topology_why;if(!discover_topology_r12(topology_why))throw std::runtime_error("P-core preflight failed: "+topology_why);std::cout<<"P_CORE_PREFLIGHT "<<topology_why<<" source=GetLogicalProcessorInformationEx(RelationProcessorCore)\n";write_new(outdir+"\\preflight_r12.json","{\"state\":\"PASS\",\""+topology_json_r12().substr(1));
  const std::string raw_path=outdir+"\\harness_raw.jsonl",summary_path=outdir+"\\harness_summary.jsonl";
  create_new_file(raw_path);create_new_file(summary_path);
  std::ofstream raw(raw_path,std::ios::binary|std::ios::app),summary(summary_path,std::ios::binary|std::ios::app);
  if(!raw||!summary)throw std::runtime_error("new aux raw files could not be opened");
  const std::string script=harness_dir()+"\\telemetry_r12.py";
  invoke_python(script,{"self-test-s20","--output",outdir+"\\s20_integration_qa_r12.txt"});
  int code=0;std::string failure;
  try{code=run_aux_smoke(raw,summary,3,disassembly_qa_path);}
  catch(const std::exception&e){failure=e.what();raw<<"{\"label\":\"SMOKE\",\"provider\":\"BW_DRAM\",\"frontier\":\"prepared\",\"N\":3,\"status\":\"BLOCKED_REAL\",\"system_error\":\""<<failure<<"\"}\n";summary<<"{\"label\":\"SMOKE\",\"provider\":\"BW_DRAM\",\"N\":3,\"status\":\"BLOCKED_REAL\",\"system_error\":\""<<failure<<"\"}\n";}
  raw.flush();summary.flush();raw.close();summary.close();
  const std::string attempt=outdir+"\\d13_attempt_aux_smoke";
  const auto sid="aux-smoke-r12-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  invoke_python(script,{"archive-harness","--raw",raw_path,"--summary",summary_path,"--attempt-directory",attempt,"--session-id",sid,"--attempt-id","attempt-001"});
  std::ifstream check(raw_path,std::ios::binary);std::size_t pinstr_rows=0;std::string line;while(std::getline(check,line))if(line.find("\"provider\":\"P_instr\"")!=std::string::npos)++pinstr_rows;
  if(!failure.empty()&&failure.find("SeLockMemoryPrivilege is not already enabled")!=std::string::npos&&pinstr_rows==216){
    std::cout<<"AUX_SMOKE_R12 P_instr=PASS rows="<<pinstr_rows<<" N=3 variants=9 warmup=5 disassembly=REAL_QA_PASS; BW_DRAM=BLOCKED_REAL reason=SeLockMemoryPrivilege_not_enabled D13=write-once PASS\n";return 0;
  }
  if(!failure.empty())throw std::runtime_error("aux SMOKE failed after archival: "+failure+" P_instr_rows="+std::to_string(pinstr_rows));
  if(code!=0)throw std::runtime_error("aux SMOKE returned failure code="+std::to_string(code));
  std::cout<<"AUX_SMOKE_R12 P_instr=PASS BW_DRAM=PASS N=3 D13=write-once PASS\n";return 0;
}
}
