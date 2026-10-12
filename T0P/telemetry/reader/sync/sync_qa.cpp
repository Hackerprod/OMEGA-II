#include "emon_window_sync.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using emon_sync::WindowSync;
static void rejected(const char* name,const std::function<void()>& f) {
  try { f(); } catch(const std::runtime_error& e) { std::printf("NEGATIVE_%s=PASS reason=%s\n",name,e.what());return; }
  throw std::runtime_error(std::string("negative test unexpectedly passed: ")+name);
}
static std::wstring wide(const char* s){return std::wstring(s,s+std::strlen(s));}
static std::string civil_from_filetime(uint64_t ft){FILETIME f{DWORD(ft),DWORD(ft>>32)},local{};SYSTEMTIME l{};FileTimeToLocalFileTime(&f,&local);FileTimeToSystemTime(&local,&l);char b[32];std::snprintf(b,sizeof(b),"%02u/%02u/%04u %02u:%02u:%02u.%03u",l.wMonth,l.wDay,l.wYear,l.wHour,l.wMinute,l.wSecond,l.wMilliseconds);return b;}

int main(int argc,char**argv) {
 try {
  {
   using emon_sync::WindowInterval; using emon_sync::PaddedBlock;
   PaddedBlock b{0,250,2250,2500}; std::vector<WindowInterval> good;
   for(int i=0;i<40;++i)good.push_back({250.0+50*i,300.0+50*i});
   auto ok=emon_sync::analyze_padded_block(b,good,1.0);
   if(!ok.qualified)throw std::runtime_error("synthetic padded-block positive control failed: "+ok.reason);
   auto crossing=good;crossing[0]={-1,300};auto ex=emon_sync::analyze_padded_block(b,crossing,1.0);
   if(ex.windows_excluded_crossing_pad!=1)throw std::runtime_error("cross-pad window was not excluded/reported");
   auto short_margin=emon_sync::analyze_padded_block(PaddedBlock{100,250,2250,2500},good,1.0);
   if(short_margin.margins_pass)throw std::runtime_error("insufficient margin accepted");
   auto hole=good;hole.erase(hole.begin()+10);auto gh=emon_sync::analyze_padded_block(b,hole,1.0);
   if(gh.gap_pass)throw std::runtime_error("real >20 ms gap accepted");
   auto low=good;low.erase(low.begin()+2,low.begin()+5);auto cv=emon_sync::analyze_padded_block(b,low,1.0);
   if(cv.coverage_pass)throw std::runtime_error("coverage below 95 percent accepted");
   auto dup=good;dup.insert(dup.begin()+1,dup[0]);auto du=emon_sync::analyze_padded_block(b,dup,1.0);
   if(du.order_pass)throw std::runtime_error("duplicate window accepted");
   auto ord=good;std::swap(ord[3],ord[4]);auto oo=emon_sync::analyze_padded_block(b,ord,1.0);
   if(oo.order_pass)throw std::runtime_error("out-of-order window accepted");
   std::printf("PADDED_BLOCK_QA=PASS margin_min_ms=200 positive_coverage=%.3f cross_pad_excluded=%zu\n",ok.coverage_lower,ex.windows_excluded_crossing_pad);
   std::printf("NEGATIVE_CROSS_CONDITION_WINDOW=PASS\nNEGATIVE_MARGIN_INSUFFICIENT=PASS\nNEGATIVE_GAP_GT20MS=PASS\nNEGATIVE_COVERAGE_LT95=PASS\nNEGATIVE_DUPLICATE=PASS\nNEGATIVE_ORDER=PASS\n",0);
  }
  LARGE_INTEGER freq{};QueryPerformanceFrequency(&freq);
  emon_sync::Config cfg{uint64_t(freq.QuadPart),1.0,19.0};
  const uint64_t now=emon_sync::filetime_utc_now(),q=emon_sync::qpc_now();
  {WindowSync s(cfg);rejected("UNOBSERVED_TIMEOUT",[&]{s.sync_begin_block(2);});}
  {WindowSync s(cfg);std::string e;s.feed_line("10/11/2026 20:00:01.000",q,now,&e);rejected("DUPLICATE_MARK",[&]{s.feed_line("10/11/2026 20:00:01.000",q+1,now+10000,&e);s.sync_begin_block(2);});}
  {WindowSync s(cfg);std::string e;s.feed_line("10/11/2026 20:00:02.000",q,now,&e);rejected("INVALID_ORDER",[&]{s.feed_line("10/11/2026 20:00:01.000",q+1,now+10000,&e);s.sync_begin_block(2);});}
  {WindowSync s(cfg);s.process_stopped();rejected("EMON_STOPPED",[&]{s.sync_end_block(2);});}
  {WindowSync s(cfg);std::string e;const auto mark=civil_from_filetime(now-250000);s.feed_line(mark,q,now,&e);if(s.timing_budget_pass()||s.endpoint_gap_upper_ms()<=20)throw std::runtime_error("delay budget test failed");std::printf("NEGATIVE_DELAY_OVER_BUDGET=PASS endpoint_gap_upper_ms=%.3f\n",s.endpoint_gap_upper_ms());}
  std::printf("SYNC_NEGATIVE_QA=PASS\n");
  if(argc==1)return 0;
  if(argc!=6||std::string(argv[1])!="--capture")throw std::runtime_error("usage: sync_qa.exe [--capture emon.exe config.edp workdir raw.txt]");
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd=nullptr,wr=nullptr;
  if(!CreatePipe(&rd,&wr,&sa,0))throw std::runtime_error("CreatePipe failed");SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
  std::wstring emon=wide(argv[2]),edp=wide(argv[3]),wd=wide(argv[4]),raw=wide(argv[5]);
  HANDLE out=CreateFileW(raw.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(out==INVALID_HANDLE_VALUE)throw std::runtime_error("raw capture exists or cannot be created");
  HANDLE err=CreateFileW((raw+L".stderr").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(err==INVALID_HANDLE_VALUE)throw std::runtime_error("stderr capture exists or cannot be created");
  STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=wr;si.hStdError=err;
  PROCESS_INFORMATION pi{};std::wstring cmd=L"\""+emon+L"\" -collect-edp \"edp_file="+edp+L"\"";
  if(!CreateProcessW(emon.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,wd.c_str(),&si,&pi))throw std::runtime_error("CreateProcess EMON failed Win32="+std::to_string(GetLastError()));
  CloseHandle(wr);CloseHandle(err);CloseHandle(pi.hThread);
  WindowSync stream(cfg);std::string line;std::vector<emon_sync::Boundary> seen;char ch;DWORD got=0;
  while(ReadFile(rd,&ch,1,&got,nullptr)&&got){DWORD written=0;WriteFile(out,&ch,1,&written,nullptr);if(ch=='\n'){std::string e;if(stream.feed_line(line,emon_sync::qpc_now(),emon_sync::filetime_utc_now(),&e)){auto all=stream.boundaries();seen.push_back(all.back());}line.clear();}else if(ch!='\r')line.push_back(ch);}
  if(!line.empty()){std::string e;if(stream.feed_line(line,emon_sync::qpc_now(),emon_sync::filetime_utc_now(),&e)){auto all=stream.boundaries();seen.push_back(all.back());}}
  CloseHandle(rd);CloseHandle(out);stream.process_stopped();WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
  if(seen.empty())throw std::runtime_error("no EMON timestamps observed");std::vector<double> delays;for(auto&b:seen)delays.push_back(b.detection_delay_ms);std::sort(delays.begin(),delays.end());
  auto pct=[&](double p){return delays[size_t(p*(delays.size()-1))];};const double med=(delays.size()%2)?delays[delays.size()/2]:(delays[delays.size()/2-1]+delays[delays.size()/2])/2.0,mx=delays.back();
  HANDLE csv=CreateFileW((raw+L".delay.csv").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(csv==INVALID_HANDLE_VALUE)throw std::runtime_error("delay csv already exists or cannot be created");
  const char*head="index,civil_timestamp,qpc_detect,filetime_utc_detect,detection_delay_ms\r\n";DWORD n=0;WriteFile(csv,head,(DWORD)std::strlen(head),&n,nullptr);char row[256];for(size_t i=0;i<seen.size();++i){int len=std::snprintf(row,sizeof(row),"%zu,%s,%llu,%llu,%.6f\r\n",i,seen[i].civil_ms.c_str(),(unsigned long long)seen[i].qpc_detect,(unsigned long long)seen[i].filetime_detect,seen[i].detection_delay_ms);WriteFile(csv,row,(DWORD)len,&n,nullptr);}CloseHandle(csv);
  std::printf("CAPTURE_EXIT=%lu\nCAPTURE_GROUPS=%llu\nREAD_METHOD=stdout anonymous pipe, byte-read line framing\nDETECTION_DELAY_MIN_MS=%.3f\nDETECTION_DELAY_P10_MS=%.3f\nDETECTION_DELAY_P50_MS=%.3f\nDETECTION_DELAY_P90_MS=%.3f\nDETECTION_DELAY_P95_MS=%.3f\nDETECTION_DELAY_P99_MS=%.3f\nDETECTION_DELAY_MAX_MS=%.3f\nALIGNMENT_U_MS=1.000\nS20_ENDPOINT_GAP_UPPER_MS=%.3f\nS20_ENDPOINT_GAP_FORMULA=U+max_detection_delay\nDELAY_SERIES=%ls\nTIMING_BUDGET=%s\n",code,(unsigned long long)seen.size(),delays.front(),pct(.10),med,pct(.90),pct(.95),pct(.99),mx,1.0+mx,(raw+L".delay.csv").c_str(),(1.0+mx<=20)?"PASS":"HOLD_INSTRUMENTATION");
  return code==0?0:2;
 }catch(const std::exception&e){std::fprintf(stderr,"SYNC_QA=HOLD cause=%s\n",e.what());return 2;}
}
