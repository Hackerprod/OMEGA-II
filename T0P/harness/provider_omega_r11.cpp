#include "harness_r11.hpp"
#include "provider_common_r11.hpp"
#include "q4_kernel.hpp"
#include <stdexcept>

namespace harness {
class Omega final : public Provider {
  t0common::Cell c_;
  omega_q4::PackedWeights w_;
  omega_q4::PackedActivations x_;
  t0common::Buffer<float> y_;
  int output_=0;
public:
  const char* name() const noexcept override { return "omega_q4"; }
  void prepare(const CellSpec& s,unsigned) override {
    output_=s.output;c_=r11::generate_cell(s.m,output_);
    omega_q4::prepare_weights(c_,output_,w_);omega_q4::prepare_activations(c_,output_,x_);y_.resize(std::size_t(s.m)*output_);
    if(!w_.bytes.is_large()||!x_.bytes.is_large()||!y_.is_large())throw std::runtime_error("OMEGA buffer outside large pages");
  }
  void run_once(unsigned,int) override { omega_q4::run(w_,x_,output_,y_.data()); }
  bool validate(std::string& why) override {
    std::vector<std::int8_t>wq,xq;std::vector<std::uint16_t>ws,xs;
    if(!omega_q4::qa_reconstruct(w_,wq,ws)||!omega_q4::qa_reconstruct_activations(x_,xq,xs)||wq!=c_.wq||ws!=c_.ws_half||xq!=c_.xq||xs!=c_.xs_half){why="OMEGA exact representation FAIL";return false;}
    std::vector<std::int32_t>dots(size_t(c_.M)*output_*r11::G);omega_q4::qa_group_dots(w_,x_,dots.data());
    for(int m=0;m<c_.M;++m)for(int o=0;o<output_;++o)for(int g=0;g<r11::G;++g){int32_t d=0;for(int k=0;k<32;++k)d+=int(c_.wq[size_t(o)*r11::I+g*32+k])*int(c_.xq[size_t(m)*r11::I+g*32+k]);if(dots[(size_t(m)*output_+o)*r11::G+g]!=d){why="OMEGA exact group dots FAIL";return false;}}
    run_once(0,c_.M);auto v=r11::verify_s12(c_,output_,y_.data(),y_.size());if(!v.pass){why="OMEGA S12 FAIL";return false;}
    why="finite=PASS representation=PASS exact_dots=PASS S12=PASS O="+std::to_string(output_);return true;
  }
  std::vector<Region> bytes_regions() const override {return{{w_.bytes.data(),w_.bytes.size(),w_.bytes.is_large(),"W"},{x_.bytes.data(),x_.bytes.size(),x_.bytes.is_large(),"X_prepared"},{y_.data(),y_.size()*sizeof(float),y_.is_large(),"Y"}};}
  t0common::Cell& cell() override {return c_;}
};
std::unique_ptr<Provider> make_omega(){return std::make_unique<Omega>();}
}
