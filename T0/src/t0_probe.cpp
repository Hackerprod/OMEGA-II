#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

struct AffinityKey { WORD group; DWORD logical; };
struct AffinityMask { WORD group; KAFFINITY mask; };
struct CoreRec { int api_index; BYTE efficiency; BYTE flags; std::vector<AffinityMask> masks; std::vector<AffinityKey> cpus; };
struct CacheRec { int id; int l2_group; BYTE level; BYTE associativity; WORD line_bytes; DWORD bytes; DWORD type; WORD group_count; std::vector<AffinityMask> masks; std::vector<AffinityKey> cpus; };

static bool queryRelation(LOGICAL_PROCESSOR_RELATIONSHIP relation, std::vector<BYTE>& data) {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(relation, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return false;
    data.resize(bytes);
    return GetLogicalProcessorInformationEx(relation,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()), &bytes) != FALSE;
}

static std::vector<CoreRec> getCores() {
    std::vector<BYTE> data;
    if (!queryRelation(RelationProcessorCore, data)) throw std::runtime_error("GetLogicalProcessorInformationEx(RelationProcessorCore) failed");
    std::vector<CoreRec> result;
    size_t off = 0;
    while (off < data.size()) {
        auto* item = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data() + off);
        if (item->Size == 0 || off + item->Size > data.size()) throw std::runtime_error("invalid processor-core record size");
        if (item->Relationship == RelationProcessorCore) {
            CoreRec c{}; c.api_index = static_cast<int>(result.size());
            c.efficiency = item->Processor.EfficiencyClass; c.flags = item->Processor.Flags;
            const WORD n = item->Processor.GroupCount;
            for (WORD i = 0; i < n; ++i) {
                GROUP_AFFINITY ga = item->Processor.GroupMask[i];
                c.masks.push_back({ga.Group, ga.Mask});
                for (DWORD bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit)
                    if ((ga.Mask >> bit) & 1) c.cpus.push_back({ga.Group, bit});
            }
            result.push_back(c);
        }
        off += item->Size;
    }
    std::sort(result.begin(), result.end(), [](const CoreRec& a, const CoreRec& b) {
        auto key=[](const CoreRec& c){ WORD g=0xffff; DWORD l=0xffffffff; for(auto x:c.cpus){if(x.group<g || (x.group==g && x.logical<l)){g=x.group;l=x.logical;}} return std::pair<WORD,DWORD>(g,l); };
        return key(a) < key(b);
    });
    for (int i=0;i<(int)result.size();++i) result[i].api_index=i;
    return result;
}

static std::vector<CacheRec> getCaches() {
    std::vector<BYTE> data;
    if (!queryRelation(RelationCache, data)) throw std::runtime_error("GetLogicalProcessorInformationEx(RelationCache) failed");
    std::vector<CacheRec> result;
    size_t off=0;
    while(off<data.size()) {
        auto* item=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()+off);
        if(item->Size==0 || off+item->Size>data.size()) throw std::runtime_error("invalid cache record size");
        if(item->Relationship==RelationCache) {
            CacheRec c{}; c.id=static_cast<int>(result.size()); c.l2_group=-1; c.level=item->Cache.Level;
            c.associativity=item->Cache.Associativity; c.line_bytes=item->Cache.LineSize;
            c.bytes=item->Cache.CacheSize; c.type=item->Cache.Type;
            c.group_count=item->Cache.GroupCount;
            // CACHE_RELATIONSHIP in this SDK reports one GROUP_AFFINITY for a cache record.
            GROUP_AFFINITY ga=item->Cache.GroupMask; c.masks.push_back({ga.Group,ga.Mask});
            for(DWORD bit=0;bit<sizeof(KAFFINITY)*8;++bit)
                if((ga.Mask>>bit)&1) c.cpus.push_back({ga.Group,bit});
            std::sort(c.cpus.begin(),c.cpus.end(),[](auto a,auto b){return std::pair<WORD,DWORD>(a.group,a.logical)<std::pair<WORD,DWORD>(b.group,b.logical);});
            result.push_back(c);
        }
        off+=item->Size;
    }
    std::sort(result.begin(),result.end(),[](const CacheRec&a,const CacheRec&b){
        auto key=[](const CacheRec&c){ WORD g=0xffff;DWORD l=0xffffffff;for(auto x:c.cpus)if(x.group<g||(x.group==g&&x.logical<l)){g=x.group;l=x.logical;}return std::tuple<int,WORD,DWORD>(c.level,g,l);};
        return key(a)<key(b);
    });
    for(int i=0;i<(int)result.size();++i) result[i].id=i;
    int l2Index=0;for(auto& c:result)if(c.level==2&&c.type==CacheUnified)c.l2_group=l2Index++;
    return result;
}

static bool contains(const AffinityMask& a, WORD group, DWORD logical) {
    return a.group==group && logical<sizeof(KAFFINITY)*8 && ((a.mask>>logical)&1);
}
static std::string coreKey(const CoreRec& c) {
    std::ostringstream s; s<<"CORE_API_"<<std::setw(2)<<std::setfill('0')<<c.api_index<<"_G";
    if(c.cpus.empty()) return s.str()+"_EMPTY";
    s<<c.cpus[0].group<<"_MASK";
    for(auto x:c.cpus) s<<"_"<<x.logical;
    return s.str();
}
static std::string cacheKey(const CacheRec& c) {
    std::ostringstream s; s<<"L2G"<<std::setw(2)<<std::setfill('0')<<c.l2_group; return s.str();
}

static int cpuid1aType(WORD group, DWORD logical) {
    GROUP_AFFINITY target{}, previous{}; target.Group=group; target.Mask=(KAFFINITY(1)<<logical);
    if(!SetThreadGroupAffinity(GetCurrentThread(),&target,&previous)) return -1;
    int regs[4]={}; __cpuid(regs,0); int maxLeaf=regs[0]; int type=-1;
    if(maxLeaf>=0x1a) { __cpuidex(regs,0x1a,0); type=(int)((unsigned)regs[0]>>24); }
    SetThreadGroupAffinity(GetCurrentThread(),&previous,nullptr);
    return type;
}

static void topology() {
    auto cores=getCores(); auto caches=getCaches();
    std::map<BYTE,std::map<int,int>> classTypes;
    for(auto& c:cores) for(auto cpu:c.cpus) classTypes[c.efficiency][cpuid1aType(cpu.group,cpu.logical)]++;
    std::map<std::pair<WORD,DWORD>,const CoreRec*> cpuToCore;
    for(auto& c:cores) for(auto cpu:c.cpus) cpuToCore[{cpu.group,cpu.logical}]=&c;
    std::vector<const CacheRec*> l2;
    for(auto& c:caches) if(c.level==2 && c.type==CacheUnified) l2.push_back(&c);
    std::cout<<"{\"schema\":\"t0-topology-query-v1\",\"core_records\":"<<cores.size()<<",\"logical_processors\":"<<cpuToCore.size()<<",\"efficiency_class_crosswalk\":[";
    bool first=true;
    for(auto& ec:classTypes){if(!first)std::cout<<",";first=false;std::cout<<"{\"efficiency_class\":"<<(unsigned)ec.first<<",\"native_cpuid_core_types\":[";bool f2=true;for(auto& t:ec.second){if(!f2)std::cout<<",";f2=false;std::cout<<"{\"leaf_1a_type\":"<<t.first<<",\"logical_processor_count\":"<<t.second<<"}";}std::cout<<"]}";}
    std::cout<<"],\"caches\":[";first=true;
    for(auto& c:caches){if(!first)std::cout<<",";first=false;std::cout<<"{\"api_cache_index\":"<<c.id<<",\"l2_group_id\":"<<c.l2_group<<",\"level\":"<<(unsigned)c.level<<",\"type\":"<<c.type<<",\"size_bytes\":"<<c.bytes<<",\"line_size_bytes\":"<<c.line_bytes<<",\"associativity\":"<<(unsigned)c.associativity<<",\"group_count_reported\":"<<c.group_count<<",\"sharing\":[";bool f=true;for(auto x:c.cpus){if(!f)std::cout<<",";f=false;std::cout<<"{\"group\":"<<x.group<<",\"logical_processor\":"<<x.logical<<"}";}std::cout<<"]}";}
    std::cout<<"]}\n";
    std::cout<<"processor_group,logical_processor,physical_core_api_key,efficiency_class,native_cpuid_leaf_1a_type,core_type,l2_group,core_smt_siblings,l2_shared_logical_processors\n";
    for(auto& c:cores) {
        int cpType=c.cpus.empty()?-1:cpuid1aType(c.cpus[0].group,c.cpus[0].logical);
        // Intel CPUID leaf 1Ah native core type: 0x40 = Core (P), 0x20 = Atom (E).
        std::string type=(cpType==0x40?"P-core":(cpType==0x20?"E-core":"UNRESOLVED"));
        std::vector<std::string> sibs;for(auto x:c.cpus){std::ostringstream s;s<<x.group<<":"<<x.logical;sibs.push_back(s.str());}
        for(auto cpu:c.cpus) {
            const CacheRec* match=nullptr;
            for(auto l:l2){bool ok=false;for(auto a:l->masks)if(contains(a,cpu.group,cpu.logical)){ok=true;break;}if(ok){match=l;break;}}
            std::vector<std::string> l2members;if(match)for(auto x:match->cpus){std::ostringstream s;s<<x.group<<":"<<x.logical;l2members.push_back(s.str());}
            std::cout<<cpu.group<<","<<cpu.logical<<","<<coreKey(c)<<","<<(unsigned)c.efficiency<<","<<cpType<<","<<type<<","<<(match?cacheKey(*match):"UNRESOLVED")<<",\"";
            for(size_t i=0;i<sibs.size();++i){if(i)std::cout<<";";std::cout<<sibs[i];}
            std::cout<<"\",\"";for(size_t i=0;i<l2members.size();++i){if(i)std::cout<<";";std::cout<<l2members[i];}std::cout<<"\"\n";
        }
    }
}

static void isa() {
    auto cores=getCores();
    std::cout<<"{\"schema\":\"t0-isa-by-physical-core-v1\",\"cpuid_observations\":[";
    bool first=true;
    for(const auto& core:cores) {
        if(core.cpus.empty()) continue;
        AffinityKey cpu=core.cpus.front(); GROUP_AFFINITY target{},previous{};
        target.Group=cpu.group; target.Mask=(KAFFINITY(1)<<cpu.logical);
        if(!SetThreadGroupAffinity(GetCurrentThread(),&target,&previous)) throw std::runtime_error("SetThreadGroupAffinity failed during CPUID read");
        int r[4]={}; __cpuid(r,0); int maxLeaf=r[0]; char vendor[13]={};memcpy(vendor,&r[1],4);memcpy(vendor+4,&r[3],4);memcpy(vendor+8,&r[2],4);
        __cpuidex(r,1,0); unsigned eax=(unsigned)r[0], ecx=(unsigned)r[2];
        int baseModel=(eax>>4)&15,baseFamily=(eax>>8)&15,extModel=(eax>>16)&15,extFamily=(eax>>20)&255;
        int family=baseFamily==15?baseFamily+extFamily:baseFamily;int model=(baseFamily==6||baseFamily==15)?(extModel<<4)|baseModel:baseModel;
        bool fma=(ecx&(1u<<12))!=0,osxsave=(ecx&(1u<<27))!=0,avx=(ecx&(1u<<28))!=0;
        bool hv=(ecx&(1u<<31))!=0;unsigned long long xcr0=0;if(osxsave)xcr0=_xgetbv(0);
        bool ymm=(xcr0&0x6)==0x6;unsigned ebx7=0,eax71=0,edxExt=0;__cpuid(r,0x80000000);unsigned maxExt=(unsigned)r[0];
        if(maxLeaf>=7){__cpuidex(r,7,0);ebx7=(unsigned)r[1];if((unsigned)r[0]>=1){__cpuidex(r,7,1);eax71=(unsigned)r[0];}}
        if(maxExt>=0x80000001){__cpuidex(r,0x80000001,0);edxExt=(unsigned)r[3];}
        bool avx2=(ebx7&(1u<<5))&&ymm;bool vnni=(eax71&(1u<<4))&&ymm;
        bool avx512f=(ebx7&(1u<<16))!=0;bool zmm=(xcr0&0xe6)==0xe6;
        bool bmi1=(ebx7&(1u<<3))!=0,bmi2=(ebx7&(1u<<8))!=0,rdtscp=(edxExt&(1u<<27))!=0;
        int nativeType=-1;if(maxLeaf>=0x1a){__cpuidex(r,0x1a,0);nativeType=(int)((unsigned)r[0]>>24);}
        SetThreadGroupAffinity(GetCurrentThread(),&previous,nullptr);
        if(!first)std::cout<<",";first=false;
        std::cout<<"{\"physical_core_api_key\":\""<<coreKey(core)<<"\",\"processor_group\":"<<cpu.group<<",\"logical_processor_tested\":"<<cpu.logical<<",\"efficiency_class\":"<<(unsigned)core.efficiency<<",\"native_core_type_leaf_1a\":"<<nativeType<<",\"vendor\":\""<<vendor<<"\",\"family\":"<<family<<",\"model\":"<<model<<",\"stepping\":"<<(eax&15)<<",\"AVX2\":"<<(avx2?"true":"false")<<",\"AVX_VNNI_256\":"<<(vnni?"true":"false")<<",\"AVX512F_CPUID\":"<<(avx512f?"true":"false")<<",\"AVX512_OS_STATE\":"<<(zmm?"true":"false")<<",\"FMA\":"<<(fma?"true":"false")<<",\"BMI1\":"<<(bmi1?"true":"false")<<",\"BMI2\":"<<(bmi2?"true":"false")<<",\"OSXSAVE\":"<<(osxsave?"true":"false")<<",\"AVX\":"<<(avx?"true":"false")<<",\"XCR0\":\"0x"<<std::hex<<xcr0<<std::dec<<"\",\"RDTSCP\":"<<(rdtscp?"true":"false")<<",\"hypervisor_bit\":"<<(hv?"true":"false")<<"}";
    }
    std::cout<<"]}\n";
}

static int timerProbe(WORD group,DWORD logical) {
    GROUP_AFFINITY target{},previous{};target.Group=group;target.Mask=(KAFFINITY(1)<<logical);
    if(!SetThreadGroupAffinity(GetCurrentThread(),&target,&previous)){std::cerr<<"SetThreadGroupAffinity failed "<<GetLastError()<<"\n";return 2;}
    LARGE_INTEGER freq{};QueryPerformanceFrequency(&freq);
    const int N=100000;
    std::vector<long long> qpcPairs; qpcPairs.reserve(N);std::vector<unsigned long long> tscPairs;tscPairs.reserve(N);
    for(int i=0;i<N;++i){LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);QueryPerformanceCounter(&b);qpcPairs.push_back(b.QuadPart-a.QuadPart);}
    const int batchRepeats=21, qpcCallsPerBatch=100000;
    std::vector<double> qpcBatchPerCallNs; qpcBatchPerCallNs.reserve(batchRepeats);
    std::vector<long long> qpcBatchTicks; qpcBatchTicks.reserve(batchRepeats);
    volatile LONGLONG sink=0;
    for(int rep=0;rep<batchRepeats;++rep){LARGE_INTEGER a{},b{},t{};QueryPerformanceCounter(&a);for(int i=0;i<qpcCallsPerBatch;++i){QueryPerformanceCounter(&t);sink^=t.QuadPart;}QueryPerformanceCounter(&b);long long dt=b.QuadPart-a.QuadPart;qpcBatchTicks.push_back(dt);qpcBatchPerCallNs.push_back(1e9*(double)dt/(double)freq.QuadPart/(double)qpcCallsPerBatch);}
    int extRegs[4]={};__cpuid(extRegs,0x80000000);unsigned maxExt=(unsigned)extRegs[0];bool rdtscpAvailable=false;
    if(maxExt>=0x80000001){__cpuidex(extRegs,0x80000001,0);rdtscpAvailable=(((unsigned)extRegs[3]&(1u<<27))!=0);}
    unsigned aux1=0,aux2=0;
    if(rdtscpAvailable)for(int i=0;i<N;++i){_mm_lfence();auto a=__rdtscp(&aux1);_mm_lfence();auto b=__rdtscp(&aux2);_mm_lfence();tscPairs.push_back(b-a);}
    auto median=[](auto v){std::sort(v.begin(),v.end());return v[v.size()/2];};
    long long qpcMin=*std::min_element(qpcPairs.begin(),qpcPairs.end());
    unsigned long long tscMin=rdtscpAvailable?*std::min_element(tscPairs.begin(),tscPairs.end()):0;
    long long qpcMed=median(qpcPairs);auto tscMed=rdtscpAvailable?median(tscPairs):0ULL;double batchMedian=median(qpcBatchPerCallNs);long long batchTickMedian=median(qpcBatchTicks);
    std::cout<<"{\"logical_processor_group\":"<<group<<",\"logical_processor\":"<<logical<<",\"fixed_affinity\":true,\"samples\":"<<N<<",\"qpc_frequency_hz\":"<<freq.QuadPart<<",\"qpc_resolution_ns\":"<<std::setprecision(12)<<(1e9/(double)freq.QuadPart)<<",\"qpc_min_observed_pair_delta_ticks\":"<<qpcMin<<",\"qpc_median_pair_ticks\":"<<qpcMed<<",\"qpc_median_pair_ns\":"<<(1e9*(double)qpcMed/(double)freq.QuadPart)<<",\"qpc_batch_repeats\":"<<batchRepeats<<",\"qpc_calls_per_batch\":"<<qpcCallsPerBatch<<",\"qpc_batch_median_ticks\":"<<batchTickMedian<<",\"qpc_batch_amortized_ns_per_call_including_loop\":"<<batchMedian<<",\"rdtscp_available\":"<<(rdtscpAvailable?"true":"false")<<",\"rdtscp_samples\":"<<tscPairs.size()<<",\"rdtscp_min_pair_cycles\":"<<tscMin<<",\"rdtscp_median_pair_cycles\":"<<tscMed<<",\"rdtscp_aux_last\":"<<aux2<<",\"qpc_batch_sink\":"<<sink<<"}\n";
    SetThreadGroupAffinity(GetCurrentThread(),&previous,nullptr);return 0;
}

int main(int argc,char**argv){try{if(argc==2&&std::string(argv[1])=="--topology"){topology();return 0;}if(argc==2&&std::string(argv[1])=="--isa"){isa();return 0;}if(argc==4&&std::string(argv[1])=="--timer")return timerProbe((WORD)std::stoul(argv[2]),(DWORD)std::stoul(argv[3]));std::cerr<<"usage: t0_probe.exe --topology | --isa | --timer <group> <logical_processor>\n";return 2;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 3;}}
