#pragma once

#define NOMINMAX
#include "q4_kernel.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <random>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>
#include <condition_variable>

namespace t0h0 {

constexpr std::size_t kPWorkers=8;
constexpr std::size_t kMaxWorkers=16;
constexpr std::uint32_t kN=31;
constexpr std::uint32_t kWarmup=5;
constexpr std::uint64_t kMinimumKernelOnlyNs=100000000ULL;
constexpr std::uint64_t kCalibrationTargetNs=150000000ULL;
constexpr std::uint64_t kEvictionMiB=8;
constexpr std::uint64_t kEvictionFallbackMiB=16;
constexpr std::uint64_t kH0OrderSeed=0x4D44333048305331ULL;
constexpr std::uint32_t kMaxK=16;

enum class Variant : std::uint8_t { A=0, B=1, C=2 };

struct Core {
    std::string key;
    std::string type;
    std::uint16_t group=0;
    std::uint32_t logical=0;
};

struct EpisodeSpec {
    Variant variant=Variant::A;
    std::uint32_t K=1;
    std::uint32_t m=1;
};

struct ShardGeometry {
    std::uint64_t W_target_bytes=0;
    std::uint32_t O=0;
    std::uint32_t I=512;
    std::uint64_t W_real_bytes=0;
};

struct H0Policy {
    std::uint32_t N=kN;
    std::uint32_t warmup=kWarmup;
    std::uint64_t minimum_kernel_only_ns=kMinimumKernelOnlyNs;
    std::uint64_t calibration_target_ns=kCalibrationTargetNs;
    std::uint32_t technical_retry_multiplier=2;
    std::uint32_t maximum_technical_retries=1;
};

struct CalibrationResult {
    std::uint64_t episodes=0;
    std::uint64_t kernel_only_ns=0;
};

struct RepSummary {
    double median=0,iqr=0,mad=0,min=0,max=0;
};

template<class First,class Second>
void forEachPairedAAStep(std::uint64_t episodes,First&& run_first,Second&& run_second){
    if(!episodes)throw std::invalid_argument("paired A/A schedule requires positive episodes");
    for(std::uint64_t episode=0;episode<episodes;++episode){run_first(episode);run_second(episode);}
}

struct SeriesDecision {
    const char* status="SERIES_COMPLETE";
    std::uint64_t next_episodes=0;
    bool accept_median=false;
};

struct H0AppObservation {
    std::uint32_t worker_index=0;
    std::uint32_t local_app_index=0;
    std::uint32_t global_application_index=0;
    std::uint64_t kernel_only_ns=0;
    std::uint64_t eviction_ns=0;
    std::uint64_t initial_eviction_ns=0;
    std::uint64_t team_wall_ns=0;
    std::uint64_t W_target_bytes=0;
    std::uint64_t W_real_bytes=0;
    std::uint64_t q4_weight_physical_bytes=0;
    std::uint64_t q4_activation_bytes=0;
    std::uint64_t q4_output_bytes=0;
    std::uint64_t eviction_buffer_bytes=0;
    std::uint64_t total_physical_bytes=0;
    std::uint64_t weight_storage_address=0;
    std::uint32_t O=0,I=0;
};

struct H0Sample {
    std::array<std::uint64_t,kMaxWorkers> kernel_only_ns{};
    std::array<std::uint64_t,kMaxWorkers> team_wall_ns{};
    std::array<std::uint64_t,kMaxWorkers> eviction_ns{};
    std::array<std::string,kMaxWorkers> physical_worker_keys{};
    std::uint32_t worker_count=0;
    std::uint64_t episodes=0;
    std::uint64_t team_wall_total_ns=0;
    std::vector<H0AppObservation> applications;
};

struct H0SeriesOutcome {
    std::uint64_t fixed_episodes=0;
    std::uint32_t attempt=0;
    bool recovered=false;
};

struct CellConfig {
    std::string cell_id;
    std::string setup;
    Variant variant=Variant::A;
    std::uint64_t W_target_bytes=0;
    std::uint32_t K=1,m=1;
    std::uint64_t data_seed=0;
    std::uint64_t eviction_mib=kEvictionMiB;
    std::uint64_t calibration_episodes_override=0;
    std::uint64_t calibration_source_episodes_override=0;
    std::uint64_t calibration_kernel_only_ns_override=0;
};

struct AppClockData {
    std::array<std::uint64_t,kMaxK> kernel_only_ns{};
    std::array<std::uint64_t,kMaxK> eviction_ns{};
    std::array<std::uint64_t,kMaxK> weight_storage_address{};
    std::uint64_t initial_eviction_ns=0;
    std::uint64_t calls=0;
    std::uint64_t initial_eviction_sweeps=0;
    std::uint64_t eviction_sweeps=0;
    std::uint64_t sink=0;
};

struct EvictionBuffer {
    std::vector<std::uint64_t> words;
    std::uint64_t sink=0;
    std::uint64_t requested_bytes=0;

    void allocateAndPretouch(std::uint64_t mib);
    std::uint64_t loadOnlySweep();
};

struct ShardCopies {
    t0q4::WeightMatrix shared;
    std::vector<t0q4::WeightMatrix> private_copies;
    std::uint32_t K=0;

    void initialize(const t0q4::WeightMatrix& base,std::uint32_t applications,bool create_b_copies);
    const t0q4::WeightMatrix& forApplication(Variant variant,std::uint32_t app_index) const;
    bool bCopiesAreIndependentNonAliasingEqualAndPretouched();
};

struct Worker {
    Core core;
    std::uint64_t W_target_bytes=0;
    EvictionBuffer eviction;
    ShardCopies shards;
    std::shared_ptr<ShardCopies> shared_shards;
    t0q4::Activations activations;
    t0q4::TimedOutput output;
    t0q4::KernelEntry kernel=nullptr;
    AppClockData clocks;
    std::uint64_t qa_cookie=0;
    std::uint32_t application_start=0;
    std::uint32_t application_stride=1;
    std::uint32_t application_count=0;
    ShardCopies& shardSet() noexcept { return shared_shards?*shared_shards:shards; }
    const ShardCopies& shardSet() const noexcept { return shared_shards?*shared_shards:shards; }
};

using KernelApplication=void(*)(Worker&,const t0q4::WeightMatrix&,std::uint32_t m,void* context);
using WorkerJob=void(*)(Worker&,const EpisodeSpec&,bool collect_qpc,void* context);

struct WorkerSnapshot {
    AppClockData clocks;
    std::uint64_t W_target_bytes=0;
    std::uint64_t W_real_bytes=0;
    std::uint64_t shared_weight_storage_address=0;
    std::uint32_t O=0,I=0;
    std::uint64_t q4_weight_physical_bytes=0;
    std::uint64_t q4_activation_bytes=0;
    std::uint64_t q4_output_bytes=0;
    std::uint64_t eviction_buffer_bytes=0;
    std::uint64_t total_physical_bytes=0;
    bool error=false;
};

struct TeamResult {
    std::uint64_t team_wall_ns=0;
    std::vector<WorkerSnapshot> workers;
    bool qpc_collected=false;
};

struct EvictionCoreResult {
    Core core;
    std::vector<double> warm_ns;
    std::vector<double> evicted_ns;
    std::vector<double> eviction_sweep_ns;
    double ratio_median=0;
};

enum class EvictionLadderAction : std::uint8_t { Freeze8MiB, Try16MiB, Freeze16MiB, HoldAfter16MiB };

struct EvictionValidationResult {
    std::uint64_t buffer_mib=0;
    std::vector<EvictionCoreResult> cores;
    bool pass=false;
};

class PersistentWorkerTeam {
public:
    explicit PersistentWorkerTeam(std::vector<Core> ordered_cores,std::uint64_t eviction_mib=kEvictionMiB);
    ~PersistentWorkerTeam();
    PersistentWorkerTeam(const PersistentWorkerTeam&)=delete;
    PersistentWorkerTeam& operator=(const PersistentWorkerTeam&)=delete;

    std::vector<Worker>& workers() noexcept { return workers_; }
    const std::vector<Worker>& workers() const noexcept { return workers_; }
    TeamResult run(const EpisodeSpec& spec,WorkerJob job,void* context,bool collect_qpc);
    TeamResult prepareEpisode(const EpisodeSpec& spec,bool collect_eviction_cost);

private:
    void workerLoop(std::size_t index);
    std::vector<Worker> workers_;
    std::vector<WorkerSnapshot> results_;
    std::vector<WorkerSnapshot> allocation_snapshot_;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t ready_=0,done_=0;
    std::uint64_t generation_=0;
    bool stop_=false,collect_qpc_=false;
    EpisodeSpec spec_{};
    WorkerJob job_=nullptr;
    void* context_=nullptr;
    std::uint64_t team_start_qpc_=0,team_end_qpc_=0;
    std::uint64_t qpc_frequency_=0;
};

std::vector<Core> loadPPhysicalCores(const std::string& topology_csv);
std::vector<Core> loadPLogicalProcessors(const std::string& topology_csv);
std::vector<Core> loadAllPhysicalCores(const std::string& topology_csv);
std::vector<std::vector<Core>> loadECoreClusters(const std::string& topology_csv);
std::vector<Variant> randomizedVariants(const std::string& codes,std::mt19937_64& rng);
void writeVariantOrderManifest(const std::string& blocks_path,const std::string& output_path,std::uint64_t seed);
ShardGeometry geometryForTarget(std::uint64_t target_bytes);
ShardGeometry geometryForArchitecturalWorkers(std::uint32_t worker_count);
void initializeQ4Worker(Worker& worker,const t0q4::WeightMatrix& base_weights,
                        const t0q4::Activations& activations,std::uint32_t K,bool create_b_copies);
void linkSharedShardSet(Worker& worker,const std::shared_ptr<ShardCopies>& shared,
                        const t0q4::Activations& activations);
void setApplicationPartition(Worker& worker,std::uint32_t total_K,std::uint32_t start,std::uint32_t stride);
void prepareEpisode(Worker& worker,const EpisodeSpec& spec,bool collect_eviction_cost);
__declspec(noinline) void runQ4KApplications(Worker& worker,const EpisodeSpec& spec,bool collect_qpc);
__declspec(noinline) void runKApplications(Worker& worker,const EpisodeSpec& spec,KernelApplication kernel,void* context,bool collect_qpc);
void applyOfficialVnniKernel(Worker& worker,const t0q4::WeightMatrix& weights,std::uint32_t m,void* unused);
void recordPhysicalAllocations(Worker& worker,Variant variant,WorkerSnapshot& result);

// Calibration and retry policy are implemented here; no calibration callback is run by scaffold/QA builds.
template<class MeasureKernelOnly>
CalibrationResult calibrateKernelOnlyEpisodes(MeasureKernelOnly&& measure,
                                               std::uint64_t target_ns=kCalibrationTargetNs,
                                               std::uint64_t max_episodes=0x7fffffffULL) {
    std::uint64_t low=0,high=1;
    std::uint64_t elapsed=measure(high);
    while(elapsed<target_ns){
        low=high;
        if(high>=max_episodes)throw std::runtime_error("H0 engineering calibration episode limit reached");
        high=std::min(max_episodes,high*2);
        elapsed=measure(high);
    }
    while(high-low>1){
        const std::uint64_t mid=low+(high-low)/2;
        const std::uint64_t middle_ns=measure(mid);
        if(middle_ns>=target_ns){high=mid;elapsed=middle_ns;}else low=mid;
    }
    return {high,elapsed};
}

SeriesDecision decideSeriesSample(std::uint64_t fixed_episodes,std::uint64_t kernel_only_ns,
                                 std::uint32_t attempt,const H0Policy& policy);

template<class Warmup,class Measure,class Record>
H0SeriesOutcome runOfficialSeries(std::uint64_t calibrated_episodes,const H0Policy& policy,
                                  Warmup&& warmup,Measure&& measure,Record&& record){
    if(calibrated_episodes==0)throw std::invalid_argument("H0 calibrated episode count must be positive");
    std::uint64_t episodes=calibrated_episodes;
    for(std::uint32_t attempt=1;attempt<=policy.maximum_technical_retries+1;++attempt){
        warmup(episodes,policy.warmup);
        bool any_short=false;
        for(std::uint32_t rep=0;rep<policy.N;++rep){
            H0Sample sample=measure(episodes,rep);
            if(sample.worker_count==0||sample.worker_count>kMaxWorkers)throw std::runtime_error("H0 sample worker count outside fixed result capacity");
            std::uint64_t minimum=std::numeric_limits<std::uint64_t>::max();
            for(std::uint32_t w=0;w<sample.worker_count;++w)minimum=std::min(minimum,sample.kernel_only_ns[w]);
            const bool valid=minimum>=policy.minimum_kernel_only_ns;
            record(sample,attempt,rep,valid);
            any_short=any_short||!valid;
        }
        if(!any_short)return {episodes,attempt,attempt>1};
        if(attempt>policy.maximum_technical_retries)throw std::runtime_error("SERIES_HOLD: second H0 attempt included a kernel-only sample below 100 ms");
        if(episodes>std::numeric_limits<std::uint64_t>::max()/policy.technical_retry_multiplier)
            throw std::overflow_error("H0 technical retry episode count overflow");
        episodes*=policy.technical_retry_multiplier;
    }
    throw std::logic_error("unreachable H0 series retry state");
}

std::uint64_t qpcFrequency();
std::uint64_t qpcNow();
std::uint64_t qpcTicksToNs(std::uint64_t ticks,std::uint64_t frequency);
std::size_t p95NearestRankIndex(std::size_t n);
double sampleCost(std::uint64_t kernel_only_sum_ns,std::uint64_t episodes,std::uint32_t K);
double teamSampleCost(const std::vector<double>& worker_sample_costs);
RepSummary summarizeRepCosts(std::vector<double> rep_values);
EvictionValidationResult runIndependentEvictionTest(const std::vector<Core>& cores,
                                                     std::uint64_t eviction_mib,
                                                     const std::vector<std::uint8_t>& victim,
                                                     std::uint32_t warmup,H0Policy policy);
EvictionValidationResult runIndependentEvictionTest(const std::vector<Core>& cores,
                                                     std::uint64_t eviction_mib,
                                                     const t0q4::WeightMatrix& victim,
                                                     std::uint32_t warmup,H0Policy policy);
void runH0Cell(const std::string& topology_csv,const std::string& output_directory,const CellConfig& config);
void runH0Block(const std::string& topology_csv,const std::string& output_directory,CellConfig block_config,
                const std::vector<Variant>& randomized_variants);
void runIndependentEvictionTestToFile(const std::string& topology_csv,const std::string& output_path,
                                     std::uint64_t eviction_mib,std::uint64_t seed);
bool evictionPasses(const EvictionValidationResult& result);
EvictionLadderAction evictionLadderAction(std::uint64_t buffer_mib,bool passed);
const char* variantName(Variant variant);

} // namespace t0h0
