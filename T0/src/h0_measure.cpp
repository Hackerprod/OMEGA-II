#define NOMINMAX
#include "h0_scaffold.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace t0h0 {
namespace {

struct EpisodeBatch {
    H0Sample sample;
};

std::uint64_t fnv1a(const std::string& text){
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char c:text){h^=c;h*=1099511628211ULL;}
    return h;
}

std::uint64_t workerSeed(std::uint64_t seed,const std::string& core_key){return seed^fnv1a(core_key);}

std::vector<Core> selectWorkers(const std::string& topology,const std::string& setup){
    if(setup=="P8")return loadPPhysicalCores(topology);
    if(setup=="P4"){
        auto p=loadPPhysicalCores(topology);p.resize(4);return p;
    }
    if(setup=="E0"||setup=="E1"){
        auto clusters=loadECoreClusters(topology);return clusters[setup=="E0"?0:1];
    }
    if(setup=="PplusE")return loadAllPhysicalCores(topology);
    if(setup=="SMT16")return loadPLogicalProcessors(topology);
    throw std::invalid_argument("unknown H0 worker setup; use P8, P4, E0, E1, PplusE, or SMT16");
}

std::uint64_t uniqueLogicalWeightBytes(const PersistentWorkerTeam& team){
    std::set<std::string> physical_cores;std::uint64_t total=0;
    for(const auto& worker:team.workers()){
        const auto& shard=worker.shardSet();const auto& base=shard.private_copies.empty()?shard.shared:shard.private_copies.front();
        if(physical_cores.insert(worker.core.key).second)total+=base.payload_bytes();
    }
    return total;
}

ShardGeometry geometryForCell(const CellConfig& c){
    if(c.W_target_bytes==589824)return geometryForArchitecturalWorkers(4);
    if(c.W_target_bytes==294912)return geometryForArchitecturalWorkers(8);
    if(c.W_target_bytes==147456)return geometryForArchitecturalWorkers(16);
    return geometryForTarget(c.W_target_bytes);
}

void prepareCellTeam(PersistentWorkerTeam& team,const CellConfig& config,const ShardGeometry& geometry,bool create_b_copies){
    auto& workers=team.workers();
    if(config.setup=="SMT16"){
        if(config.variant!=Variant::A||config.K!=16||config.m!=1||workers.size()!=16)
            throw std::invalid_argument("SMT descriptive row is A-only, K=16, m=1, 16 LPs");
        for(std::size_t first=0;first<workers.size();first+=2){
            if(workers[first].core.key!=workers[first+1].core.key)throw std::runtime_error("SMT sibling LPs are not adjacent in stable-key order");
            const auto workerSeed=config.data_seed^fnv1a(workers[first].core.key);
            auto base=t0q4::generate_weights(geometry.O,512,workerSeed);
            auto shared=std::make_shared<ShardCopies>();shared->initialize(base,config.K,false);
            for(std::size_t sibling=0;sibling<2;++sibling){
                auto& worker=workers[first+sibling];worker.W_target_bytes=config.W_target_bytes;
                const auto acts=t0q4::generate_activations(config.m,512,workerSeed^(0x9E3779B97F4A7C15ULL+worker.core.logical));
                linkSharedShardSet(worker,shared,acts);
                setApplicationPartition(worker,config.K,static_cast<std::uint32_t>(sibling),2);
            }
        }
        return;
    }
    for(auto& worker:workers){
        const auto workerSeed=config.data_seed^fnv1a(worker.core.key);
        const auto weights=t0q4::generate_weights(geometry.O,512,workerSeed);
        const auto activations=t0q4::generate_activations(config.m,512,workerSeed^0x9E3779B97F4A7C15ULL);
        worker.W_target_bytes=config.W_target_bytes;
        initializeQ4Worker(worker,weights,activations,config.K,create_b_copies);
    }
}

EpisodeBatch runEpisodeBatch(PersistentWorkerTeam& team,const EpisodeSpec& spec,std::uint64_t episodes,
                             bool collect_qpc,bool capture_applications=true){
    EpisodeBatch batch;auto& sample=batch.sample;sample.episodes=episodes;
    if(!episodes)throw std::invalid_argument("H0 episode batch must be positive");
    std::map<std::string,std::uint32_t> physical_index;
    for(const auto& worker:team.workers())if(physical_index.emplace(worker.core.key,static_cast<std::uint32_t>(physical_index.size())).second)
        sample.physical_worker_keys[physical_index.at(worker.core.key)]=worker.core.key;
    sample.worker_count=static_cast<std::uint32_t>(physical_index.size());
    std::size_t appsPerEpisode=0;for(const auto& worker:team.workers())appsPerEpisode+=worker.application_count;
    if(capture_applications){
        if(episodes>std::numeric_limits<std::size_t>::max()/std::max<std::size_t>(appsPerEpisode,1))throw std::overflow_error("H0 raw application observation count overflow");
        sample.applications.reserve(static_cast<std::size_t>(episodes)*appsPerEpisode);
    }
    for(std::uint64_t episode=0;episode<episodes;++episode){
        (void)team.prepareEpisode(spec,collect_qpc);
        const TeamResult result=team.run(spec,[](Worker& w,const EpisodeSpec& s,bool qpc,void*){runQ4KApplications(w,s,qpc);},nullptr,collect_qpc);
        sample.team_wall_total_ns+=result.team_wall_ns;
        for(std::size_t wi=0;wi<result.workers.size();++wi){
            const auto& snapshot=result.workers[wi];const auto& worker=team.workers()[wi];
            const auto group=physical_index.at(worker.core.key);
            const std::uint32_t count=worker.application_count;
            std::uint64_t total=0,eviction=0;
            for(std::uint32_t local=0;local<count;++local){total+=snapshot.clocks.kernel_only_ns[local];eviction+=snapshot.clocks.eviction_ns[local];}
            sample.kernel_only_ns[group]+=total;sample.team_wall_ns[group]+=result.team_wall_ns;
            sample.eviction_ns[group]+=eviction+snapshot.clocks.initial_eviction_ns;
            for(std::uint32_t local=0;capture_applications&&local<count;++local){
                H0AppObservation observation{};observation.worker_index=static_cast<std::uint32_t>(wi);
                observation.local_app_index=local;
                observation.global_application_index=worker.application_start+local*worker.application_stride;
                observation.kernel_only_ns=snapshot.clocks.kernel_only_ns[local];
                observation.eviction_ns=snapshot.clocks.eviction_ns[local];
                observation.weight_storage_address=snapshot.clocks.weight_storage_address[local];
                observation.initial_eviction_ns=local==0?snapshot.clocks.initial_eviction_ns:0;
                observation.team_wall_ns=result.team_wall_ns;observation.W_target_bytes=snapshot.W_target_bytes;
                observation.W_real_bytes=snapshot.W_real_bytes;observation.q4_weight_physical_bytes=snapshot.q4_weight_physical_bytes;
                observation.q4_activation_bytes=snapshot.q4_activation_bytes;observation.q4_output_bytes=snapshot.q4_output_bytes;
                observation.eviction_buffer_bytes=snapshot.eviction_buffer_bytes;observation.total_physical_bytes=snapshot.total_physical_bytes;
                observation.O=snapshot.O;observation.I=snapshot.I;sample.applications.push_back(observation);
            }
        }
    }
    return batch;
}

using PairedAASample=std::array<H0Sample,2>;
struct PairedAASampleCache { std::vector<std::vector<PairedAASample>> attempts; };

std::pair<H0Sample,H0Sample> runPairedAABatch(PersistentWorkerTeam& team,const EpisodeSpec& spec,
                                              std::uint64_t episodes,bool collect_qpc,bool capture_applications){
    H0Sample first{},second{};first.episodes=second.episodes=episodes;
    const auto append=[](H0Sample& dst,const H0Sample& src){
        if(dst.worker_count==0){dst.worker_count=src.worker_count;dst.physical_worker_keys=src.physical_worker_keys;}
        if(dst.worker_count!=src.worker_count)throw std::logic_error("paired A/A worker topology changed within a batch");
        for(std::uint32_t w=0;w<src.worker_count;++w){dst.kernel_only_ns[w]+=src.kernel_only_ns[w];dst.team_wall_ns[w]+=src.team_wall_ns[w];dst.eviction_ns[w]+=src.eviction_ns[w];}
        dst.team_wall_total_ns+=src.team_wall_total_ns;
        dst.applications.insert(dst.applications.end(),src.applications.begin(),src.applications.end());
    };
    forEachPairedAAStep(episodes,
        [&](std::uint64_t){auto a=runEpisodeBatch(team,spec,1,collect_qpc,capture_applications).sample;append(first,a);},
        [&](std::uint64_t){auto b=runEpisodeBatch(team,spec,1,collect_qpc,capture_applications).sample;append(second,b);});
    return {std::move(first),std::move(second)};
}

std::uint64_t minimumWorkerTime(const H0Sample& sample){
    if(!sample.worker_count)throw std::runtime_error("H0 episode batch returned no workers");
    std::uint64_t minimum=std::numeric_limits<std::uint64_t>::max();
    for(std::uint32_t i=0;i<sample.worker_count;++i)minimum=std::min(minimum,sample.kernel_only_ns[i]);
    return minimum;
}

double median(std::vector<double> x){
    if(x.empty())throw std::invalid_argument("H0 median needs values");
    std::sort(x.begin(),x.end());const auto n=x.size();return n%2?x[n/2]:(x[n/2-1]+x[n/2])*0.5;
}

struct VariantRunResult { H0SeriesOutcome series; CalibrationResult calibration; };

VariantRunResult runVariantInTeam(PersistentWorkerTeam& team,const std::string& output_directory,
                                  const std::string& cell_id,const CellConfig& config,const ShardGeometry& geometry,
                                  std::uint64_t cell_total_unique_physical,PairedAASampleCache* paired_cache=nullptr,
                                  std::size_t paired_condition=0){
    const auto rawPath=std::filesystem::path(output_directory)/(cell_id+".csv");
    const auto statusPath=std::filesystem::path(output_directory)/(cell_id+".status.csv");
    const auto summaryPath=std::filesystem::path(output_directory)/(cell_id+".summary.csv");
    for(const auto& path:{rawPath,statusPath,summaryPath})if(std::filesystem::exists(path))throw std::runtime_error("refusing to overwrite existing H0 cell output: "+path.string());
    const EpisodeSpec spec{config.variant,config.K,config.m};const H0Policy policy{};
    std::ofstream raw(rawPath,std::ios::binary);if(!raw)throw std::runtime_error("cannot create H0 raw cell CSV");
    raw<<"cell_id,setup,variant,series_attempt,fixed_episodes,rep,episode,worker_index,worker_key,worker_type,processor_group,logical_processor,weight_storage_address,shared_weight_storage_address,application_index,N,warmup_count,K,m,m_tile,route,W_target_bytes,O,I,W_real_bytes,W_total_real_bytes,weight_physical_allocated_bytes,activation_bytes,output_bytes,eviction_buffer_bytes,total_physical_bytes,cell_total_unique_physical_bytes,kernel_only_ns,initial_eviction_ns,eviction_between_ns,team_wall_ns,sample_kernel_only_total_ns,status\n";
    CalibrationResult calibration{};
    if(config.calibration_episodes_override){
        if(config.calibration_source_episodes_override==0||config.calibration_kernel_only_ns_override<policy.calibration_target_ns)
            throw std::invalid_argument("paired A/A fixed episodes require a valid source ENGINEERING_CALIBRATION_ONLY record >=150 ms");
        calibration={config.calibration_episodes_override,config.calibration_kernel_only_ns_override};
    }else calibration=calibrateKernelOnlyEpisodes([&](std::uint64_t episodes){return minimumWorkerTime(runEpisodeBatch(team,spec,episodes,true,false).sample);},policy.calibration_target_ns);
    const auto calibrationPath=std::filesystem::path(output_directory)/"engineering_calibration_only.csv";
    const bool calibrationExists=std::filesystem::exists(calibrationPath);
    std::ofstream calibrationOut(calibrationPath,std::ios::binary|std::ios::app);if(!calibrationOut)throw std::runtime_error("cannot append H0 calibration CSV");
    if(!calibrationExists)calibrationOut<<"cell_id,setup,variant,W_target_bytes,W_real_bytes,O,I,K,m,series_fixed_episodes,calibration_measurement_episodes,kernel_only_min_worker_ns,calibration_target_ns,minimum_sample_ns,source,statistical_sample\n";
    const auto calibration_measurement_episodes=config.calibration_episodes_override?config.calibration_source_episodes_override:calibration.episodes;
    calibrationOut<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<config.W_target_bytes<<","<<geometry.W_real_bytes<<","<<geometry.O<<",512,"<<config.K<<","<<config.m<<","<<calibration.episodes<<","<<calibration_measurement_episodes<<","<<calibration.kernel_only_ns<<","<<policy.calibration_target_ns<<","<<policy.minimum_kernel_only_ns<<","<<(config.calibration_episodes_override?"PAIRED_AA_FIXED_FROM_SOURCE_CALIBRATION":"ENGINEERING_CALIBRATION_ONLY")<<",false\n";

    std::vector<std::vector<std::vector<double>>> repCosts(3,std::vector<std::vector<double>>(kMaxWorkers));
    std::vector<std::vector<double>> teamRepCosts(3),teamRepRates(3);std::array<bool,3> attempt_short{};
    std::uint32_t paired_attempt=0;
    auto writeSample=[&](const H0Sample& sample,std::uint32_t attempt,std::uint32_t rep,bool valid){
        std::size_t cursor=0;
        for(std::uint64_t episode=0;episode<sample.episodes;++episode)for(std::size_t wi=0;wi<team.workers().size();++wi){
            const auto& worker=team.workers()[wi];
            const auto groupIt=std::find(sample.physical_worker_keys.begin(),sample.physical_worker_keys.begin()+sample.worker_count,worker.core.key);
            if(groupIt==sample.physical_worker_keys.begin()+sample.worker_count)throw std::logic_error("H0 raw sample lacks physical worker key");
            const auto physical_index=static_cast<std::size_t>(groupIt-sample.physical_worker_keys.begin());
            for(std::uint32_t local=0;local<worker.application_count;++local){
                if(cursor>=sample.applications.size())throw std::logic_error("H0 app observation underflow");
                const auto& app=sample.applications[cursor++];
                const auto sharedAddress=reinterpret_cast<std::uint64_t>(worker.shardSet().shared.storage.data());
                raw<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<attempt<<","<<sample.episodes<<","<<rep<<","<<episode<<","<<wi<<","<<worker.core.key<<","<<worker.core.type<<","<<worker.core.group<<","<<worker.core.logical<<","<<app.weight_storage_address<<","<<sharedAddress<<","<<app.global_application_index<<","<<policy.N<<","<<policy.warmup<<","<<config.K<<","<<config.m<<","<<t0q4::kernel_m_tile(config.m)<<","<<t0q4::route_name(t0q4::Route::AVXVNNI256)<<","<<app.W_target_bytes<<","<<app.O<<","<<app.I<<","<<app.W_real_bytes<<","<<uniqueLogicalWeightBytes(team)<<","<<app.q4_weight_physical_bytes<<","<<app.q4_activation_bytes<<","<<app.q4_output_bytes<<","<<app.eviction_buffer_bytes<<","<<app.total_physical_bytes<<","<<cell_total_unique_physical<<","<<app.kernel_only_ns<<","<<app.initial_eviction_ns<<","<<app.eviction_ns<<","<<app.team_wall_ns<<","<<sample.kernel_only_ns[physical_index]<<","<<(valid?"OK":"SERIES_INVALID_TIMER_TOO_SHORT")<<"\n";
            }
        }
        raw.flush();
    };

    std::ofstream status(statusPath,std::ios::binary);if(!status)throw std::runtime_error("cannot create H0 series status CSV");
    status<<"cell_id,setup,variant,attempt,fixed_episodes,N,warmup_count,status,has_median\n";
    H0SeriesOutcome outcome;
    try{
        outcome=runOfficialSeries(calibration.episodes,policy,
            [&](std::uint64_t episodes,std::uint32_t warmups){
                if(paired_cache){++paired_attempt;if(paired_condition==0)for(std::uint32_t w=0;w<warmups;++w)(void)runPairedAABatch(team,spec,episodes,false,false);}
                else for(std::uint32_t w=0;w<warmups;++w)(void)runEpisodeBatch(team,spec,episodes,false,false);
            },
            [&](std::uint64_t episodes,std::uint32_t rep){
                if(!paired_cache)return runEpisodeBatch(team,spec,episodes,true).sample;
                if(paired_condition==0){
                    auto pair=runPairedAABatch(team,spec,episodes,true,true);
                    if(paired_cache->attempts.size()<paired_attempt)paired_cache->attempts.resize(paired_attempt);
                    auto& rows=paired_cache->attempts.at(paired_attempt-1);if(rows.size()<=rep)rows.resize(rep+1);
                    rows[rep]={std::move(pair.first),std::move(pair.second)};
                }
                const auto& pair=paired_cache->attempts.at(paired_attempt-1).at(rep);
                if(pair[0].episodes!=episodes||pair[1].episodes!=episodes)
                    throw std::logic_error("paired A/A cached episode count differs from the replay attempt");
                H0Sample gate=pair[0];for(std::uint32_t w=0;w<gate.worker_count;++w)gate.kernel_only_ns[w]=std::min(pair[0].kernel_only_ns[w],pair[1].kernel_only_ns[w]);
                return gate;
            },
            [&](const H0Sample& sample,std::uint32_t attempt,std::uint32_t rep,bool valid){
                attempt_short.at(attempt-1)=attempt_short.at(attempt-1)||!valid;
                const H0Sample& observed=paired_cache?paired_cache->attempts.at(attempt-1).at(rep).at(paired_condition):sample;
                if(valid){
                    std::vector<double> worker_costs;worker_costs.reserve(sample.worker_count);
                    for(std::uint32_t core=0;core<observed.worker_count;++core){
                        const double cost=sampleCost(observed.kernel_only_ns[core],observed.episodes,config.K);
                        repCosts.at(attempt-1).at(core).push_back(cost);worker_costs.push_back(cost);
                    }
                    const double team_cost=teamSampleCost(worker_costs);
                    teamRepCosts.at(attempt-1).push_back(team_cost);
                    const double macs=static_cast<double>(observed.worker_count)*static_cast<double>(observed.episodes)*config.K*
                                      static_cast<double>(geometry.O)*512.0*static_cast<double>(config.m);
                    teamRepRates.at(attempt-1).push_back(observed.team_wall_total_ns?macs/(static_cast<double>(observed.team_wall_total_ns)*1e-9):0.0);
                }
                writeSample(observed,attempt,rep,valid);
            });
        if(outcome.attempt==2)status<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<",1,"<<calibration.episodes<<","<<policy.N<<","<<policy.warmup<<",SERIES_INVALID_TIMER_TOO_SHORT,false\n";
        status<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<outcome.attempt<<","<<outcome.fixed_episodes<<","<<policy.N<<","<<policy.warmup<<","<<(outcome.recovered?"SERIES_RECOVERED_TECHNICAL_RETRY":"SERIES_COMPLETE")<<",true\n";
    }catch(const std::exception& ex){
        for(std::uint32_t attempt=1;attempt<=policy.maximum_technical_retries+1;++attempt)if(attempt_short[attempt-1])
            status<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<attempt<<","<<(calibration.episodes*(attempt==1?1:policy.technical_retry_multiplier))<<","<<policy.N<<","<<policy.warmup<<","<<(attempt==1?"SERIES_INVALID_TIMER_TOO_SHORT":"SERIES_HOLD")<<",false\n";
        status.flush();throw std::runtime_error(std::string("H0 series held: ")+ex.what());
    }
    status.flush();

    std::ofstream summary(summaryPath,std::ios::binary);if(!summary)throw std::runtime_error("cannot create H0 summary CSV");
    summary<<"cell_id,setup,variant,worker_key,worker_type,metric,N_rep,median,IQR,MAD,min,max,unit,episodes_per_sample,K,m,MACs_per_application,median_MAC_per_second,W_target_bytes,O,I,W_real_bytes,weight_physical_allocated_bytes,activation_bytes,output_bytes,eviction_buffer_bytes,total_physical_bytes,cell_total_unique_physical_bytes,series_attempt,fixed_episodes,warmup_count\n";
    std::vector<std::string> physicalKeys;for(const auto& worker:team.workers())if(std::find(physicalKeys.begin(),physicalKeys.end(),worker.core.key)==physicalKeys.end())physicalKeys.push_back(worker.core.key);
    const auto writeSummaryRow=[&](const std::string& workerKey,const std::string& workerType,const std::string& metric,const std::vector<double>& source,const std::string& unit,const std::vector<const Worker*>& members){
        const auto stats=summarizeRepCosts(source);std::set<std::uint64_t> addresses;std::uint64_t weightBytes=0,activationBytes=0,outputBytes=0,evictionBytes=0;
        for(const Worker* worker:members){const auto& shard=worker->shardSet();const auto add=[&](const t0q4::WeightMatrix& weights){const auto address=reinterpret_cast<std::uint64_t>(weights.storage.data());if(addresses.insert(address).second)weightBytes+=weights.physical_allocated_bytes();};
            if(config.variant==Variant::B){add(shard.shared);for(const auto& copy:shard.private_copies)add(copy);}else add(shard.shared);
            activationBytes+=worker->activations.q.capacity()*sizeof(std::int8_t)+worker->activations.scale.capacity()*sizeof(float);outputBytes+=worker->output.values.capacity()*sizeof(float);evictionBytes+=worker->eviction.words.capacity()*sizeof(std::uint64_t);}
        const auto macs=static_cast<std::uint64_t>(geometry.O)*512ULL*config.m;const double rate=unit=="MAC_per_second"?stats.median:(stats.median>0?static_cast<double>(macs)/(stats.median*1e-9):0.0);
        summary<<cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<workerKey<<","<<workerType<<","<<metric<<","<<policy.N<<","<<stats.median<<","<<stats.iqr<<","<<stats.mad<<","<<stats.min<<","<<stats.max<<","<<unit<<","<<outcome.fixed_episodes<<","<<config.K<<","<<config.m<<","<<macs<<","<<rate<<","<<config.W_target_bytes<<","<<geometry.O<<",512,"<<geometry.W_real_bytes<<","<<weightBytes<<","<<activationBytes<<","<<outputBytes<<","<<evictionBytes<<","<<(weightBytes+activationBytes+outputBytes+evictionBytes)<<","<<cell_total_unique_physical<<","<<outcome.attempt<<","<<outcome.fixed_episodes<<","<<policy.warmup<<"\n";
    };
    for(std::size_t core=0;core<physicalKeys.size();++core){std::vector<const Worker*> members;for(const auto& worker:team.workers())if(worker.core.key==physicalKeys[core])members.push_back(&worker);writeSummaryRow(physicalKeys[core],members.front()->core.type,"sample_cost_rep_worker_ns_per_application",repCosts.at(outcome.attempt-1).at(core),"ns_per_application",members);}
    std::vector<const Worker*> allMembers;for(const auto& worker:team.workers())allMembers.push_back(&worker);
    writeSummaryRow("TEAM_MAX","TEAM","team_sample_cost_rep_ns_per_application",teamRepCosts.at(outcome.attempt-1),"ns_per_application",allMembers);
    if(!summary)throw std::runtime_error("failed writing H0 summary CSV");
    return {outcome,calibration};
}

} // namespace

void runH0Cell(const std::string& topology_csv,const std::string& output_directory,const CellConfig& config){
    if(config.cell_id.empty()||config.cell_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::invalid_argument("H0 cell_id must be filesystem-safe ASCII");
    const bool validK=config.K==1||config.K==2||config.K==4||config.K==8||config.K==16;
    const bool validM=config.m==1||config.m==2||config.m==4||config.m==8||config.m==16||config.m==32;
    if(!validK||!validM)
        throw std::invalid_argument("H0 cell K/m outside the frozen canonical sets");
    if(config.variant==Variant::C&&config.K<2)throw std::invalid_argument("C is defined only for K>=2");
    const auto geometry=geometryForCell(config);
    const auto cores=selectWorkers(topology_csv,config.setup);
    if((config.setup=="P8"&&cores.size()!=8)||(config.setup=="P4"&&cores.size()!=4)||
       ((config.setup=="E0"||config.setup=="E1")&&cores.size()!=4)||(config.setup=="PplusE"&&cores.size()!=16)||
       (config.setup=="SMT16"&&cores.size()!=16))throw std::logic_error("H0 setup worker-count mismatch");
    if(config.setup=="P4"&&config.W_target_bytes!=589824)throw std::invalid_argument("4P descriptor is frozen at W_arch(4)");
    if(config.setup=="PplusE"&&config.W_target_bytes!=147456)throw std::invalid_argument("P+E descriptor is frozen at W_arch(16)");
    if(config.setup=="SMT16"&&(config.W_target_bytes!=294912||config.K!=16||config.m!=1||config.variant!=Variant::A))
        throw std::invalid_argument("SMT descriptor is A-only at W_arch(8), K=16, m=1");
    if((config.setup=="E0"||config.setup=="E1")&&config.W_target_bytes!=589824&&
       config.W_target_bytes!=262152&&config.W_target_bytes!=524286&&config.W_target_bytes!=786438&&
       config.W_target_bytes!=1048572&&config.W_target_bytes!=1310724)
        throw std::invalid_argument("E-cluster descriptor W must be an MD/30 sweep point or W_arch(4)");
    if(config.setup=="P8"&&config.W_target_bytes==294912&&geometry.O!=1024)throw std::logic_error("P8 architectural shard must be O=1024");
    std::filesystem::create_directories(output_directory);
    const auto rawPath=std::filesystem::path(output_directory)/(config.cell_id+".csv");
    const auto statusPath=std::filesystem::path(output_directory)/(config.cell_id+".status.csv");
    const auto summaryPath=std::filesystem::path(output_directory)/(config.cell_id+".summary.csv");
    for(const auto& path:{rawPath,statusPath,summaryPath})if(std::filesystem::exists(path))throw std::runtime_error("refusing to overwrite existing H0 cell output: "+path.string());

    PersistentWorkerTeam team(cores,config.eviction_mib);prepareCellTeam(team,config,geometry,config.variant==Variant::B);
    if(config.variant==Variant::B)for(auto& worker:team.workers())if(!worker.shardSet().bCopiesAreIndependentNonAliasingEqualAndPretouched())
        throw std::runtime_error("B copy validation failed before timing: expected independent non-aliasing allocations, byte equality, and page touching");
    const EpisodeSpec spec{config.variant,config.K,config.m};const H0Policy policy{};
    std::set<std::uint64_t> unique_weight_addresses;std::uint64_t unique_weight_physical=0;
    std::uint64_t unique_nonweight_physical=0;
    for(const auto& worker:team.workers()){
        const auto& shard=worker.shardSet();
        if(config.variant==Variant::B){
            const auto baseAddress=reinterpret_cast<std::uint64_t>(shard.shared.storage.data());
            if(unique_weight_addresses.insert(baseAddress).second)unique_weight_physical+=shard.shared.physical_allocated_bytes();
            for(const auto& copy:shard.private_copies){const auto address=reinterpret_cast<std::uint64_t>(copy.storage.data());if(unique_weight_addresses.insert(address).second)unique_weight_physical+=copy.physical_allocated_bytes();}
        }else{
            const auto address=reinterpret_cast<std::uint64_t>(shard.shared.storage.data());if(unique_weight_addresses.insert(address).second)unique_weight_physical+=shard.shared.physical_allocated_bytes();
        }
        unique_nonweight_physical+=worker.activations.q.capacity()*sizeof(std::int8_t)+worker.activations.scale.capacity()*sizeof(float)+
                                   worker.output.values.capacity()*sizeof(float)+worker.eviction.words.capacity()*sizeof(std::uint64_t);
    }
    const std::uint64_t cell_total_unique_physical=unique_weight_physical+unique_nonweight_physical;
    const std::uint64_t cell_W_total_real=uniqueLogicalWeightBytes(team);
    std::ofstream raw(rawPath,std::ios::binary);if(!raw)throw std::runtime_error("cannot create H0 raw cell CSV");
    raw<<"cell_id,setup,variant,series_attempt,fixed_episodes,rep,episode,worker_index,worker_key,worker_type,processor_group,logical_processor,weight_storage_address,shared_weight_storage_address,application_index,N,warmup_count,K,m,m_tile,route,W_target_bytes,O,I,W_real_bytes,W_total_real_bytes,weight_physical_allocated_bytes,activation_bytes,output_bytes,eviction_buffer_bytes,total_physical_bytes,cell_total_unique_physical_bytes,kernel_only_ns,initial_eviction_ns,eviction_between_ns,team_wall_ns,sample_kernel_only_total_ns,status\n";
    CalibrationResult calibration{};
    if(config.calibration_episodes_override){
        if(config.calibration_source_episodes_override==0||config.calibration_kernel_only_ns_override<policy.calibration_target_ns)
            throw std::invalid_argument("paired A/A fixed episodes require a valid source ENGINEERING_CALIBRATION_ONLY record >=150 ms");
        calibration={config.calibration_episodes_override,config.calibration_kernel_only_ns_override};
    }else calibration=calibrateKernelOnlyEpisodes([&](std::uint64_t episodes){return minimumWorkerTime(runEpisodeBatch(team,spec,episodes,true,false).sample);},policy.calibration_target_ns);
    const auto calibrationPath=std::filesystem::path(output_directory)/"engineering_calibration_only.csv";
    const bool calibrationExists=std::filesystem::exists(calibrationPath);
    std::ofstream calibrationOut(calibrationPath,std::ios::binary|std::ios::app);if(!calibrationOut)throw std::runtime_error("cannot append H0 calibration CSV");
    if(!calibrationExists)calibrationOut<<"cell_id,setup,variant,W_target_bytes,W_real_bytes,O,I,K,m,series_fixed_episodes,calibration_measurement_episodes,kernel_only_min_worker_ns,calibration_target_ns,minimum_sample_ns,source,statistical_sample\n";
    const auto calibration_measurement_episodes=config.calibration_episodes_override?config.calibration_source_episodes_override:calibration.episodes;
    calibrationOut<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<config.W_target_bytes<<","<<geometry.W_real_bytes<<","<<geometry.O<<",512,"<<config.K<<","<<config.m<<","<<calibration.episodes<<","<<calibration_measurement_episodes<<","<<calibration.kernel_only_ns<<","<<policy.calibration_target_ns<<","<<policy.minimum_kernel_only_ns<<","<<(config.calibration_episodes_override?"PAIRED_AA_FIXED_FROM_SOURCE_CALIBRATION":"ENGINEERING_CALIBRATION_ONLY")<<",false\n";

    std::vector<std::vector<std::vector<double>>> perCoreRepCosts(3,std::vector<std::vector<double>>(kMaxWorkers));
    std::vector<std::vector<double>> teamRepCosts(3),teamRepRates(3);
    std::array<bool,3> attempt_short{};

    auto writeSample=[&](const H0Sample& sample,std::uint32_t attempt,std::uint32_t rep,bool valid){
        std::size_t cursor=0;
        for(std::uint64_t episode=0;episode<sample.episodes;++episode){
            for(std::size_t wi=0;wi<team.workers().size();++wi){
                const auto& worker=team.workers()[wi];
                const auto groupIt=std::find(sample.physical_worker_keys.begin(),sample.physical_worker_keys.begin()+sample.worker_count,worker.core.key);
                if(groupIt==sample.physical_worker_keys.begin()+sample.worker_count)throw std::logic_error("H0 raw sample lacks physical worker key");
                const auto physical_index=static_cast<std::size_t>(groupIt-sample.physical_worker_keys.begin());
                for(std::uint32_t local=0;local<worker.application_count;++local){
                    if(cursor>=sample.applications.size())throw std::logic_error("H0 app observation underflow");
                    const auto& app=sample.applications[cursor++];
                    const auto sharedAddress=reinterpret_cast<std::uint64_t>(team.workers()[wi].shardSet().shared.storage.data());
                    raw<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<attempt<<","<<sample.episodes<<","<<rep<<","<<episode<<","<<wi<<","<<worker.core.key<<","<<worker.core.type<<","<<worker.core.group<<","<<worker.core.logical<<","<<app.weight_storage_address<<","<<sharedAddress<<","<<app.global_application_index<<","<<policy.N<<","<<policy.warmup<<","<<config.K<<","<<config.m<<","<<t0q4::kernel_m_tile(config.m)<<","<<t0q4::route_name(t0q4::Route::AVXVNNI256)<<","<<app.W_target_bytes<<","<<app.O<<","<<app.I<<","<<app.W_real_bytes<<","<<cell_W_total_real<<","<<app.q4_weight_physical_bytes<<","<<app.q4_activation_bytes<<","<<app.q4_output_bytes<<","<<app.eviction_buffer_bytes<<","<<app.total_physical_bytes<<","<<cell_total_unique_physical<<","<<app.kernel_only_ns<<","<<app.initial_eviction_ns<<","<<app.eviction_ns<<","<<app.team_wall_ns<<","<<sample.kernel_only_ns[physical_index]<<","<<(valid?"OK":"SERIES_INVALID_TIMER_TOO_SHORT")<<"\n";
                }
            }
        }
        raw.flush();
    };

    std::ofstream status(statusPath,std::ios::binary);if(!status)throw std::runtime_error("cannot create H0 series status CSV");
    status<<"cell_id,setup,variant,attempt,fixed_episodes,N,warmup_count,status,has_median\n";
    H0SeriesOutcome outcome;
    try{
        outcome=runOfficialSeries(calibration.episodes,policy,
            [&](std::uint64_t episodes,std::uint32_t warmups){for(std::uint32_t w=0;w<warmups;++w)(void)runEpisodeBatch(team,spec,episodes,false,false);},
            [&](std::uint64_t episodes,std::uint32_t){return runEpisodeBatch(team,spec,episodes,true).sample;},
            [&](const H0Sample& sample,std::uint32_t attempt,std::uint32_t rep,bool valid){
                attempt_short.at(attempt-1)=attempt_short.at(attempt-1)||!valid;
                if(valid){
                    std::vector<double> worker_costs;worker_costs.reserve(sample.worker_count);
                    for(std::uint32_t worker=0;worker<sample.worker_count;++worker){
                        const double cost=sampleCost(sample.kernel_only_ns[worker],sample.episodes,config.K);
                        perCoreRepCosts.at(attempt-1).at(worker).push_back(cost);worker_costs.push_back(cost);
                    }
                    const double team_cost=teamSampleCost(worker_costs);
                    teamRepCosts.at(attempt-1).push_back(team_cost);
                    const double macs=static_cast<double>(sample.worker_count)*static_cast<double>(sample.episodes)*config.K*static_cast<double>(geometry.O)*512.0*static_cast<double>(config.m);
                    teamRepRates.at(attempt-1).push_back(sample.team_wall_total_ns?macs/(static_cast<double>(sample.team_wall_total_ns)*1e-9):0.0);
                }
                writeSample(sample,attempt,rep,valid);
            });
        if(outcome.attempt==2)status<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<",1,"<<(calibration.episodes)<<","<<policy.N<<","<<policy.warmup<<",SERIES_INVALID_TIMER_TOO_SHORT,false\n";
        status<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<outcome.attempt<<","<<outcome.fixed_episodes<<","<<policy.N<<","<<policy.warmup<<","<<(outcome.recovered?"SERIES_RECOVERED_TECHNICAL_RETRY":"SERIES_COMPLETE")<<",true\n";
    }catch(const std::exception& ex){
        for(std::uint32_t attempt=1;attempt<=policy.maximum_technical_retries+1;++attempt)if(attempt_short[attempt-1])
            status<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<attempt<<","<<(calibration.episodes*(attempt==1?1:policy.technical_retry_multiplier))<<","<<policy.N<<","<<policy.warmup<<","<<(attempt==1?"SERIES_INVALID_TIMER_TOO_SHORT":"SERIES_HOLD")<<",false\n";
        status.flush();throw std::runtime_error(std::string("H0 series held: ")+ex.what());
    }
    status.flush();

    std::ofstream summary(summaryPath,std::ios::binary);if(!summary)throw std::runtime_error("cannot create H0 summary CSV");
    summary<<"cell_id,setup,variant,worker_key,worker_type,metric,N_rep,median,IQR,MAD,min,max,unit,episodes_per_sample,K,m,MACs_per_application,median_MAC_per_second,W_target_bytes,O,I,W_real_bytes,weight_physical_allocated_bytes,activation_bytes,output_bytes,eviction_buffer_bytes,total_physical_bytes,cell_total_unique_physical_bytes,series_attempt,fixed_episodes,warmup_count\n";
    const auto summaryStats=[&](const std::string& worker_key,const std::string& worker_type,const std::string& metric,const std::vector<double>& source,const std::string& unit,const std::vector<const Worker*>& members){
        if(source.size()!=policy.N)throw std::runtime_error("H0 D3 summary requires exactly N=31 rep-level observations");
        const auto stats=summarizeRepCosts(source);
        std::set<std::uint64_t> addresses;std::uint64_t weightBytes=0,activationBytes=0,outputBytes=0,evictionBytes=0;
        for(const Worker* worker:members){
            const auto& shard=worker->shardSet();
            const auto add=[&](const t0q4::WeightMatrix& weights){const auto address=reinterpret_cast<std::uint64_t>(weights.storage.data());if(addresses.insert(address).second)weightBytes+=weights.physical_allocated_bytes();};
            if(config.variant==Variant::B){add(shard.shared);for(const auto& copy:shard.private_copies)add(copy);}else add(shard.shared);
            activationBytes+=worker->activations.q.capacity()*sizeof(std::int8_t)+worker->activations.scale.capacity()*sizeof(float);
            outputBytes+=worker->output.values.capacity()*sizeof(float);evictionBytes+=worker->eviction.words.capacity()*sizeof(std::uint64_t);
        }
        const std::uint64_t totalPhysical=weightBytes+activationBytes+outputBytes+evictionBytes;
        const std::uint64_t macs=static_cast<std::uint64_t>(geometry.O)*512ULL*config.m;
        const double rate=(unit=="ns_per_application"&&stats.median>0)?static_cast<double>(macs)/(stats.median*1e-9):(unit=="MAC_per_second"?stats.median:0.0);
        summary<<config.cell_id<<","<<config.setup<<","<<variantName(config.variant)<<","<<worker_key<<","<<worker_type<<","<<metric<<","<<policy.N<<","<<stats.median<<","<<stats.iqr<<","<<stats.mad<<","<<stats.min<<","<<stats.max<<","<<unit<<","<<outcome.fixed_episodes<<","<<config.K<<","<<config.m<<","<<macs<<","<<rate<<","<<config.W_target_bytes<<","<<geometry.O<<",512,"<<geometry.W_real_bytes<<","<<weightBytes<<","<<activationBytes<<","<<outputBytes<<","<<evictionBytes<<","<<totalPhysical<<","<<cell_total_unique_physical<<","<<outcome.attempt<<","<<outcome.fixed_episodes<<","<<policy.warmup<<"\n";
    };
    std::vector<std::string> physicalKeys;for(const auto& worker:team.workers())if(std::find(physicalKeys.begin(),physicalKeys.end(),worker.core.key)==physicalKeys.end())physicalKeys.push_back(worker.core.key);
    for(std::size_t core=0;core<physicalKeys.size();++core){
        std::vector<const Worker*> members;for(const auto& worker:team.workers())if(worker.core.key==physicalKeys[core])members.push_back(&worker);
        if(members.empty())throw std::logic_error("H0 physical-core summary missing worker group");
        summaryStats(physicalKeys[core],members.front()->core.type,"sample_cost_rep_worker_ns_per_application",perCoreRepCosts.at(outcome.attempt-1).at(core),"ns_per_application",members);
    }
    std::vector<const Worker*> allMembers;for(const auto& worker:team.workers())allMembers.push_back(&worker);
    summaryStats("TEAM_MAX","TEAM","team_sample_cost_rep_ns_per_application",teamRepCosts.at(outcome.attempt-1),"ns_per_application",allMembers);
    summaryStats("TEAM_RATE","TEAM","team_MAC_rate_rep",teamRepRates.at(outcome.attempt-1),"MAC_per_second",allMembers);
    if(!summary)throw std::runtime_error("failed writing H0 summary CSV");
    std::cout<<"{\"record\":\"h0_cell_complete\",\"cell_id\":\""<<config.cell_id<<"\",\"setup\":\""<<config.setup
             <<"\",\"variant\":\""<<variantName(config.variant)<<"\",\"W_target_bytes\":"<<config.W_target_bytes
             <<",\"O\":"<<geometry.O<<",\"W_real_bytes\":"<<geometry.W_real_bytes<<",\"series_attempt\":"<<outcome.attempt
             <<",\"official_timing_performed\":true}\n";
}

void runH0Block(const std::string& topology_csv,const std::string& output_directory,CellConfig block_config,
                const std::vector<Variant>& randomized_variants){
    if(block_config.cell_id.empty()||block_config.cell_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
        throw std::invalid_argument("H0 block id must be filesystem-safe ASCII");
    if(randomized_variants.empty())throw std::invalid_argument("H0 block variant order is empty");
    for(auto v:randomized_variants)if(v!=Variant::A&&v!=Variant::B&&v!=Variant::C)throw std::invalid_argument("unknown variant in preregistered H0 block");
    const bool validK=block_config.K==1||block_config.K==2||block_config.K==4||block_config.K==8||block_config.K==16;
    const bool validM=block_config.m==1||block_config.m==2||block_config.m==4||block_config.m==8||block_config.m==16||block_config.m==32;
    if(!validK||!validM)throw std::invalid_argument("H0 block K/m outside frozen canonical sets");
    if(std::find(randomized_variants.begin(),randomized_variants.end(),Variant::C)!=randomized_variants.end()&&block_config.K<2)
        throw std::invalid_argument("C requires K>=2");
    const auto geometry=geometryForCell(block_config);const auto cores=selectWorkers(topology_csv,block_config.setup);
    const bool containsB=std::find(randomized_variants.begin(),randomized_variants.end(),Variant::B)!=randomized_variants.end();
    block_config.variant=randomized_variants.front();
    if(block_config.setup=="SMT16"&&(randomized_variants.size()!=1||block_config.variant!=Variant::A||block_config.K!=16||block_config.m!=1||block_config.W_target_bytes!=294912))
        throw std::invalid_argument("SMT block is fixed A-only, K=16, m=1, W_arch(8)");
    if((block_config.setup=="P8"&&cores.size()!=8)||(block_config.setup=="P4"&&cores.size()!=4)||
       ((block_config.setup=="E0"||block_config.setup=="E1")&&cores.size()!=4)||(block_config.setup=="PplusE"&&cores.size()!=16)||
       (block_config.setup=="SMT16"&&cores.size()!=16))throw std::logic_error("H0 block worker-count mismatch");
    if(block_config.setup=="P4"&&block_config.W_target_bytes!=589824)throw std::invalid_argument("4P is frozen at W_arch(4)");
    if(block_config.setup=="PplusE"&&block_config.W_target_bytes!=147456)throw std::invalid_argument("P+E is frozen at W_arch(16)");
    if((block_config.setup=="E0"||block_config.setup=="E1")&&block_config.W_target_bytes!=589824&&
       block_config.W_target_bytes!=262152&&block_config.W_target_bytes!=524286&&block_config.W_target_bytes!=786438&&
       block_config.W_target_bytes!=1048572&&block_config.W_target_bytes!=1310724)
        throw std::invalid_argument("E-cluster W is outside the preregistered points");

    PersistentWorkerTeam team(cores,block_config.eviction_mib);
    prepareCellTeam(team,block_config,geometry,containsB);
    if(containsB)for(auto& worker:team.workers())if(!worker.shardSet().bCopiesAreIndependentNonAliasingEqualAndPretouched())
        throw std::runtime_error("B copy validation failed before block timing: expected independent non-aliasing allocations, byte equality, and page touching");
    std::set<std::uint64_t> weight_addresses;std::uint64_t weight_physical=0,other_physical=0;
    for(const auto& worker:team.workers()){
        const auto& shard=worker.shardSet();
        const auto add=[&](const t0q4::WeightMatrix& w){const auto address=reinterpret_cast<std::uint64_t>(w.storage.data());if(weight_addresses.insert(address).second)weight_physical+=w.physical_allocated_bytes();};
        add(shard.shared);for(const auto& copy:shard.private_copies)add(copy);
        other_physical+=worker.activations.q.capacity()*sizeof(std::int8_t)+worker.activations.scale.capacity()*sizeof(float)+
                        worker.output.values.capacity()*sizeof(float)+worker.eviction.words.capacity()*sizeof(std::uint64_t);
    }
    const auto cell_physical=weight_physical+other_physical;
    std::optional<VariantRunResult> aa_source;
    const bool paired_aa=randomized_variants.size()==2&&randomized_variants[0]==Variant::A&&randomized_variants[1]==Variant::A;
    PairedAASampleCache paired_cache;
    for(std::size_t index=0;index<randomized_variants.size();++index){
        CellConfig config=block_config;config.variant=randomized_variants[index];
        config.cell_id=block_config.cell_id+"_"+variantName(config.variant)+"_"+std::to_string(index+1);
        if(paired_aa&&index==1){
            if(!aa_source)throw std::logic_error("paired A/A source series missing");
            // Replay both cached attempts from the original episode count, including the common retry.
            config.calibration_episodes_override=aa_source->calibration.episodes;
            config.calibration_source_episodes_override=aa_source->calibration.episodes;
            config.calibration_kernel_only_ns_override=aa_source->calibration.kernel_only_ns;
        }
        auto current=runVariantInTeam(team,output_directory,config.cell_id,config,geometry,cell_physical,paired_aa?&paired_cache:nullptr,index);
        if(paired_aa&&index==0)aa_source=current;
    }
    std::cout<<"{\"record\":\"h0_block_complete\",\"block_id\":\""<<block_config.cell_id
             <<"\",\"variant_count\":"<<randomized_variants.size()<<",\"W_target_bytes\":"<<block_config.W_target_bytes
             <<",\"O\":"<<geometry.O<<",\"W_real_bytes\":"<<geometry.W_real_bytes
             <<",\"same_A_C_base_address_for_block\":true,\"official_timing_performed\":true}\n";
}

void runIndependentEvictionTestToFile(const std::string& topology_csv,const std::string& output_path,
                                     std::uint64_t eviction_mib,std::uint64_t seed){
    if(std::filesystem::exists(output_path))throw std::runtime_error("refusing to overwrite H0 eviction-test output");
    const auto cores=loadPPhysicalCores(topology_csv);
    const auto victim=t0q4::generate_weights(1024,512,seed);
    H0Policy policy{};
    const auto result=runIndependentEvictionTest(cores,eviction_mib,victim.storage,policy.warmup,policy);
    std::ofstream out(output_path,std::ios::binary);if(!out)throw std::runtime_error("cannot create H0 eviction-test result JSON");
    out<<"{\n  \"record\": \"h0_independent_eviction_validation\",\n  \"status\": \""<<(result.pass?"PASS":"FAIL")
       <<"\",\n  \"buffer_mib_per_p_core\": "<<result.buffer_mib<<",\n  \"victim_matrix_O\": 1024,\n  \"victim_matrix_I\": 512,\n  \"victim_W_real_bytes\": "<<victim.payload_bytes()<<",\n  \"N\": "<<policy.N<<",\n  \"warmup_count\": "<<policy.warmup
       <<",\n  \"load_only\": true,\n  \"measurement\": true,\n  \"cores\": [\n";
    for(std::size_t i=0;i<result.cores.size();++i){
        const auto& core=result.cores[i];out<<"    {\"worker_key\": \""<<core.core.key<<"\", \"logical_processor\": "<<core.core.logical
            <<", \"ratio_median_evicted_over_warm\": "<<std::setprecision(12)<<core.ratio_median<<", \"warm_ns\": [";
        for(std::size_t j=0;j<core.warm_ns.size();++j)out<<(j?",":"")<<core.warm_ns[j];
        out<<"], \"evicted_ns\": [";for(std::size_t j=0;j<core.evicted_ns.size();++j)out<<(j?",":"")<<core.evicted_ns[j];
        out<<"], \"eviction_sweep_ns\": [";for(std::size_t j=0;j<core.eviction_sweep_ns.size();++j)out<<(j?",":"")<<core.eviction_sweep_ns[j];
        out<<"]}"<<(i+1==result.cores.size()?"\n":",\n");
    }
    out<<"  ]\n}\n";if(!out)throw std::runtime_error("failed writing H0 eviction-test JSON");
}

} // namespace t0h0
