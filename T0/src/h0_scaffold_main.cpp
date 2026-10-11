#include "h0_scaffold.hpp"
#include <iostream>
#include <string>

int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--describe"){
        std::cout<<"{\"record\":\"H0_scaffold\",\"status\":\"SCAFFOLD_ONLY_WAITING_FOR_OFFICIAL_FREEZE\","
                    "\"primary_isa\":\"AVX-VNNI-256\",\"workers\":8,\"SMT\":false,"
                    "\"geometry\":\"[O,512]; O and W_real follow MD/30 table\","
                    "\"m_tile\":\"1/2/4\",\"official_timing_mode\":\"requires_fresh_AUTORIZADO\",\"measurement\":false}\n";
        return 0;
    }
    if(argc==5&&std::string(argv[1])=="--prepare-variant-order"){
        try{
            const auto seed=std::stoull(argv[2],nullptr,16);
            t0h0::writeVariantOrderManifest(argv[3],argv[4],seed);
            std::cout<<"{\"record\":\"h0_variant_order_prepared\",\"seed_hex\":\""<<argv[2]
                     <<"\",\"measurement\":false}\n";
            return 0;
        }catch(const std::exception& ex){std::cerr<<"h0_variant_order_error: "<<ex.what()<<"\n";return 1;}
    }
    if(argc==7&&std::string(argv[1])=="--eviction-test"){
        if(std::string(argv[2])!="AUTORIZADO"){
            std::cerr<<"H0 eviction validation requires the exact AUTORIZADO token\n";return 3;
        }
        try{t0h0::runIndependentEvictionTestToFile(argv[3],argv[4],std::stoull(argv[5]),std::stoull(argv[6],nullptr,16));return 0;}
        catch(const std::exception& ex){std::cerr<<"h0_eviction_error: "<<ex.what()<<"\n";return 1;}
    }
    if(argc==13&&std::string(argv[1])=="--run-block"){
        if(std::string(argv[2])!="AUTORIZADO"){
            std::cerr<<"H0 official block mode requires the exact AUTORIZADO token\n";return 3;
        }
        try{
            t0h0::CellConfig c{};c.cell_id=argv[5];c.setup=argv[6];
            std::vector<t0h0::Variant> variants;
            for(char code:std::string(argv[7])){
                if(code=='A')variants.push_back(t0h0::Variant::A);
                else if(code=='B')variants.push_back(t0h0::Variant::B);
                else if(code=='C')variants.push_back(t0h0::Variant::C);
                else throw std::invalid_argument("variant sequence must contain only A/B/C");
            }
            if(variants.empty())throw std::invalid_argument("variant sequence is empty");
            c.variant=variants.front();c.W_target_bytes=std::stoull(argv[8]);
            c.K=static_cast<std::uint32_t>(std::stoul(argv[9]));c.m=static_cast<std::uint32_t>(std::stoul(argv[10]));
            c.data_seed=std::stoull(argv[11],nullptr,16);c.eviction_mib=std::stoull(argv[12]);
            t0h0::runH0Block(argv[3],argv[4],c,variants);return 0;
        }catch(const std::exception& ex){std::cerr<<"h0_block_error: "<<ex.what()<<"\n";return 1;}
    }
    std::cerr<<"H0 scaffold; official variant blocks require a fresh AUTORIZADO token. Usage: h0_scaffold.exe --describe\n";
    return argc==1?0:2;
}
