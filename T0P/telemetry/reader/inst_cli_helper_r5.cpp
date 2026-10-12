#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
extern "C" uint64_t known_retired_loop(uint64_t iterations);
struct Worker { unsigned lp; volatile LONG stop; HANDLE thread; volatile LONG affinity; };
struct MapState { volatile LONG stop; FILE* file; };
static DWORD_PTR mask_for(unsigned lp){return (DWORD_PTR(1)<<lp);}
static DWORD WINAPI worker_proc(LPVOID p){Worker*w=(Worker*)p;InterlockedExchange(&w->affinity,SetThreadAffinityMask(GetCurrentThread(),mask_for(w->lp))?1:0);volatile uint64_t x=0x123456789ULL+w->lp;while(InterlockedCompareExchange(&w->stop,0,0)==0){x+=0x9E3779B97F4A7C15ULL;x^=(x>>17);x+=0xD1B54A32D192ED03ULL;x^=(x<<13);x+=1;}return(DWORD)(x&0);}
static uint64_t qpc(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return(uint64_t)q.QuadPart;}
static uint64_t utc(){FILETIME f;GetSystemTimePreciseAsFileTime(&f);ULARGE_INTEGER u;u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart;}
static void marker(FILE*f,const char*name,unsigned long long i,uint64_t a,uint64_t b){fprintf(f,"%s,%llu,%llu,%llu,%llu\n",name,i,(unsigned long long)a,(unsigned long long)b,(unsigned long long)utc());fflush(f);}
static DWORD WINAPI map_proc(LPVOID p){MapState*m=(MapState*)p;SetThreadAffinityMask(GetCurrentThread(),mask_for(16));unsigned long long n=0;while(InterlockedCompareExchange(&m->stop,0,0)==0){uint64_t a=qpc(),u=utc(),b=qpc();fprintf(m->file,"%llu,%llu,%llu,%llu\n",n++,(unsigned long long)a,(unsigned long long)b,(unsigned long long)u);fflush(m->file);Sleep(250);}return 0;}
static void wait_ticks(uint64_t deadline){while(qpc()<deadline){uint64_t left=deadline-qpc();if(left>(uint64_t)100*1000)Sleep(1);}}
int main(){const uint64_t iterations=20000000000ULL,expected=3*iterations+4;const unsigned reps=5;LARGE_INTEGER qf;if(!QueryPerformanceFrequency(&qf))return 2;
 FILE*marks=fopen("logs\\inst_cli_markers_r5.csv","w");FILE*map=fopen("logs\\inst_cli_qpc_utc_map_r5.csv","w");if(!marks||!map)return 3;
 fprintf(marks,"mark,rep,qpc_start,qpc_end,utc_end_filetime_100ns\n");fprintf(map,"sample,qpc_pre,qpc_post,filetime_100ns\n");
 Worker ws[7];for(unsigned i=0;i<7;++i){ws[i].lp=2*(i+1);ws[i].stop=0;ws[i].affinity=-1;ws[i].thread=CreateThread(nullptr,0,worker_proc,&ws[i],0,nullptr);if(!ws[i].thread)return 4;}Sleep(100);bool workers_ok=true;for(auto&w:ws)workers_ok&=(w.affinity==1);bool main_pin=SetThreadAffinityMask(GetCurrentThread(),mask_for(0))!=0;if(!main_pin||!workers_ok)return 5;
 MapState ms;ms.stop=0;ms.file=map;HANDLE mt=CreateThread(nullptr,0,map_proc,&ms,0,nullptr);if(!mt)return 6;uint64_t total=0;bool sinks_ok=true;
 for(unsigned r=1;r<=reps;++r){uint64_t a=qpc();marker(marks,"REP_START",r,a,a);uint64_t sink=known_retired_loop(iterations);uint64_t b=qpc();marker(marks,"REP_END",r,a,b);sinks_ok&=(sink==iterations);total+=b-a;printf("REP%u expected=%llu sink=%llu qpc_start=%llu qpc_end=%llu duration_s=%.9f\n",r,(unsigned long long)expected,(unsigned long long)sink,(unsigned long long)a,(unsigned long long)b,(double)(b-a)/(double)qf.QuadPart);if(r<reps)Sleep(750);}
 uint64_t baseTicks=total/reps;Sleep(750);uint64_t ba=qpc();marker(marks,"BASE_START",0,ba,ba);wait_ticks(ba+baseTicks);uint64_t bb=qpc();marker(marks,"BASE_END",0,ba,bb);printf("BASE qpc_start=%llu qpc_end=%llu duration_s=%.9f target_mean_loop_s=%.9f\n",(unsigned long long)ba,(unsigned long long)bb,(double)(bb-ba)/(double)qf.QuadPart,(double)baseTicks/(double)qf.QuadPart);
 InterlockedExchange(&ms.stop,1);WaitForSingleObject(mt,INFINITE);CloseHandle(mt);for(auto&w:ws)InterlockedExchange(&w.stop,1);for(auto&w:ws){WaitForSingleObject(w.thread,INFINITE);CloseHandle(w.thread);}printf("DUMMY=TRUE; throughput=NOT_REPORTED; expected=%llu; reps=%u; N=%llu; main_LP=0; affinity=%s; other_P_worker_LPs=2,4,6,8,10,12,14; worker_affinity=%s; SMT_sibling_counts=NOT_MEASURED_SEPARATELY; BASE=empty fixed-LP region, target duration=mean of five REP durations; pauses_ms=750\n",(unsigned long long)expected,reps,(unsigned long long)iterations,main_pin?"PASS":"FAIL",workers_ok?"PASS":"FAIL");fclose(marks);fclose(map);return(sinks_ok&&main_pin&&workers_ok)?0:7;}

