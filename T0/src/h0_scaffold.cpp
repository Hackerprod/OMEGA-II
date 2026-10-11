#define NOMINMAX
#include "h0_scaffold.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace t0h0 {
namespace {

class ScopedAffinity {
public:
    explicit ScopedAffinity(const Core& core) {
        if(core.logical>=sizeof(KAFFINITY)*8)throw std::runtime_error("H0 LP exceeds GROUP_AFFINITY mask");
        GROUP_AFFINITY target{};target.Group=core.group;target.Mask=KAFFINITY(1)<<core.logical;
        if(!SetThreadGroupAffinity(GetCurrentThread(),&target,&old_))throw std::runtime_error("H0 affinity failed for "+core.key);
        active_=true;
    }
    ~ScopedAffinity(){if(active_)SetThreadGroupAffinity(GetCurrentThread(),&old_,nullptr);}
    ScopedAffinity(const ScopedAffinity&)=delete;
    ScopedAffinity& operator=(const ScopedAffinity&)=delete;
private:
    GROUP_AFFINITY old_{};
    bool active_=false;
};

std::vector<std::string> splitCsv(const std::string& line){
    std::vector<std::string> cells;std::string cell;bool quoted=false;
    for(char ch:line){
        if(ch=='"'){quoted=!quoted;continue;}
        if(ch==','&&!quoted){cells.push_back(cell);cell.clear();}else cell.push_back(ch);
    }
    cells.push_back(cell);return cells;
}

std::uint64_t loadOnlyBytes(const std::uint8_t* data,std::size_t bytes){
    if(!data||bytes==0)throw std::invalid_argument("load-only eviction victim must be nonempty");
    const volatile std::uint64_t* words=reinterpret_cast<const volatile std::uint64_t*>(data);
    const std::size_t count=bytes/sizeof(std::uint64_t);std::uint64_t acc=0;
    for(std::size_t i=0;i<count;i+=8)acc+=words[i];
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return acc^static_cast<std::uint64_t>(bytes);
}

double median(std::vector<double> values){
    if(values.empty())throw std::invalid_argument("median requires observations");
    std::sort(values.begin(),values.end());const std::size_t n=values.size();
    return n%2?values[n/2]:(values[n/2-1]+values[n/2])*0.5;
}

void preparationJob(Worker& worker,const EpisodeSpec& spec,bool,void* context){
    const bool collect=*static_cast<const bool*>(context);
    prepareEpisode(worker,spec,collect);
}

} // namespace

std::uint64_t qpcFrequency(){
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("QPC frequency unavailable");
    return static_cast<std::uint64_t>(frequency.QuadPart);
}
std::uint64_t qpcNow(){
    LARGE_INTEGER value{};if(!QueryPerformanceCounter(&value))throw std::runtime_error("QueryPerformanceCounter failed");
    return static_cast<std::uint64_t>(value.QuadPart);
}
std::uint64_t qpcTicksToNs(std::uint64_t ticks,std::uint64_t frequency){
    if(!frequency)throw std::invalid_argument("QPC frequency must be nonzero");
    return static_cast<std::uint64_t>((static_cast<long double>(ticks)*1000000000.0L)/static_cast<long double>(frequency));
}
std::size_t p95NearestRankIndex(std::size_t n){
    if(n==0)throw std::invalid_argument("nearest-rank p95 requires n>0");
    if(n>(std::numeric_limits<std::size_t>::max()-99)/95)throw std::overflow_error("p95 sample count overflow");
    return (95*n+99)/100-1;
}
double sampleCost(std::uint64_t kernel_only_sum_ns,std::uint64_t episodes,std::uint32_t K){
    if(!episodes||!K)throw std::invalid_argument("H0 sample_cost requires positive episode and K counts");
    return static_cast<double>(kernel_only_sum_ns)/(static_cast<double>(episodes)*K);
}
double teamSampleCost(const std::vector<double>& worker_sample_costs){
    if(worker_sample_costs.empty())throw std::invalid_argument("H0 team_sample_cost requires at least one physical worker");
    return *std::max_element(worker_sample_costs.begin(),worker_sample_costs.end());
}
RepSummary summarizeRepCosts(std::vector<double> rep_values){
    if(rep_values.size()!=31)throw std::invalid_argument("H0 primary statistics require exactly 31 repetition-level costs");
    std::sort(rep_values.begin(),rep_values.end());const double med=rep_values[15];
    std::vector<double> deviations;deviations.reserve(rep_values.size());
    for(double value:rep_values)deviations.push_back(std::fabs(value-med));
    std::sort(deviations.begin(),deviations.end());
    return {med,rep_values[22]-rep_values[7],deviations[15],rep_values.front(),rep_values.back()};
}

std::vector<Core> loadPPhysicalCores(const std::string& topology_csv){
    std::ifstream in(topology_csv);if(!in)throw std::runtime_error("cannot read H0 topology CSV: "+topology_csv);
    std::string line;if(!std::getline(in,line))throw std::runtime_error("H0 topology CSV is empty");
    std::vector<Core> cores;std::set<std::string> keys;
    while(std::getline(in,line)){
        const auto c=splitCsv(line);if(c.size()<7)throw std::runtime_error("malformed H0 topology row");
        if(c[5]!="P-core"||!keys.insert(c[2]).second)continue;
        cores.push_back({c[2],c[5],static_cast<std::uint16_t>(std::stoul(c[0])),static_cast<std::uint32_t>(std::stoul(c[1]))});
    }
    std::sort(cores.begin(),cores.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    if(cores.size()!=kPWorkers)throw std::runtime_error("H0 primary requires exactly eight physical P-core API keys");
    return cores;
}

std::vector<Core> loadPLogicalProcessors(const std::string& topology_csv){
    std::ifstream in(topology_csv);if(!in)throw std::runtime_error("cannot read H0 topology CSV: "+topology_csv);
    std::string line;if(!std::getline(in,line))throw std::runtime_error("H0 topology CSV is empty");
    std::vector<Core> logicals;
    while(std::getline(in,line)){
        const auto c=splitCsv(line);if(c.size()<7)throw std::runtime_error("malformed H0 topology row");
        if(c[5]=="P-core")logicals.push_back({c[2],c[5],static_cast<std::uint16_t>(std::stoul(c[0])),static_cast<std::uint32_t>(std::stoul(c[1]))});
    }
    std::sort(logicals.begin(),logicals.end(),[](const Core& a,const Core& b){return a.key==b.key?a.logical<b.logical:a.key<b.key;});
    if(logicals.size()!=16)throw std::runtime_error("SMT characterization requires 16 P-core LPs");
    return logicals;
}

std::vector<Core> loadAllPhysicalCores(const std::string& topology_csv){
    std::ifstream in(topology_csv);if(!in)throw std::runtime_error("cannot read H0 topology CSV: "+topology_csv);
    std::string line;if(!std::getline(in,line))throw std::runtime_error("H0 topology CSV is empty");
    std::vector<Core> cores;std::set<std::string> seen;
    while(std::getline(in,line)){
        const auto c=splitCsv(line);if(c.size()<7)throw std::runtime_error("malformed H0 topology row");
        if(seen.insert(c[2]).second)cores.push_back({c[2],c[5],static_cast<std::uint16_t>(std::stoul(c[0])),static_cast<std::uint32_t>(std::stoul(c[1]))});
    }
    std::sort(cores.begin(),cores.end(),[](const Core& a,const Core& b){return a.key<b.key;});
    if(cores.size()!=16)throw std::runtime_error("H0 descriptive mixed topology requires 16 physical cores");
    return cores;
}

std::vector<std::vector<Core>> loadECoreClusters(const std::string& topology_csv){
    std::ifstream in(topology_csv);if(!in)throw std::runtime_error("cannot read H0 topology CSV: "+topology_csv);
    std::string line;if(!std::getline(in,line))throw std::runtime_error("H0 topology CSV is empty");
    std::map<std::string,std::vector<Core>> groups;std::set<std::string> seen;
    while(std::getline(in,line)){
        const auto c=splitCsv(line);if(c.size()<7)throw std::runtime_error("malformed H0 topology row");
        if(c[5]!="E-core"||!seen.insert(c[2]).second)continue;
        groups[c[6]].push_back({c[2],c[5],static_cast<std::uint16_t>(std::stoul(c[0])),static_cast<std::uint32_t>(std::stoul(c[1]))});
    }
    if(groups.size()!=2)throw std::runtime_error("H0 E characterization expects two L2 clusters");
    std::vector<std::vector<Core>> result;for(auto& item:groups){
        auto& cluster=item.second;std::sort(cluster.begin(),cluster.end(),[](const Core& a,const Core& b){return a.key<b.key;});
        if(cluster.size()!=4)throw std::runtime_error("each E L2 cluster must contain four physical E-cores");
        result.push_back(std::move(cluster));
    }
    return result;
}

ShardGeometry geometryForTarget(std::uint64_t target_bytes){
    static constexpr std::array<ShardGeometry,11> table{{
        {262152,910,512,262080},{294912,1024,512,294912},{524286,1820,512,524160},
        {786438,2731,512,786528},{1048572,3641,512,1048608},{1310724,4551,512,1310688},
        {1572858,5461,512,1572768},{1835010,6372,512,1835136},{2097144,7282,512,2097216},
        {2621448,9102,512,2621376},{3145734,10923,512,3145824}
    }};
    const auto it=std::find_if(table.begin(),table.end(),[&](const ShardGeometry& g){return g.W_target_bytes==target_bytes;});
    if(it==table.end())throw std::invalid_argument("W_target is not one of the MD/30 preregistered points");
    if(it->W_real_bytes!=static_cast<std::uint64_t>(it->O)*512ULL/32ULL*18ULL)throw std::logic_error("MD/30 W geometry table is inconsistent");
    return *it;
}

ShardGeometry geometryForArchitecturalWorkers(std::uint32_t worker_count){
    if(worker_count==4)return {589824,2048,512,589824};
    if(worker_count==8)return {294912,1024,512,294912};
    if(worker_count==16)return {147456,512,512,147456};
    throw std::invalid_argument("architectural W is frozen only for 4, 8, or 16 workers");
}

void EvictionBuffer::allocateAndPretouch(std::uint64_t mib){
    if(mib!=kEvictionMiB&&mib!=kEvictionFallbackMiB)throw std::invalid_argument("H0 eviction buffer must be 8 or 16 MiB");
    requested_bytes=mib*1024ULL*1024ULL;words.resize(static_cast<std::size_t>(requested_bytes/sizeof(std::uint64_t)));
    for(std::size_t i=0;i<words.size();i+=8)words[i]=0x9E3779B97F4A7C15ULL^(static_cast<std::uint64_t>(i)*0xD1B54A32D192ED03ULL);
    for(std::size_t i=1;i<words.size();i+=8)words[i]=0x94D049BB133111EBULL^(static_cast<std::uint64_t>(i)*0xBF58476D1CE4E5B9ULL);
    sink=0;
}

std::uint64_t EvictionBuffer::loadOnlySweep(){
    if(words.empty())throw std::logic_error("H0 eviction buffer has not been allocated/pre-touched");
    const volatile std::uint64_t* p=words.data();std::uint64_t acc=0;
    for(std::size_t i=0;i<words.size();i+=8)acc+=p[i];
    std::atomic_signal_fence(std::memory_order_seq_cst);sink=acc;return acc;
}

void ShardCopies::initialize(const t0q4::WeightMatrix& base,std::uint32_t applications,bool create_b_copies){
    if(applications==0||applications>kMaxK)throw std::invalid_argument("H0 K must be in [1,16]");
    if(base.I!=512||base.O==0||base.groups_per_row!=16||base.payload_bytes()!=static_cast<std::uint64_t>(base.O)*288ULL)
        throw std::invalid_argument("H0 shard must be complete [O,512] Q4 rows per MD/30");
    K=applications;private_copies.clear();shared=base;
    if(create_b_copies&&applications>1)private_copies.assign(applications-1,base);
}

const t0q4::WeightMatrix& ShardCopies::forApplication(Variant variant,std::uint32_t app_index)const{
    if(app_index>=K)throw std::out_of_range("H0 application index exceeds K");
    if(variant!=Variant::B)return shared;
    if(private_copies.size()+1!=K)throw std::logic_error("B requires K independent non-aliasing shard allocations including the base copy");
    return app_index==0?shared:private_copies[app_index-1];
}

bool ShardCopies::bCopiesAreIndependentNonAliasingEqualAndPretouched(){
    if(K==0||private_copies.size()+1!=K)return false;
    std::vector<std::pair<std::uintptr_t,std::uintptr_t>> ranges;ranges.reserve(K);
    const auto touchAndRange=[&](t0q4::WeightMatrix& weights){
        if(weights.storage.empty())return false;
        auto* bytes=reinterpret_cast<volatile std::uint8_t*>(weights.storage.data());
        for(std::size_t offset=0;offset<weights.storage.size();offset+=4096)bytes[offset]=bytes[offset];
        bytes[weights.storage.size()-1]=bytes[weights.storage.size()-1];
        const auto begin=reinterpret_cast<std::uintptr_t>(weights.storage.data());
        if(weights.storage.size()>std::numeric_limits<std::uintptr_t>::max()-begin)return false;
        ranges.emplace_back(begin,begin+weights.storage.size());return true;
    };
    if(!touchAndRange(shared))return false;
    for(auto& copy:private_copies){
        if(copy.storage.size()!=shared.storage.size()||
           std::memcmp(copy.storage.data(),shared.storage.data(),shared.storage.size())!=0||!touchAndRange(copy))return false;
    }
    std::sort(ranges.begin(),ranges.end());
    for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].second)return false;
    for(const auto& copy:private_copies)if(std::memcmp(copy.storage.data(),shared.storage.data(),shared.storage.size())!=0)return false;
    return true;
}

void initializeQ4Worker(Worker& worker,const t0q4::WeightMatrix& base_weights,
                        const t0q4::Activations& activations,std::uint32_t K,bool create_b_copies){
    if(activations.I!=512||activations.M==0||activations.M>32)throw std::invalid_argument("H0 activation geometry must be [m,512], m in {1,2,4,8,16,32}");
    const auto tile=t0q4::kernel_m_tile(activations.M);
    if((activations.M==1&&tile!=1)||(activations.M==2&&tile!=2)||(activations.M>=4&&(activations.M%4||tile!=4)))
        throw std::invalid_argument("H0 m must use the frozen m_tile=1/2/4 contract");
    t0q4::validate_inputs(base_weights,activations);
    worker.shardSet().initialize(base_weights,K,create_b_copies);
    worker.activations=activations;
    t0q4::prepare_timed_output(worker.output,activations.M,base_weights.O);
    worker.kernel=t0q4::select_prevalidated_kernel(t0q4::Route::AVXVNNI256,activations.M);
    worker.application_start=0;worker.application_stride=1;worker.application_count=K;
}

void linkSharedShardSet(Worker& worker,const std::shared_ptr<ShardCopies>& shared,const t0q4::Activations& activations){
    if(!shared||shared->K==0)throw std::invalid_argument("SMT shared shard set must already be initialized");
    if(activations.I!=512||activations.M==0||activations.M>32)throw std::invalid_argument("H0 activation geometry must be [m,512]");
    t0q4::validate_inputs(shared->shared,activations);worker.shared_shards=shared;worker.activations=activations;
    t0q4::prepare_timed_output(worker.output,activations.M,shared->shared.O);
    worker.kernel=t0q4::select_prevalidated_kernel(t0q4::Route::AVXVNNI256,activations.M);
    worker.application_start=0;worker.application_stride=1;worker.application_count=shared->K;
}

void setApplicationPartition(Worker& worker,std::uint32_t total_K,std::uint32_t start,std::uint32_t stride){
    if(total_K==0||total_K>kMaxK||total_K>worker.shardSet().K||stride==0||start>=stride||start>=total_K)
        throw std::invalid_argument("invalid H0 K application partition");
    worker.application_start=start;worker.application_stride=stride;
    worker.application_count=(total_K-start+stride-1)/stride;
}

void prepareEpisode(Worker& worker,const EpisodeSpec& spec,bool collect_eviction_cost){
    if(spec.K==0||spec.K>kMaxK||worker.shardSet().K<spec.K)throw std::invalid_argument("H0 episode K exceeds preallocated shard capacity");
    worker.clocks=AppClockData{};
    const std::uint64_t start=collect_eviction_cost?qpcNow():0;
    worker.clocks.sink=worker.eviction.loadOnlySweep();
    worker.clocks.initial_eviction_sweeps=1;
    if(collect_eviction_cost)worker.clocks.initial_eviction_ns=qpcTicksToNs(qpcNow()-start,qpcFrequency());
}

__declspec(noinline) void runKApplications(Worker& worker,const EpisodeSpec& spec,KernelApplication kernel,void* context,bool collect_qpc){
    if(!kernel)throw std::invalid_argument("H0 application callback is null");
    if(spec.K==0||spec.K>kMaxK||spec.K>worker.shardSet().K)throw std::invalid_argument("H0 K is not prepared");
    if(spec.m!=worker.activations.M)throw std::invalid_argument("H0 m differs from the pre-touched activation matrix");
    if(worker.application_count==0||worker.application_start>=spec.K)throw std::logic_error("H0 application partition was not prepared");
    for(std::uint32_t local=0;local<worker.application_count;++local){
        const std::uint32_t app=worker.application_start+local*worker.application_stride;
        if(app>=spec.K)throw std::logic_error("H0 application partition exceeds K");
        if(spec.variant==Variant::C&&local>0){
            const auto evict_start=collect_qpc?qpcNow():0;
            worker.clocks.sink^=worker.eviction.loadOnlySweep();
            ++worker.clocks.eviction_sweeps;
            if(collect_qpc)worker.clocks.eviction_ns[local]=qpcTicksToNs(qpcNow()-evict_start,qpcFrequency());
        }
        const auto& weights=worker.shardSet().forApplication(spec.variant,app);
        worker.clocks.weight_storage_address[local]=reinterpret_cast<std::uint64_t>(weights.storage.data());
        const auto kernel_start=collect_qpc?qpcNow():0;
        kernel(worker,weights,spec.m,context);
        const auto kernel_end=collect_qpc?qpcNow():0;
        if(collect_qpc)worker.clocks.kernel_only_ns[local]=qpcTicksToNs(kernel_end-kernel_start,qpcFrequency());
        ++worker.clocks.calls;
    }
}

__declspec(noinline) void runQ4KApplications(Worker& worker,const EpisodeSpec& spec,bool collect_qpc){
    if(!worker.kernel)throw std::invalid_argument("H0 VNNI kernel entry must be selected before the timed episode");
    if(spec.K==0||spec.K>kMaxK||spec.K>worker.shardSet().K)throw std::invalid_argument("H0 K is not prepared");
    if(spec.m!=worker.activations.M)throw std::invalid_argument("H0 m differs from the pre-touched activation matrix");
    if(worker.application_count==0||worker.application_start>=spec.K)throw std::logic_error("H0 application partition was not prepared");
    for(std::uint32_t local=0;local<worker.application_count;++local){
        const std::uint32_t app=worker.application_start+local*worker.application_stride;
        if(app>=spec.K)throw std::logic_error("H0 application partition exceeds K");
        if(spec.variant==Variant::C&&local>0){
            const auto evict_start=collect_qpc?qpcNow():0;
            worker.clocks.sink^=worker.eviction.loadOnlySweep();++worker.clocks.eviction_sweeps;
            if(collect_qpc)worker.clocks.eviction_ns[local]=qpcTicksToNs(qpcNow()-evict_start,qpcFrequency());
        }
        const auto& weights=worker.shardSet().forApplication(spec.variant,app);
        const auto kernel_start=collect_qpc?qpcNow():0;
        worker.kernel(weights,worker.activations,worker.output.values.data());
        const auto kernel_end=collect_qpc?qpcNow():0;
        if(collect_qpc)worker.clocks.kernel_only_ns[local]=qpcTicksToNs(kernel_end-kernel_start,qpcFrequency());
        ++worker.clocks.calls;
    }
}

void applyOfficialVnniKernel(Worker& worker,const t0q4::WeightMatrix& weights,std::uint32_t,void*){
    if(!worker.kernel)throw std::logic_error("H0 VNNI kernel entry was not selected before episode timing");
    worker.kernel(weights,worker.activations,worker.output.values.data());
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

void recordPhysicalAllocations(Worker& worker,Variant variant,WorkerSnapshot& result){
    std::uint64_t weights=0;
    const auto& shard=worker.shardSet();
    if(variant==Variant::B){
        weights=shard.shared.physical_allocated_bytes();
        for(const auto& copy:shard.private_copies)weights+=copy.physical_allocated_bytes();
    }else weights=shard.shared.physical_allocated_bytes();
    const auto& representative=shard.shared;
    const std::uint64_t acts=worker.activations.q.capacity()*sizeof(std::int8_t)+worker.activations.scale.capacity()*sizeof(float);
    const std::uint64_t output=worker.output.values.capacity()*sizeof(float);
    result.clocks=worker.clocks;result.W_target_bytes=worker.W_target_bytes;result.W_real_bytes=representative.payload_bytes();
    result.shared_weight_storage_address=reinterpret_cast<std::uint64_t>(representative.storage.data());
    result.O=static_cast<std::uint32_t>(representative.O);result.I=static_cast<std::uint32_t>(representative.I);
    result.q4_weight_physical_bytes=weights;result.q4_activation_bytes=acts;
    result.q4_output_bytes=output;result.eviction_buffer_bytes=worker.eviction.words.capacity()*sizeof(std::uint64_t);
    result.total_physical_bytes=weights+acts+output+result.eviction_buffer_bytes;
}

PersistentWorkerTeam::PersistentWorkerTeam(std::vector<Core> ordered_cores,std::uint64_t eviction_mib){
    if(ordered_cores.empty()||ordered_cores.size()>kMaxWorkers)throw std::invalid_argument("H0 team must contain 1..16 logical workers");
    std::sort(ordered_cores.begin(),ordered_cores.end(),[](const Core& a,const Core& b){return a.key==b.key?a.logical<b.logical:a.key<b.key;});
    workers_.resize(ordered_cores.size());results_.resize(ordered_cores.size());allocation_snapshot_.resize(ordered_cores.size());
    for(std::size_t i=0;i<ordered_cores.size();++i){
        workers_[i].core=std::move(ordered_cores[i]);workers_[i].eviction.allocateAndPretouch(eviction_mib);
    }
    qpc_frequency_=qpcFrequency();threads_.reserve(workers_.size());
    try{
        for(std::size_t i=0;i<workers_.size();++i)threads_.emplace_back([this,i]{workerLoop(i);});
        std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return ready_==workers_.size();});
        for(const auto& r:results_)if(r.error)throw std::runtime_error("H0 worker failed before ready barrier");
    }catch(...){
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;cv_.notify_all();}
        for(auto& t:threads_)if(t.joinable())t.join();
        throw;
    }
}

PersistentWorkerTeam::~PersistentWorkerTeam(){
    {std::lock_guard<std::mutex> lock(mutex_);stop_=true;cv_.notify_all();}
    for(auto& t:threads_)if(t.joinable())t.join();
}

void PersistentWorkerTeam::workerLoop(std::size_t index){
    bool posted=false;
    try{
        ScopedAffinity affinity(workers_[index].core);std::uint64_t seen=0;
        std::unique_lock<std::mutex> lock(mutex_);
        for(;;){
            ++ready_;posted=true;cv_.notify_all();
            cv_.wait(lock,[&]{return stop_||generation_>seen;});
            if(stop_)return;
            seen=generation_;const EpisodeSpec spec=spec_;const bool timed=collect_qpc_;const WorkerJob job=job_;void* context=context_;
            lock.unlock();
            if(job)job(workers_[index],spec,timed,context);
            lock.lock();
            results_[index]=allocation_snapshot_[index];results_[index].clocks=workers_[index].clocks;
            ++done_;posted=false;cv_.notify_all();
        }
    }catch(...){
        std::lock_guard<std::mutex> lock(mutex_);results_[index].error=true;
        if(!posted)++ready_;
        if(done_<workers_.size())++done_;
        cv_.notify_all();
    }
}

TeamResult PersistentWorkerTeam::run(const EpisodeSpec& spec,WorkerJob job,void* context,bool collect_qpc){
    if(!job)throw std::invalid_argument("H0 team job is null");
    std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return ready_==workers_.size();});
    ready_=0;done_=0;spec_=spec;job_=job;context_=context;collect_qpc_=collect_qpc;
    for(std::size_t i=0;i<workers_.size();++i)recordPhysicalAllocations(workers_[i],spec.variant,allocation_snapshot_[i]);
    if(collect_qpc)team_start_qpc_=qpcNow();
    ++generation_;cv_.notify_all();cv_.wait(lock,[&]{return done_==workers_.size();});
    if(collect_qpc)team_end_qpc_=qpcNow();
    TeamResult out{};out.qpc_collected=collect_qpc;
    if(collect_qpc)out.team_wall_ns=qpcTicksToNs(team_end_qpc_-team_start_qpc_,qpc_frequency_);
    out.workers=results_;
    for(const auto& r:out.workers)if(r.error)throw std::runtime_error("H0 worker job failed");
    return out;
}

TeamResult PersistentWorkerTeam::prepareEpisode(const EpisodeSpec& spec,bool collect_eviction_cost){
    bool* collect=&collect_eviction_cost;
    return run(spec,preparationJob,collect,false);
}

EvictionValidationResult runIndependentEvictionTest(const std::vector<Core>& cores,std::uint64_t eviction_mib,
                                                     const std::vector<std::uint8_t>& victim,std::uint32_t warmup,H0Policy policy){
    if(victim.empty()||victim.size()%sizeof(std::uint64_t)!=0)throw std::invalid_argument("eviction-test victim must contain aligned Q4 bytes");
    if(cores.size()!=kPWorkers)throw std::invalid_argument("independent eviction test requires each of the eight P cores");
    EvictionValidationResult result{};result.buffer_mib=eviction_mib;result.cores.reserve(cores.size());
    const auto frequency=qpcFrequency();
    for(const auto& core:cores){
        ScopedAffinity affinity(core);EvictionBuffer buffer{};buffer.allocateAndPretouch(eviction_mib);
        const auto preload=loadOnlyBytes(victim.data(),victim.size());(void)preload;
        EvictionCoreResult row{};row.core=core;row.warm_ns.reserve(policy.N);row.evicted_ns.reserve(policy.N);row.eviction_sweep_ns.reserve(policy.N);
        for(std::uint32_t w=0;w<warmup;++w){(void)loadOnlyBytes(victim.data(),victim.size());(void)buffer.loadOnlySweep();(void)loadOnlyBytes(victim.data(),victim.size());}
        for(std::uint32_t rep=0;rep<policy.N;++rep){
            const auto warm_start=qpcNow();const auto warm_sink=loadOnlyBytes(victim.data(),victim.size());const auto warm_end=qpcNow();
            buffer.sink^=warm_sink;row.warm_ns.push_back(static_cast<double>(qpcTicksToNs(warm_end-warm_start,frequency)));
            const auto sweep_start=qpcNow();const auto sweep_sink=buffer.loadOnlySweep();const auto sweep_end=qpcNow();
            buffer.sink^=sweep_sink;row.eviction_sweep_ns.push_back(static_cast<double>(qpcTicksToNs(sweep_end-sweep_start,frequency)));
            const auto cold_start=qpcNow();const auto cold_sink=loadOnlyBytes(victim.data(),victim.size());const auto cold_end=qpcNow();
            buffer.sink^=cold_sink;row.evicted_ns.push_back(static_cast<double>(qpcTicksToNs(cold_end-cold_start,frequency)));
        }
        row.ratio_median=median(row.evicted_ns)/median(row.warm_ns);result.cores.push_back(std::move(row));
    }
    result.pass=evictionPasses(result);return result;
}

bool evictionPasses(const EvictionValidationResult& result){
    if(result.cores.size()!=kPWorkers)return false;
    for(const auto& core:result.cores)if(core.ratio_median<1.25)return false;
    std::vector<double> ratios;ratios.reserve(result.cores.size());for(const auto& core:result.cores)ratios.push_back(core.ratio_median);
    return median(std::move(ratios))>=1.50;
}

EvictionLadderAction evictionLadderAction(std::uint64_t buffer_mib,bool passed){
    if(buffer_mib==kEvictionMiB)return passed?EvictionLadderAction::Freeze8MiB:EvictionLadderAction::Try16MiB;
    if(buffer_mib==kEvictionFallbackMiB)return passed?EvictionLadderAction::Freeze16MiB:EvictionLadderAction::HoldAfter16MiB;
    throw std::invalid_argument("eviction ladder permits only 8 MiB then 16 MiB per P-core");
}

const char* variantName(Variant variant){
    switch(variant){case Variant::A:return "A";case Variant::B:return "B";case Variant::C:return "C";}
    return "INVALID";
}

SeriesDecision decideSeriesSample(std::uint64_t fixed_episodes,std::uint64_t kernel_only_ns,std::uint32_t attempt,const H0Policy& policy){
    if(fixed_episodes==0||attempt==0||attempt>policy.maximum_technical_retries+1)throw std::invalid_argument("invalid H0 series attempt");
    if(kernel_only_ns>=policy.minimum_kernel_only_ns)return {attempt==1?"SERIES_COMPLETE":"SERIES_RECOVERED_TECHNICAL_RETRY",fixed_episodes,true};
    if(attempt>policy.maximum_technical_retries)return {"SERIES_HOLD",fixed_episodes,false};
    if(fixed_episodes>std::numeric_limits<std::uint64_t>::max()/policy.technical_retry_multiplier)throw std::overflow_error("H0 fixed episode count cannot be safely doubled");
    return {"SERIES_INVALID_TIMER_TOO_SHORT",fixed_episodes*policy.technical_retry_multiplier,false};
}

std::vector<Variant> randomizedVariants(const std::string& codes,std::mt19937_64& rng){
    if(codes.empty()||codes.size()>3)throw std::invalid_argument("variant block must contain one to three A/B/C codes");
    std::vector<Variant> result;result.reserve(codes.size());
    for(char code:codes){
        if(code=='A')result.push_back(Variant::A);
        else if(code=='B')result.push_back(Variant::B);
        else if(code=='C')result.push_back(Variant::C);
        else throw std::invalid_argument("unknown H0 variant code");
    }
    if(result.size()>1)std::shuffle(result.begin(),result.end(),rng);
    return result;
}

void writeVariantOrderManifest(const std::string& blocks_path,const std::string& output_path,std::uint64_t seed){
    std::ifstream in(blocks_path);if(!in)throw std::runtime_error("cannot read H0 variant-order block list");
    if(GetFileAttributesA(output_path.c_str())!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("refusing to overwrite preregistered H0 variant order");
    std::vector<std::pair<std::string,std::string>> blocks;std::string line;
    while(std::getline(in,line)){
        if(line.empty())continue;const auto split=line.find(',');if(split==std::string::npos||split==0||split+1>=line.size())throw std::runtime_error("malformed H0 variant-order block row");
        blocks.emplace_back(line.substr(0,split),line.substr(split+1));
    }
    if(blocks.empty())throw std::runtime_error("H0 variant-order block list is empty");
    std::mt19937_64 rng(seed);std::ofstream out(output_path,std::ios::binary);if(!out)throw std::runtime_error("cannot write H0 variant-order manifest");
    out<<"{\n  \"record\": \"h0_preregistered_variant_order\",\n  \"seed_hex\": \""<<std::uppercase<<std::hex<<seed<<std::dec
       <<"\",\n  \"generator\": \"std::mt19937_64 + std::shuffle per block\",\n  \"measurement\": false,\n  \"blocks\": [\n";
    for(std::size_t i=0;i<blocks.size();++i){
        const auto variants=randomizedVariants(blocks[i].second,rng);
        out<<"    {\"block\": \""<<blocks[i].first<<"\", \"variants\": [";
        for(std::size_t v=0;v<variants.size();++v)out<<(v?", ":"")<<"\""<<variantName(variants[v])<<"\"";
        out<<"]}"<<(i+1==blocks.size()?"\n":",\n");
    }
    out<<"  ]\n}\n";if(!out)throw std::runtime_error("failed writing H0 variant-order manifest");
}

} // namespace t0h0
