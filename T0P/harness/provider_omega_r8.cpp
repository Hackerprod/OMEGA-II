#include "harness.hpp"
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
    output_=s.output;
    if(output_<=0 || output_>omega_q4::kOutputMax || output_%8) throw std::invalid_argument("OMEGA O inválido/alineación");
    c_=t0common::generate(s.m,t0common::Fixture::Uniform);
    // A common::Cell currently carries O=1024 rows; only the kernel's output argument is variable.
    if(output_!=t0common::O) throw std::invalid_argument("fixture gobernante common::Cell solo aporta O=1024");
    omega_q4::prepare_weights(c_,output_,w_);
    omega_q4::prepare_activations(c_,output_,x_);
    y_.resize(std::size_t(s.m)*output_);
    if(!w_.bytes.is_large() || !x_.bytes.is_large() || !y_.is_large()) throw std::runtime_error("buffer no respaldado por large pages");
  }
  void run_once(unsigned,int m) override { omega_q4::run(w_,x_,output_,y_.data()); (void)m; }
  bool validate(std::string& why) override {
    std::vector<std::int8_t>wq,xq; std::vector<std::uint16_t>ws,xs;
    if(!omega_q4::qa_reconstruct(w_,wq,ws) || !omega_q4::qa_reconstruct_activations(x_,xq,xs) ||
       !t0common::exact_representation(c_,[&](auto&a,auto&b,auto&d,auto&e){a=wq;b=ws;d=xq;e=xs;return true;})) { why="representación exacta FAIL"; return false; }
    std::vector<std::int32_t> dots(size_t(c_.M)*output_*omega_q4::kGroups);omega_q4::qa_group_dots(w_,x_,dots.data());for(int m=0;m<c_.M;++m)for(int o=0;o<output_;++o)for(int g=0;g<omega_q4::kGroups;++g){std::int32_t d=0;for(int k=0;k<32;++k)d+=int(c_.wq[o*t0common::I+g*32+k])*int(c_.xq[m*t0common::I+g*32+k]);if(dots[(size_t(m)*output_+o)*omega_q4::kGroups+g]!=d){why="dots enteros exactos FAIL";return false;}}
    omega_q4::run(w_,x_,output_,y_.data());
    std::vector<float> out(y_.data(),y_.data()+y_.size());
    if(!t0common::verify_s12(c_,out).pass) { why="gate numérico S12 FAIL"; return false; }
    why="finite=PASS representation=PASS exact_dots=PASS tolerance=PASS"; return true;
  }
  std::vector<Region> bytes_regions() const override { return {{w_.bytes.data(),w_.bytes.size(),w_.bytes.is_large(),"W"},{x_.bytes.data(),x_.bytes.size(),x_.bytes.is_large(),"X_prepared"},{y_.data(),y_.size()*sizeof(float),y_.is_large(),"Y"}}; }
  t0common::Cell& cell() override { return c_; }
};
std::unique_ptr<Provider> make_omega(){return std::make_unique<Omega>();}
}
