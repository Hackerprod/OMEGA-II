#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <iomanip>
#include "emon_api.h"

#pragma comment(lib, "kernel32.lib")

namespace {
struct Interval { double a, b; };
struct ValidationInput {
    std::vector<Interval> intervals;
    double start = 0, finish = 2.5, uncertainty = 0.0;
    int windows = 0;
    double denominator = 1.0, value = 1.0, margin_c = 10.0;
    bool required_channel = true, required_core = true, energy_snapshot = false;
    bool active_thermal = false, finite = true;
};

bool validate(const ValidationInput& x, std::string& why) {
    if (!x.required_channel) { why="required channel absent"; return false; }
    if (!x.required_core) { why="required core absent"; return false; }
    if (!x.finite || !std::isfinite(x.start) || !std::isfinite(x.finish) ||
        !std::isfinite(x.value) || !std::isfinite(x.denominator)) {
        why="non-finite value"; return false;
    }
    if (x.denominator == 0) { why="zero denominator: NOT_ESTIMABLE"; return false; }
    if (x.finish <= x.start) { why="invalid temporal order"; return false; }
    if (x.windows < 20) { why="fewer than 20 windows"; return false; }
    double span=x.finish-x.start;
    std::vector<Interval> v=x.intervals;
    for (const auto& i:v) {
        if (!std::isfinite(i.a)||!std::isfinite(i.b)||i.b<=i.a) { why="invalid temporal order"; return false; }
        if (i.b-i.a > .1000000001) { why="window exceeds 100 ms"; return false; }
    }
    for (size_t i=1;i<v.size();++i) {
        if (v[i].a==v[i-1].a && v[i].b==v[i-1].b) { why="duplicate interval"; return false; }
        if (v[i].a < v[i-1].a) { why="interval order is not monotonic"; return false; }
    }
    double covered=0, cursor=x.start, maxgap=0;
    for (const auto& i:v) {
        double a=std::max(x.start,i.a), b=std::min(x.finish,i.b);
        if (b<=a) continue;
        if(a>cursor)maxgap=std::max(maxgap,a-cursor);
        covered+=std::max(0.0,b-std::max(a,cursor));cursor=std::max(cursor,b);
    }
    maxgap=std::max(maxgap,x.finish-cursor);
    // Conservative endpoint alignment: shrink every interval by U and expand block endpoints by U.
    double conservative=std::max(0.0,covered-2.0*x.uncertainty*std::max(1,x.windows));
    double denominator_span=span+2.0*x.uncertainty;
    double coverage=denominator_span>0?conservative/denominator_span:0;
    double conservative_gap=maxgap+2*x.uncertainty;
    if (conservative_gap > .0200000001) { why="gap exceeds 20 ms including endpoints and alignment uncertainty"; return false; }
    if (coverage < .95) { why="coverage below 95 percent"; return false; }
    if (x.energy_snapshot) { why="energy input type confusion: snapshot cannot be treated as INTERVAL_DELTA"; return false; }
    if (x.active_thermal && x.margin_c < 5.0) { why="thermal flag active with insufficient TCC margin"; return false; }
    why="PASS"; return true;
}

ValidationInput good_case() {
    ValidationInput x; x.start=0; x.finish=2.5; x.uncertainty=.00005; x.windows=50;
    for(int k=0;k<50;++k) x.intervals.push_back({k*.05,(k+1)*.05});
    return x;
}
struct Case { const char* name; void(*mutate)(ValidationInput&); };
void miss_channel(ValidationInput&x){x.required_channel=false;}
void miss_core(ValidationInput&x){x.required_core=false;}
void zero_denom(ValidationInput&x){x.denominator=0;}
void nonfinite(ValidationInput&x){x.value=std::numeric_limits<double>::quiet_NaN();}
void duplicate(ValidationInput&x){x.intervals.insert(x.intervals.begin()+3,x.intervals[2]);}
void bad_order(ValidationInput&x){std::swap(x.intervals[1],x.intervals[2]);}
void big_gap(ValidationInput&x){x.intervals.erase(x.intervals.begin()+20);}
void low_coverage(ValidationInput&x){for(size_t k=0;k<42;++k)x.intervals[k].b-=.003;}
void few_windows(ValidationInput&x){x.windows=19;}
void long_window(ValidationInput&x){x.intervals[0].b=.101;}
void energy_confusion(ValidationInput&x){x.energy_snapshot=true;}
void thermal_bad(ValidationInput&x){x.active_thermal=true;x.margin_c=4.9;}

DWORD_PTR affinity_mask(unsigned lp) { return (DWORD_PTR(1) << lp); }
uint64_t qpc_now(){ LARGE_INTEGER q; QueryPerformanceCounter(&q); return (uint64_t)q.QuadPart; }
uint64_t tsc_now(){ unsigned aux=0; return __rdtscp(&aux); }
double qpc_seconds(uint64_t d, LARGE_INTEGER f){return (double)d/(double)f.QuadPart;}

struct Worker { unsigned lp; volatile LONG stop; volatile uint64_t accumulator; HANDLE thread; volatile LONG affinity_state; };
extern "C" uint64_t known_retired_loop(uint64_t iterations);
DWORD WINAPI dummy_worker(LPVOID p) {
    Worker* w=(Worker*)p;
    InterlockedExchange(&w->affinity_state,SetThreadAffinityMask(GetCurrentThread(),affinity_mask(w->lp))?1:0);
    volatile uint64_t x=0x123456789ULL+(uint64_t)w->lp;
    while(InterlockedCompareExchange(&w->stop,0,0)==0) {
        // Frozen DUMMY integer workload: dependent integer additions/xors only.
        x += 0x9E3779B97F4A7C15ULL;
        x ^= (x >> 17);
        x += 0xD1B54A32D192ED03ULL;
        x ^= (x << 13);
        x += 1;
    }
    w->accumulator=x;
    return 0;
}
bool start_workers(std::vector<Worker>& ws,bool use_smt_sibling=false) {
    ws.resize(8);
    for(unsigned i=0;i<8;++i){ ws[i].lp=i*2+(use_smt_sibling?1:0); ws[i].stop=0; ws[i].accumulator=0; ws[i].affinity_state=-1; ws[i].thread=CreateThread(nullptr,0,dummy_worker,&ws[i],CREATE_SUSPENDED,nullptr); if(!ws[i].thread)return false; ResumeThread(ws[i].thread); }
    for(int n=0;n<1000;++n){bool ready=true;for(const auto&w:ws)ready&=w.affinity_state>=0;if(ready)break;Sleep(1);}
    for(const auto&w:ws)if(w.affinity_state!=1)return false;
    return true;
}
void stop_workers(std::vector<Worker>& ws) { for(auto& w:ws) InterlockedExchange(&w.stop,1); for(auto& w:ws) { WaitForSingleObject(w.thread,INFINITE); CloseHandle(w.thread); } }

int negative_tests() {
    const Case cases[]={{"channel_absent",miss_channel},{"core_absent",miss_core},{"denominator_zero",zero_denom},{"non_finite",nonfinite},{"duplicate_interval",duplicate},{"temporal_order",bad_order},{"gap_over_20ms",big_gap},{"coverage_under_95",low_coverage},{"fewer_than_20_windows",few_windows},{"window_over_100ms",long_window},{"snapshot_delta_confusion",energy_confusion},{"thermal_active_margin_lt5",thermal_bad}};
    bool all=true;
    for(const auto& c:cases){auto x=good_case();c.mutate(x);std::string why;bool ok=validate(x,why);bool pass=!ok;printf("NEGATIVE_CASE=%s EXPECT=REJECT ACTUAL=%s REASON=%s RESULT=%s\n",c.name,ok?"ACCEPT":"REJECT",why.c_str(),pass?"PASS":"FAIL");all&=pass;}
    auto z=good_case();z.denominator=0;std::string why;bool valid=validate(z,why);printf("DENOMINATOR_ZERO_RATIO=NOT_ESTIMABLE VALIDATOR=%s\n",valid?"FAIL":"PASS");
    auto overlap=good_case();overlap.intervals[1].a-=.01;std::string ow;bool union_ok=validate(overlap,ow);printf("OVERLAP_UNION EXPECT=PASS ACTUAL=%s REASON=%s RESULT=%s\n",union_ok?"ACCEPT":"REJECT",ow.c_str(),union_ok?"PASS":"FAIL");
    return all&&!valid&&union_ok?0:2;
}

int dummy_run(unsigned seconds,const char* map_path) {
    LARGE_INTEGER qf;QueryPerformanceFrequency(&qf);if(!qf.QuadPart)return 2;
    std::ofstream map(map_path,std::ios::out|std::ios::trunc);if(!map)return 3;
    std::vector<Worker> ws;if(!start_workers(ws))return 4;
    bool affinity=true;for(const auto&w:ws)affinity&=w.affinity_state==1;
    uint64_t q0pre=qpc_now();FILETIME utc0{};GetSystemTimePreciseAsFileTime(&utc0);uint64_t q0post=qpc_now();uint64_t q0=q0pre+(q0post-q0pre)/2;ULARGE_INTEGER u0;u0.LowPart=utc0.dwLowDateTime;u0.HighPart=utc0.dwHighDateTime;
    map<<"# DUMMY=TRUE; QPC-to-UTC calibration during native eight-P integer load; no performance throughput\n";
    map<<"sample,qpc_pre,qpc_post,filetime_100ns,sample_period_ms\n";
    map<<"BEGIN,"<<q0pre<<","<<q0post<<","<<u0.QuadPart<<",0\n";
    uint64_t end=q0+(uint64_t)seconds*(uint64_t)qf.QuadPart;unsigned long long row=0;
    while(qpc_now()<end){uint64_t qpre=qpc_now();FILETIME ft;GetSystemTimePreciseAsFileTime(&ft);uint64_t qpost=qpc_now();ULARGE_INTEGER u;u.LowPart=ft.dwLowDateTime;u.HighPart=ft.dwHighDateTime;map<<row++<<","<<qpre<<","<<qpost<<","<<u.QuadPart<<",5\n";Sleep(5);}
    uint64_t q1pre=qpc_now();FILETIME utc1{};GetSystemTimePreciseAsFileTime(&utc1);uint64_t q1post=qpc_now();uint64_t q1=q1pre+(q1post-q1pre)/2;ULARGE_INTEGER u1;u1.LowPart=utc1.dwLowDateTime;u1.HighPart=utc1.dwHighDateTime;map<<"END,"<<q1pre<<","<<q1post<<","<<u1.QuadPart<<",0\n";stop_workers(ws);
    printf("DUMMY=TRUE workers=8 P_LP=0,2,4,6,8,10,12,14 affinity_all=%s elapsed_qpc_s=%.9f map_rows=%llu map=%s; throughput=NOT_REPORTED\n",affinity?"PASS":"FAIL",qpc_seconds(q1-q0,qf),row,map_path);
    return affinity?0:5;
}

int instruction_test(const char* config,uint64_t iterations,const char* outpath) {
    if(iterations<1000000)return 64;
    LARGE_INTEGER qf;QueryPerformanceFrequency(&qf);std::ofstream out(outpath,std::ios::out|std::ios::trunc);if(!out)return 3;
    std::vector<Worker> ws;if(!start_workers(ws,true))return 4;
    bool pin=SetThreadAffinityMask(GetCurrentThread(),affinity_mask(0))!=0;
    unsigned rc=EMONConfig(config);if(rc){stop_workers(ws);printf("INST_TEST_EMON_CONFIG=%u\n",rc);return 10;}
    rc=EMONStart();if(rc){EMONStop();stop_workers(ws);printf("INST_TEST_EMON_START=%u\n",rc);return 11;}
    rc=EMONMarkerCreate("known_retired_region");
    if(rc){EMONStop();stop_workers(ws);printf("INST_TEST_MARKER_CREATE=%u\n",rc);return 12;}
    rc=EMONMarkerEnter("known_retired_region");if(rc){EMONStop();stop_workers(ws);printf("INST_TEST_MARKER_ENTER=%u\n",rc);return 13;}
    volatile uint64_t sink=known_retired_loop(iterations);
    rc=EMONMarkerLeave("known_retired_region");if(rc){EMONStop();stop_workers(ws);printf("INST_TEST_MARKER_LEAVE=%u\n",rc);return 14;}
    EMON_DATA data=nullptr;rc=EMONReadMarkerCounts("known_retired_region",&data);
    double* counts=nullptr;unsigned count=0;unsigned grc=rc?rc:EMONDataGetSingleEntry(data,"INST_RETIRED.ANY",&counts,&count);
    double measured=(grc==0&&count>0&&counts)?counts[0]:std::numeric_limits<double>::quiet_NaN();
    uint64_t expected=3*iterations+4;double rel=fabs(measured-(double)expected)/(double)expected;
    out<<"DUMMY=TRUE; throughput=NOT_REPORTED\n"<<"LP=0; affinity_ok="<<(pin?1:0)<<"; iterations="<<iterations<<"; expected_instructions="<<expected<<"; measured_INST_RETIRED_ANY="<<std::setprecision(15)<<measured<<"; relative_error="<<rel<<"; threshold=0.01; marker_status="<<grc<<"\n";
    if(data)EMONDataFree(&data);EMONStop();stop_workers(ws);
    printf("INST_RETIRED known=%llu measured=%.0f relative_error=%.9f threshold=0.01 result=%s raw=%s sink=%llu\n",(unsigned long long)expected,measured,rel,(pin&&grc==0&&std::isfinite(measured)&&rel<=.01)?"PASS":"FAIL/HOLD",outpath,(unsigned long long)sink);
    return pin&&grc==0&&std::isfinite(measured)&&rel<=.01?0:15;
}

int d1(double configured_mhz, const char* outpath) {
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf); if(!qf.QuadPart)return 2;
    std::ofstream out(outpath,std::ios::out|std::ios::trunc); if(!out)return 3;
    out<<"# DUMMY=TRUE; source=RDTSCP+QPC; N=31; duration>=2s; eight concurrent native integer loops pinned to first P LP; no OMEGA\n";
    out<<"core,lp,n,qpc_s,qpc_ticks,tsc_delta,tsc_mhz,configured_mhz,relative_error,affinity_ok\n"<<std::setprecision(15);
    std::mutex out_mutex;std::vector<double> all;all.reserve(248);std::vector<std::thread> threads;
    for(unsigned c=0;c<8;++c)threads.emplace_back([&,c]{unsigned lp=c*2;bool aff=SetThreadAffinityMask(GetCurrentThread(),affinity_mask(lp))!=0;volatile uint64_t x=0x123456789ULL+c;
        for(int n=1;n<=31;++n){uint64_t q0=qpc_now(),t0=tsc_now(),qnow=q0;do{for(unsigned k=0;k<100000;++k){x+=0x9E3779B97F4A7C15ULL;x^=(x>>17);x+=0xD1B54A32D192ED03ULL;x^=(x<<13);}qnow=qpc_now();}while(qnow-q0<2ULL*(uint64_t)qf.QuadPart);uint64_t t1=tsc_now(),q1=qpc_now();double sec=qpc_seconds(q1-q0,qf),mhz=(double)(t1-t0)/sec/1.0e6,err=fabs(mhz-configured_mhz)/configured_mhz;std::lock_guard<std::mutex> lock(out_mutex);out<<c<<","<<lp<<","<<n<<","<<sec<<","<<(q1-q0)<<","<<(t1-t0)<<","<<mhz<<","<<configured_mhz<<","<<err<<","<<(aff?1:0)<<"\n";all.push_back(err);}
        (void)x;
    });
    for(auto&t:threads)t.join();
    double mx=*std::max_element(all.begin(),all.end()), mean=0;for(double v:all)mean+=v;mean/=all.size();
    printf("D1 N=%zu per_core=31 min_duration_s=2 configured_mhz=%.9f mean_relative_error=%.12f max_relative_error=%.12f threshold=0.001 result=%s\n",all.size(),configured_mhz,mean,mx,mx<=.001?"PASS":"FAIL");
    printf("D1_RAW=%s\n",outpath); return mx<=.001?0:5;
}

// Deliberately bounded API timestamp probe: each read is bracketed by QPC and RDTSCP.
int api_probe(const char* config, const char* outpath, int seconds) {
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    std::ofstream out(outpath,std::ios::out|std::ios::trunc); if(!out)return 3;
    out<<"# DUMMY=TRUE; API timestamps are empirical; qpc and tsc brackets recorded per EMONReadCounts\n";
    out<<"sample,qpc_pre,qpc_post,tsc_pre,emon_timestamp,tsc_post,read_status,read_duration_qpc_ticks\n";
    unsigned rc=EMONConfig(config);printf("EMONConfig=%u\n",rc);if(rc)return 10;
    rc=EMONStart();printf("EMONStart=%u\n",rc);if(rc){EMONStop();return 11;}
    uint64_t stop=qpc_now()+(uint64_t)seconds*(uint64_t)qf.QuadPart;int sample=0;
    while(qpc_now()<stop){
        EMON_DATA data=nullptr;uint64_t q0=qpc_now(),t0=tsc_now();unsigned readrc=EMONReadCounts(&data);uint64_t t1=tsc_now(),q1=qpc_now(),stamp=0;
        unsigned trc=readrc?readrc:EMONDataGetTimestamp(data,(unsigned long long*)&stamp);
        out<<sample++<<","<<q0<<","<<q1<<","<<t0<<","<<stamp<<","<<t1<<","<<trc<<","<<(q1-q0)<<"\n";
        if(data)EMONDataFree(&data);
        Sleep(50);
    }
    EMONStop();printf("API_PROBE samples=%d qpc_hz=%lld raw=%s\n",sample,qf.QuadPart,outpath);return sample>=20?0:12;
}

int api_d2(const char* config,const char* outpath,int intervals,double tsc_mhz) {
    LARGE_INTEGER qf;QueryPerformanceFrequency(&qf);std::ofstream out(outpath,std::ios::out|std::ios::trunc);if(!out)return 3;
    std::vector<Worker> ws;if(!start_workers(ws))return 4;
    unsigned rc=EMONConfig(config);printf("EMONConfig=%u\n",rc);if(rc){stop_workers(ws);return 10;}
    rc=EMONStart();printf("EMONStart=%u\n",rc);if(rc){EMONStop();stop_workers(ws);return 11;}
    out<<"# DUMMY=TRUE; 8 fixed P LP workers; API PMU counts; interval>=2s; raw counts and QPC/API timestamps\n";
    out<<"interval,lp,ref_start,ref_end,thread_start,thread_end,dt_s,ref_ratio,thread_wall,api_ts_start,api_ts_end,qpc_start_pre,qpc_start_post,qpc_end_pre,qpc_end_post,api_read_bracket_max_ms\n";
    auto read=[&](std::vector<double>& ref,std::vector<double>& thr,uint64_t& stamp,uint64_t& qpre,uint64_t& qpost,double& uncertainty_ms)->bool{
        EMON_DATA d=nullptr;uint64_t q0=qpc_now();unsigned r=EMONReadCounts(&d);uint64_t q1=qpc_now();
        if(r||!d)return false;unsigned tr=EMONDataGetTimestamp(d,(unsigned long long*)&stamp);
        double *a=nullptr,*b=nullptr;unsigned na=0,nb=0;
        unsigned ra=EMONDataGetSingleEntry(d,"CPU_CLK_UNHALTED.REF_TSC",&a,&na),rb=EMONDataGetSingleEntry(d,"CPU_CLK_UNHALTED.THREAD",&b,&nb);
        bool ok=!tr&&!ra&&!rb&&na>=16&&nb>=16;
        if(ok){ref.assign(a,a+na);thr.assign(b,b+nb);} qpre=q0;qpost=q1; uncertainty_ms=1000.0*(double)(q1-q0)/(double)qf.QuadPart;
        EMONDataFree(&d);return ok;
    };
    std::vector<double> r0,t0,r1,t1;uint64_t s0=0,s1=0,q0pre=0,q0post=0,q1pre=0,q1post=0;double u0=0,u1=0;
    bool all_ok=read(r0,t0,s0,q0pre,q0post,u0);if(!all_ok){EMONStop();stop_workers(ws);printf("API_READ_FAIL=initial snapshot unavailable\n");return 12;}
    for(int n=0;n<intervals;++n){Sleep(2000);bool read_ok=read(r1,t1,s1,q1pre,q1post,u1);if(!read_ok){printf("API_READ_FAIL=interval=%d\n",n);all_ok=false;break;}
        double dt=qpc_seconds(((q1pre+q1post)/2)-((q0pre+q0post)/2),qf);
        for(unsigned c=0;c<8;++c){unsigned lp=c*2;if(lp>=r0.size()||lp>=r1.size()){all_ok=false;break;}
            double dr=r1[lp]-r0[lp],dth=t1[lp]-t0[lp],ratio=dr/(tsc_mhz*1.0e6*dt),fwall=dth/dt;
            out<<n<<","<<lp<<","<<r0[lp]<<","<<r1[lp]<<","<<t0[lp]<<","<<t1[lp]<<","<<dt<<","<<ratio<<","<<fwall<<","<<s0<<","<<s1<<","<<q0pre<<","<<q0post<<","<<q1pre<<","<<q1post<<","<<std::max(u0,u1)<<"\n";
            if(!std::isfinite(ratio)||ratio<.98||ratio>1.02)all_ok=false;
        }
        r0.swap(r1);t0.swap(t1);s0=s1;q0pre=q1pre;q0post=q1post;u0=u1;
    }
    EMONStop();stop_workers(ws);
    printf("D2_API intervals=%d cores=8 tsc_mhz_calibrated=%.6f event_group=THREAD,REF_TSC; raw=%s; verdict=%s\n",intervals,tsc_mhz,outpath,all_ok?"PASS":"FAIL/HOLD");return all_ok?0:13;
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc<2){fwprintf(stderr,L"usage: telemetry_reader.exe --negative-tests | --dummy seconds clock-map.csv | --d1 configured_MHz raw.csv | --api-probe config.xml raw.csv seconds | --api-d2 config.xml raw.csv N calibrated_MHz | --inst-test config.xml iterations raw.txt\n");return 64;}
    std::wstring mode=argv[1];
    if(mode==L"--negative-tests")return negative_tests();
    if(mode==L"--dummy"&&argc>=4){char o[MAX_PATH];WideCharToMultiByte(CP_UTF8,0,argv[3],-1,o,MAX_PATH,nullptr,nullptr);return dummy_run(_wtoi(argv[2]),o);}
    if(mode==L"--inst-test"&&argc>=5){char c[MAX_PATH],o[MAX_PATH];WideCharToMultiByte(CP_UTF8,0,argv[2],-1,c,MAX_PATH,nullptr,nullptr);WideCharToMultiByte(CP_UTF8,0,argv[4],-1,o,MAX_PATH,nullptr,nullptr);return instruction_test(c,_wtoi64(argv[3]),o);}
    if(mode==L"--d1"&&argc>=4){char o[MAX_PATH];WideCharToMultiByte(CP_UTF8,0,argv[3],-1,o,MAX_PATH,nullptr,nullptr);return d1(_wtof(argv[2]),o);}
    if(mode==L"--api-probe"&&argc>=5){char c[MAX_PATH],o[MAX_PATH];WideCharToMultiByte(CP_UTF8,0,argv[2],-1,c,MAX_PATH,nullptr,nullptr);WideCharToMultiByte(CP_UTF8,0,argv[3],-1,o,MAX_PATH,nullptr,nullptr);return api_probe(c,o,_wtoi(argv[4]));}
    if(mode==L"--api-d2"&&argc>=6){char c[MAX_PATH],o[MAX_PATH];WideCharToMultiByte(CP_UTF8,0,argv[2],-1,c,MAX_PATH,nullptr,nullptr);WideCharToMultiByte(CP_UTF8,0,argv[3],-1,o,MAX_PATH,nullptr,nullptr);return api_d2(c,o,_wtoi(argv[4]),_wtof(argv[5]));}
    fwprintf(stderr,L"invalid arguments\n");return 64;
}
