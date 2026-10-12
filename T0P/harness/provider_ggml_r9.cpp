#include "harness_r9.hpp"
#include "provider_common_r9.hpp"
#include "ggml.h"
#include "ggml-cpu.h"
#include <stdexcept>

extern "C" { float ggml_table_f32_f16[1<<16]={}; float ggml_table_f32_e8m0_half[1<<8]={}; }

namespace harness {
class GgmlFlat final : public Provider {
  t0common::Cell c_;
  t0common::Buffer<uint8_t> w_,x_;
  t0common::Buffer<float> y_;
  t0common::Buffer<std::byte> work_;
  ggml_context*ctx_=nullptr;ggml_threadpool*pool_=nullptr;ggml_tensor*weight_=nullptr;ggml_tensor*input_=nullptr;ggml_tensor*result_=nullptr;ggml_cgraph*graph_=nullptr;ggml_cplan plan_{};
  int output_=0;
  void compute(){if(ggml_graph_compute(graph_,&plan_)!=GGML_STATUS_SUCCESS)throw std::runtime_error("ggml graph compute failed");}
public:
  ~GgmlFlat()override{if(pool_)ggml_threadpool_free(pool_);if(ctx_)ggml_free(ctx_);}
  const char*name()const noexcept override{return "ggml_flat";}
  void prepare(const CellSpec&s,unsigned)override{
    output_=s.output;c_=r9::generate_cell(s.m,output_);auto qw=r9::ggml_q4_0(c_,output_),qx=r9::ggml_q8_0(c_);w_=qw;x_=qx;y_.resize(size_t(s.m)*output_);
    ggml_init_params ip{64*1024*1024,nullptr,true};ctx_=ggml_init(ip);if(!ctx_)throw std::runtime_error("ggml_init");
    weight_=ggml_new_tensor_2d(ctx_,GGML_TYPE_Q4_0,r9::I,output_);input_=ggml_new_tensor_2d(ctx_,GGML_TYPE_Q8_0,r9::I,s.m);if(!weight_||!input_)throw std::runtime_error("ggml tensor metadata allocation");
    weight_->data=w_.data();input_->data=x_.data();result_=ggml_mul_mat(ctx_,weight_,input_);if(!result_)throw std::runtime_error("ggml_mul_mat");result_->data=y_.data();graph_=ggml_new_graph(ctx_);ggml_build_forward_expand(graph_,result_);
    auto tp=ggml_threadpool_params_default(1);pool_=ggml_threadpool_new(&tp);if(!pool_)throw std::runtime_error("ggml threadpool");plan_=ggml_graph_plan(graph_,1,pool_);plan_.use_ref=false;work_.resize(plan_.work_size);plan_.work_data=work_.empty()?nullptr:reinterpret_cast<uint8_t*>(work_.data());
    if(weight_->data!=w_.data()||input_->data!=x_.data()||result_->data!=y_.data())throw std::runtime_error("ggml tensor storage bind failed");
  }
  void run_once(unsigned,int)override{compute();}
  bool validate(std::string&why)override{
    std::vector<int8_t>wq(size_t(output_)*r9::I),xq(size_t(c_.M)*r9::I);std::vector<uint16_t>ws(size_t(output_)*r9::G),xs(size_t(c_.M)*r9::G);
    for(int o=0;o<output_;++o)for(int g=0;g<r9::G;++g){size_t b=(size_t(o)*r9::G+g)*18;ws[o*r9::G+g]=uint16_t(w_[b]|(uint16_t(w_[b+1])<<8));for(int j=0;j<16;++j){wq[size_t(o)*r9::I+g*32+j]=int8_t((w_[b+2+j]&15)-8);wq[size_t(o)*r9::I+g*32+j+16]=int8_t((w_[b+2+j]>>4)-8);}}
    for(int m=0;m<c_.M;++m)for(int g=0;g<r9::G;++g){size_t b=(size_t(m)*r9::G+g)*34;xs[m*r9::G+g]=uint16_t(x_[b]|(uint16_t(x_[b+1])<<8));for(int k=0;k<32;++k)xq[size_t(m)*r9::I+g*32+k]=int8_t(x_[b+2+k]);}
    if(wq!=c_.wq||ws!=c_.ws_half||xq!=c_.xq||xs!=c_.xs_half){why="ggml Q4_0/Q8_0 exact representation FAIL";return false;}
    compute();auto v=r9::verify_s12(c_,output_,y_.data(),y_.size());if(!v.pass){why="ggml S12 FAIL";return false;}
    why="finite=PASS representation=PASS S12=PASS dispatch=ggml_mul_mat O="+std::to_string(output_);return true;
  }
  std::vector<Region>bytes_regions()const override{std::vector<Region>r{{w_.data(),w_.size(),w_.is_large(),"W_Q4_0"},{x_.data(),x_.size(),x_.is_large(),"X_Q8_0"},{y_.data(),y_.size()*sizeof(float),y_.is_large(),"Y"}};if(!work_.empty())r.push_back({work_.data(),work_.size(),work_.is_large(),"ggml_work"});return r;}
  t0common::Cell&cell()override{return c_;}
};
std::unique_ptr<Provider>make_ggml_flat(){return std::make_unique<GgmlFlat>();}
}
