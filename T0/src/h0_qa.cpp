#define NOMINMAX
#include "h0_scaffold.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
struct QaSpyState { std::atomic<std::uint64_t> calls{0}; std::atomic<std::uint64_t> digest{0}; };

void qaSpyApplication(t0h0::Worker& worker,const t0q4::WeightMatrix& weights,std::uint32_t m,void* context){
    auto& spy=*static_cast<QaSpyState*>(context);spy.calls.fetch_add(1,std::memory_order_relaxed);
    const auto* bytes=weights.storage.data();std::uint64_t value=static_cast<std::uint64_t>(m);
    for(std::size_t i=0;i<weights.storage.size();i+=64)value=(value*1315423911ULL)^bytes[i];
    spy.digest.fetch_xor(value,std::memory_order_relaxed);worker.qa_cookie^=value;
}

void qaTeamJob(t0h0::Worker& worker,const t0h0::EpisodeSpec& spec,bool collect_qpc,void* context){
    t0h0::runKApplications(worker,spec,qaSpyApplication,context,collect_qpc);
}

bool allSameBits(const std::vector<float>& left,const std::vector<float>& right){
    return left.size()==right.size()&&(left.empty()||std::memcmp(left.data(),right.data(),left.size()*sizeof(float))==0);
}

bool nonOverlapping(const void* a,std::size_t a_bytes,const void* b,std::size_t b_bytes){
    const auto aa=reinterpret_cast<std::uintptr_t>(a),bb=reinterpret_cast<std::uintptr_t>(b);
    return a&&b&&a_bytes&&b_bytes&&(aa+a_bytes<=bb||bb+b_bytes<=aa);
}

bool testGeometry(){
    const std::array<std::pair<std::uint64_t,std::uint32_t>,11> expected{{
        {262152,910},{294912,1024},{524286,1820},{786438,2731},{1048572,3641},{1310724,4551},
        {1572858,5461},{1835010,6372},{2097144,7282},{2621448,9102},{3145734,10923}
    }};
    for(const auto& item:expected){const auto g=t0h0::geometryForTarget(item.first);if(g.O!=item.second||g.W_real_bytes!=static_cast<std::uint64_t>(g.O)*288ULL)return false;}
    return t0h0::geometryForArchitecturalWorkers(4).O==2048&&
           t0h0::geometryForArchitecturalWorkers(8).O==1024&&
           t0h0::geometryForArchitecturalWorkers(16).O==512;
}

bool testAbaPointersAndKLoop(const std::string& topology){
    const auto cores=t0h0::loadPPhysicalCores(topology);
    const auto base=t0q4::generate_weights(2,512,0x4830513347413031ULL);
    const auto activations=t0q4::generate_activations(4,512,0x4830513347413032ULL);
    auto sharedAC=std::make_shared<t0h0::ShardCopies>();sharedAC->initialize(base,16,false);
    t0h0::Worker workerA{},workerB{},workerC{};
    workerA.core=cores[0];workerB.core=cores[0];workerC.core=cores[0];
    workerA.eviction.allocateAndPretouch(8);workerB.eviction.allocateAndPretouch(8);workerC.eviction.allocateAndPretouch(8);
    t0h0::linkSharedShardSet(workerA,sharedAC,activations);t0h0::linkSharedShardSet(workerC,sharedAC,activations);
    t0h0::initializeQ4Worker(workerB,base,activations,16,true);
     auto& shardB=workerB.shardSet();
    if(workerA.shardSet().forApplication(t0h0::Variant::A,0).storage.data()!=workerA.shardSet().forApplication(t0h0::Variant::A,15).storage.data())return false;
    if(workerA.shardSet().shared.storage.data()!=workerC.shardSet().shared.storage.data())return false;
     if(!shardB.bCopiesAreIndependentNonAliasingEqualAndPretouched())return false;
    std::array<const void*,16> b_addresses{};
    for(std::uint32_t app=0;app<16;++app)b_addresses[app]=shardB.forApplication(t0h0::Variant::B,app).storage.data();
    if(std::memcmp(base.storage.data(),shardB.private_copies.front().storage.data(),base.storage.size())!=0)return false;

    t0h0::setApplicationPartition(workerA,1,0,1);QaSpyState one{};t0h0::prepareEpisode(workerA,{t0h0::Variant::A,1,4},false);
    t0h0::runKApplications(workerA,{t0h0::Variant::A,1,4},qaSpyApplication,&one,false);
    if(one.calls.load(std::memory_order_relaxed)!=1)return false;
    t0h0::setApplicationPartition(workerB,16,0,1);
    QaSpyState many{};t0h0::prepareEpisode(workerB,{t0h0::Variant::B,16,4},false);
    t0h0::runKApplications(workerB,{t0h0::Variant::B,16,4},qaSpyApplication,&many,false);
    if(many.calls.load(std::memory_order_relaxed)!=16)return false;
    for(std::uint32_t app=0;app<16;++app)if(b_addresses[app]!=shardB.forApplication(t0h0::Variant::B,app).storage.data())return false;

    t0q4::Evaluation reference=t0q4::execute(base,activations,t0q4::Route::Scalar);
    for(auto* worker:{&workerA,&workerB,&workerC}){
        t0h0::setApplicationPartition(*worker,3,0,1);
        const auto variant=worker==&workerA?t0h0::Variant::A:(worker==&workerB?t0h0::Variant::B:t0h0::Variant::C);
        t0h0::prepareEpisode(*worker,{variant,3,4},false);
        t0h0::runQ4KApplications(*worker,{variant,3,4},false);
        if(!allSameBits(worker->output.values,reference.output))return false;
    }
    t0h0::setApplicationPartition(workerC,16,0,1);
    t0h0::prepareEpisode(workerC,{t0h0::Variant::C,16,4},false);
    t0h0::runKApplications(workerC,{t0h0::Variant::C,16,4},qaSpyApplication,&many,false);
    if(many.calls.load(std::memory_order_relaxed)!=32||workerC.clocks.eviction_sweeps!=15||workerC.clocks.initial_eviction_sweeps!=1)return false;
    return true;
}

bool testPersistentTeamNoQpc(const std::string& topology){
    auto cores=t0h0::loadPPhysicalCores(topology);t0h0::PersistentWorkerTeam team(std::move(cores),8);
    const t0h0::EpisodeSpec spec{t0h0::Variant::A,1,1};
    std::set<const void*> eviction_addresses;
    for(auto& worker:team.workers()){
        if(worker.eviction.requested_bytes!=8ULL*1024ULL*1024ULL||!eviction_addresses.insert(worker.eviction.words.data()).second)return false;
        if(worker.eviction.loadOnlySweep()==0)return false;
        auto weights=t0q4::generate_weights(1,512,0x483051335445414dULL+worker.core.logical);
        auto activations=t0q4::generate_activations(1,512,0x483051335445414dULL^worker.core.logical);
        t0h0::initializeQ4Worker(worker,weights,activations,1,false);
    }
    const auto prep=team.prepareEpisode(spec,false);
    if(prep.qpc_collected||prep.team_wall_ns!=0)return false;
    QaSpyState spy{};const auto run=team.run(spec,qaTeamJob,&spy,false);
    if(run.qpc_collected||run.team_wall_ns!=0)return false;
    if(run.workers.size()!=t0h0::kPWorkers)return false;
    for(std::size_t i=0;i<t0h0::kPWorkers;++i)if(run.workers[i].clocks.calls!=1||run.workers[i].error)return false;
    return spy.calls.load(std::memory_order_relaxed)==t0h0::kPWorkers;
}

bool testSmtSharedShardNoQpc(const std::string& topology){
    auto logicals=t0h0::loadPLogicalProcessors(topology);t0h0::PersistentWorkerTeam team(std::move(logicals),8);
    std::set<const void*> eviction_addresses;
    for(const auto& worker:team.workers())if(!eviction_addresses.insert(worker.eviction.words.data()).second)return false;
    std::size_t first=0;
    while(first<team.workers().size()){
        const std::string key=team.workers()[first].core.key;
        if(first+1>=team.workers().size()||team.workers()[first+1].core.key!=key)return false;
        const auto weights=t0q4::generate_weights(2,512,0x4830534D545F5134ULL+first);
        auto shared=std::make_shared<t0h0::ShardCopies>();shared->initialize(weights,16,false);
        for(std::size_t sibling=0;sibling<2;++sibling){
            auto& worker=team.workers()[first+sibling];
            const auto acts=t0q4::generate_activations(1,512,0x4830534D545F5134ULL^worker.core.logical);
            t0h0::linkSharedShardSet(worker,shared,acts);worker.W_target_bytes=294912;
            t0h0::setApplicationPartition(worker,16,static_cast<std::uint32_t>(sibling),2);
        }
        first+=2;
    }
    const t0h0::EpisodeSpec spec{t0h0::Variant::A,16,1};team.prepareEpisode(spec,false);
    QaSpyState spy{};const auto run=team.run(spec,qaTeamJob,&spy,false);
    if(run.workers.size()!=16||spy.calls.load(std::memory_order_relaxed)!=128)return false;
    for(std::size_t i=0;i<run.workers.size();i+=2){
         const auto& a=run.workers[i];const auto& b=run.workers[i+1];
         if(a.shared_weight_storage_address==0||a.shared_weight_storage_address!=b.shared_weight_storage_address)return false;
         const auto& wa=team.workers()[i];const auto& wb=team.workers()[i+1];
         if(!nonOverlapping(wa.activations.q.data(),wa.activations.q.size(),wb.activations.q.data(),wb.activations.q.size())||
            !nonOverlapping(wa.activations.scale.data(),wa.activations.scale.size()*sizeof(float),wb.activations.scale.data(),wb.activations.scale.size()*sizeof(float))||
            !nonOverlapping(wa.output.values.data(),wa.output.values.size()*sizeof(float),wb.output.values.data(),wb.output.values.size()*sizeof(float))||
            !nonOverlapping(wa.eviction.words.data(),wa.eviction.words.size()*sizeof(std::uint64_t),wb.eviction.words.data(),wb.eviction.words.size()*sizeof(std::uint64_t)))return false;
         if(&wa.clocks==&wb.clocks||!nonOverlapping(&wa.qa_cookie,sizeof(wa.qa_cookie),&wb.qa_cookie,sizeof(wb.qa_cookie))||
            wa.application_start!=0||wb.application_start!=1||wa.application_stride!=2||wb.application_stride!=2)return false;
         if(a.clocks.calls!=8||b.clocks.calls!=8||a.W_real_bytes!=b.W_real_bytes)return false;
    }
    return true;
}

bool testEvictionAndSeriesPolicies(){
    t0h0::EvictionBuffer first{},second{};first.allocateAndPretouch(8);second.allocateAndPretouch(8);
    if(first.words.data()==second.words.data()||first.words.size()!=second.words.size())return false;
    const auto s1=first.loadOnlySweep(),s2=second.loadOnlySweep();if(s1==0||s2==0)return false;
    const t0h0::H0Policy p{};
    const auto retry=t0h0::decideSeriesSample(8,99999999,1,p);
    const auto hold=t0h0::decideSeriesSample(16,99999999,2,p);
    const auto complete=t0h0::decideSeriesSample(8,100000000,1,p);
    return retry.next_episodes==16&&std::string(retry.status)=="SERIES_INVALID_TIMER_TOO_SHORT"&&
           std::string(hold.status)=="SERIES_HOLD"&&!hold.accept_median&&complete.accept_median&&
           t0h0::evictionLadderAction(8,true)==t0h0::EvictionLadderAction::Freeze8MiB&&
           t0h0::evictionLadderAction(8,false)==t0h0::EvictionLadderAction::Try16MiB&&
           t0h0::evictionLadderAction(16,true)==t0h0::EvictionLadderAction::Freeze16MiB&&
           t0h0::evictionLadderAction(16,false)==t0h0::EvictionLadderAction::HoldAfter16MiB;
}

bool testVariantOrderDeterminism(){
    std::mt19937_64 a(t0h0::kH0OrderSeed),b(t0h0::kH0OrderSeed);
    const auto first=t0h0::randomizedVariants("ABC",a),second=t0h0::randomizedVariants("ABC",b);
    return first==second&&first.size()==3&&
           std::count(first.begin(),first.end(),t0h0::Variant::A)==1&&
           std::count(first.begin(),first.end(),t0h0::Variant::B)==1&&
           std::count(first.begin(),first.end(),t0h0::Variant::C)==1;
}

bool testNearestRankP95Indices(){
    return t0h0::p95NearestRankIndex(31)==29&&t0h0::p95NearestRankIndex(20)==18;
}

bool testRepLevelStatistics(){
    std::vector<double> team_costs;team_costs.reserve(31);
    for(std::uint64_t rep=0;rep<31;++rep){
        const auto core0=t0h0::sampleCost((rep+1)*32,2,16);
        const auto core1=t0h0::sampleCost((rep+2)*32,2,16);
        if(core0!=static_cast<double>(rep+1)||core1!=static_cast<double>(rep+2))return false;
        team_costs.push_back(t0h0::teamSampleCost({core0,core1}));
    }
    const auto stats=t0h0::summarizeRepCosts(team_costs);
    if(stats.median!=17||stats.iqr!=15||stats.mad!=8||stats.min!=2||stats.max!=32)return false;
    try{(void)t0h0::summarizeRepCosts(std::vector<double>(30,1));return false;}catch(const std::invalid_argument&){}
    return true;
}

bool testPairedAAEpisodeOrder(){
    std::vector<std::pair<std::uint64_t,char>> sequence;
    t0h0::forEachPairedAAStep(3,[&](std::uint64_t e){sequence.emplace_back(e,'1');},[&](std::uint64_t e){sequence.emplace_back(e,'2');});
    const std::vector<std::pair<std::uint64_t,char>> expected{{0,'1'},{0,'2'},{1,'1'},{1,'2'},{2,'1'},{2,'2'}};
    if(sequence!=expected)return false;
    // Synthetic durations only: both conditions replay the same 31-rep retry ladder.
    const t0h0::H0Policy policy{};
    std::array<std::vector<t0h0::H0Sample>,2> cache;
    std::uint32_t attempt=0;
    const auto first=t0h0::runOfficialSeries(2,policy,
        [&](std::uint64_t,std::uint32_t){++attempt;},
        [&](std::uint64_t episodes,std::uint32_t){
            t0h0::H0Sample sample{};sample.episodes=episodes;sample.worker_count=1;
            sample.kernel_only_ns[0]=episodes==2?99999999ULL:100000000ULL;
            cache.at(attempt-1).push_back(sample);return sample;
        },[](const t0h0::H0Sample&,std::uint32_t,std::uint32_t,bool){});
    attempt=0;
    const auto second=t0h0::runOfficialSeries(2,policy,
        [&](std::uint64_t,std::uint32_t){++attempt;},
        [&](std::uint64_t episodes,std::uint32_t rep){
            const auto sample=cache.at(attempt-1).at(rep);
            if(sample.episodes!=episodes)throw std::logic_error("synthetic paired replay episode mismatch");
            return sample;
        },[](const t0h0::H0Sample&,std::uint32_t,std::uint32_t,bool){});
    return first.attempt==2&&second.attempt==2&&first.fixed_episodes==4&&second.fixed_episodes==4&&
           cache[0].size()==31&&cache[1].size()==31;
}

bool testCalibrationPolicyPure(){
    const auto calibration=t0h0::calibrateKernelOnlyEpisodes([](std::uint64_t episodes){return episodes*75000000ULL;});
    return calibration.episodes==2&&calibration.kernel_only_ns==150000000ULL;
}
}

int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"usage: h0_qa.exe <b_topologia.csv>\n";return 2;}
    try{
        const std::string topology=argv[1];
        const bool geometry=testGeometry();
        const bool pointerAndLoop=testAbaPointersAndKLoop(topology);
        const bool team=testPersistentTeamNoQpc(topology);
        const bool smt=testSmtSharedShardNoQpc(topology);
        const bool eviction=testEvictionAndSeriesPolicies();
        const bool calibration=testCalibrationPolicyPure();
        const bool order=testVariantOrderDeterminism();
        const bool nearestRank=testNearestRankP95Indices();
        const bool repStats=testRepLevelStatistics();
        const bool aaOrder=testPairedAAEpisodeOrder();
        std::cout<<"{\"record\":\"h0_qa\",\"geometry_md30\":"<<(geometry?"true":"false")
                 <<",\"A_C_same_B_distinct_bytes_equal\":"<<(pointerAndLoop?"true":"false")
                 <<",\"K1_vs_Kmax16_and_Q4_reference\":"<<(pointerAndLoop?"true":"false")
                 <<",\"persistent_8P_barriers_no_qpc\":"<<(team?"true":"false")
                  <<",\"smt_shared_shard_partition_no_qpc\":"<<(smt?"true":"false")
                  <<",\"D5_smt_only_weights_shared_private_activations_output_eviction_timing\":"<<(smt?"true":"false")
                  <<",\"D4_B_independent_non_aliasing_allocations_byte_equal_pages_touched\":"<<(pointerAndLoop?"true":"false")
                 <<",\"non_overlapping_ranges\":"<<(pointerAndLoop?"true":"false")
                 <<",\"byte_equality\":"<<(pointerAndLoop?"true":"false")
                 <<",\"all_pages_touched\":"<<(pointerAndLoop?"true":"false")
                 <<",\"B_allocation_contract\":\"INDEPENDENT_NON_ALIASING_ALLOCATIONS\""
                 <<",\"eviction_buffers_policy\":"<<(eviction?"true":"false")
                 <<",\"calibration_policy_pure\":"<<(calibration?"true":"false")
                 <<",\"variant_order_seeded\":"<<(order?"true":"false")
                 <<",\"p95_nearest_rank_n31_index29_n20_index18\":"<<(nearestRank?"true":"false")
                 <<",\"D3_sample_cost_team_max_31rep_median_iqr_mad\":"<<(repStats?"true":"false")
                 <<",\"D2_AA_rep_episode_local_interleaving\":"<<(aaOrder?"true":"false")
                 <<",\"measurement\":false,\"official_timing\":false,\"status\":" 
                  <<((geometry&&pointerAndLoop&&team&&smt&&eviction&&calibration&&order&&nearestRank&&repStats&&aaOrder)?"\"PASS\"":"\"FAIL\"")<<"}\n";
        return geometry&&pointerAndLoop&&team&&smt&&eviction&&calibration&&order&&nearestRank&&repStats&&aaOrder?0:1;
    }catch(const std::exception& ex){std::cerr<<"h0_qa_error: "<<ex.what()<<"\n";return 1;}
}
