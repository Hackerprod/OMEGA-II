#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

extern "C" uint64_t known_retired_loop(uint64_t iterations);

struct Worker { unsigned lp; volatile LONG stop; HANDLE thread; volatile LONG affinity; };
static DWORD_PTR mask_for(unsigned lp) { return (DWORD_PTR(1) << lp); }
static DWORD WINAPI worker_proc(LPVOID p) {
    Worker* w = (Worker*)p;
    InterlockedExchange(&w->affinity, SetThreadAffinityMask(GetCurrentThread(), mask_for(w->lp)) ? 1 : 0);
    volatile uint64_t x = 0x123456789ULL + w->lp;
    while (InterlockedCompareExchange(&w->stop, 0, 0) == 0) {
        x += 0x9E3779B97F4A7C15ULL; x ^= (x >> 17);
        x += 0xD1B54A32D192ED03ULL; x ^= (x << 13); x += 1;
    }
    return (DWORD)(x & 0);
}
static uint64_t qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return (uint64_t)q.QuadPart; }
static uint64_t filetime() { FILETIME f; GetSystemTimePreciseAsFileTime(&f); ULARGE_INTEGER u; u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart; }
static void map_row(FILE* f, unsigned long long n) {
    uint64_t a=qpc(), utc=filetime(), b=qpc();
    fprintf(f,"%llu,%llu,%llu,%llu,5\n",n,(unsigned long long)a,(unsigned long long)b,(unsigned long long)utc);
}
extern "C" int main() {
    const uint64_t iterations=100000000ULL, expected=3*iterations+4;
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    FILE* f=fopen("..\\logs\\inst_cli_qpc_utc_map_r4.csv","w");
    if(!f) return 2;
    fprintf(f,"sample,qpc_pre,qpc_post,filetime_100ns,sample_period_ms\n");
    Worker ws[7];
    for(unsigned i=0;i<7;++i){ws[i].lp=2*(i+1);ws[i].stop=0;ws[i].affinity=-1;ws[i].thread=CreateThread(nullptr,0,worker_proc,&ws[i],0,nullptr);if(!ws[i].thread)return 3;}
    Sleep(100);
    bool workers_ok=true;for(auto& w:ws)workers_ok&=(w.affinity==1);
    bool main_pin=SetThreadAffinityMask(GetCurrentThread(),mask_for(0))!=0;
    uint64_t a=qpc(),u=filetime(),b=qpc();fprintf(f,"BEGIN,%llu,%llu,%llu,0\n",(unsigned long long)a,(unsigned long long)b,(unsigned long long)u);
    const uint64_t stop=qpc()+(uint64_t)8*qf.QuadPart;unsigned long long n=0;
    while(qpc()<stop){map_row(f,n++);Sleep(5);}
    uint64_t q0=qpc(),u0=filetime(),q1=qpc();fprintf(f,"LOOP_START,%llu,%llu,%llu,0\n",(unsigned long long)q0,(unsigned long long)q1,(unsigned long long)u0);
    uint64_t sink=known_retired_loop(iterations);
    uint64_t q2=qpc(),u1=filetime(),q3=qpc();fprintf(f,"LOOP_END,%llu,%llu,%llu,0\n",(unsigned long long)q2,(unsigned long long)q3,(unsigned long long)u1);
    for(auto& w:ws)InterlockedExchange(&w.stop,1);
    for(auto& w:ws){WaitForSingleObject(w.thread,INFINITE);CloseHandle(w.thread);}
    uint64_t c=qpc(),v=filetime(),d=qpc();fprintf(f,"END,%llu,%llu,%llu,0\n",(unsigned long long)c,(unsigned long long)d,(unsigned long long)v);fclose(f);
    printf("DUMMY=TRUE; throughput=NOT_REPORTED; expected=%llu; sink=%llu; loop_qpc_start=%llu; loop_qpc_end=%llu; loop_s=%.9f; main_affinity=%s; other_P_workers=%s; LPs=2,4,6,8,10,12,14; map=logs\\inst_cli_qpc_utc_map_r4.csv\n",(unsigned long long)expected,(unsigned long long)sink,(unsigned long long)q0,(unsigned long long)q2,(double)(q2-q0)/(double)qf.QuadPart,main_pin?"PASS":"FAIL",workers_ok?"PASS":"FAIL");
    return (main_pin&&workers_ok&&sink==iterations)?0:4;
}
