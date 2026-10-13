#include "harness_r17.hpp"
#include "provider_common_r17.hpp"
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
    output_=s.output;c_=r17::generate_cell(s.m,output_);auto qw=r17::ggml_q4_0(c_,output_),qx=r17::ggml_q8_0(c_);w_=qw;x_=qx;y_.resize(size_t(s.m)*output_);
    ggml_init_params ip{64*1024*1024,nullptr,true};ctx_=ggml_init(ip);if(!ctx_)throw std::runtime_error("ggml_init");
    weight_=ggml_new_tensor_2d(ctx_,GGML_TYPE_Q4_0,r17::I,output_);input_=ggml_new_tensor_2d(ctx_,GGML_TYPE_Q8_0,r17::I,s.m);if(!weight_||!input_)throw std::runtime_error("ggml tensor metadata allocation");
    weight_->data=w_.data();input_->data=x_.data();result_=ggml_mul_mat(ctx_,weight_,input_);if(!result_)throw std::runtime_error("ggml_mul_mat");result_->data=y_.data();graph_=ggml_new_graph(ctx_);ggml_build_forward_expand(graph_,result_);
    auto tp=ggml_threadpool_params_default(1);pool_=ggml_threadpool_new(&tp);if(!pool_)throw std::runtime_error("ggml threadpool");plan_=ggml_graph_plan(graph_,1,pool_);plan_.use_ref=false;work_.resize(plan_.work_size);plan_.work_data=work_.empty()?nullptr:reinterpret_cast<uint8_t*>(work_.data());
    if(weight_->data!=w_.data()||input_->data!=x_.data()||result_->data!=y_.data())throw std::runtime_error("ggml tensor storage bind failed");
  }
  void run_once(unsigned,int)override{compute();}
  void run_once_frontier(unsigned worker,int m,const std::string&frontier)override{
    if(frontier=="dynamic"){
      const auto& c=cell();
      for(int row=0;row<m;++row)for(int g=0;g<r17::G;++g){const size_t b=(size_t(row)*r17::G+g)*34;const uint16_t h=c.xs_half[size_t(row)*r17::G+g];x_[b]=uint8_t(h);x_[b+1]=uint8_t(h>>8);for(int k=0;k<32;++k)x_[b+2+k]=uint8_t(c.xq[size_t(row)*r17::I+g*32+k]);}
    } else if(frontier!="prepared") throw std::runtime_error("unknown ggml flat frontier");
    compute();(void)worker;
  }
  std::uint64_t dynamic_prep_bytes()const noexcept override{return std::uint64_t(x_.size())*2;}
  bool validate(std::string&why)override{
    const auto* q4_traits=ggml_get_type_traits_cpu(GGML_TYPE_Q4_0);
    if(!q4_traits||!q4_traits->vec_dot||q4_traits->vec_dot_type!=GGML_TYPE_Q8_0){why="ggml Q4_0 vec_dot/Q8_0 trait missing on worker";return false;}
    if(c_.M>=4&&(!ggml_cpu_has_llamafile()||!ggml_cpu_has_avx2())){why="frozen ggml llamafile_sgemm route unavailable for m>=4";return false;}
    std::vector<int8_t>wq(size_t(output_)*r17::I),xq(size_t(c_.M)*r17::I);std::vector<uint16_t>ws(size_t(output_)*r17::G),xs(size_t(c_.M)*r17::G);
    for(int o=0;o<output_;++o)for(int g=0;g<r17::G;++g){size_t b=(size_t(o)*r17::G+g)*18;ws[o*r17::G+g]=uint16_t(w_[b]|(uint16_t(w_[b+1])<<8));for(int j=0;j<16;++j){wq[size_t(o)*r17::I+g*32+j]=int8_t((w_[b+2+j]&15)-8);wq[size_t(o)*r17::I+g*32+j+16]=int8_t((w_[b+2+j]>>4)-8);}}
    for(int m=0;m<c_.M;++m)for(int g=0;g<r17::G;++g){size_t b=(size_t(m)*r17::G+g)*34;xs[m*r17::G+g]=uint16_t(x_[b]|(uint16_t(x_[b+1])<<8));for(int k=0;k<32;++k)xq[size_t(m)*r17::I+g*32+k]=int8_t(x_[b+2+k]);}
    if(wq!=c_.wq||ws!=c_.ws_half||xq!=c_.xq||xs!=c_.xs_half){why="ggml Q4_0/Q8_0 exact representation FAIL";return false;}
    compute();auto v=r17::verify_s12(c_,output_,y_.data(),y_.size());if(!v.pass){why="ggml S12 FAIL";return false;}
    why=std::string("finite=PASS representation=PASS S12=PASS worker_trait=Q4_0_vec_dot_Q8_0 dispatch=")+
        (c_.M==1?"vec_dot(m=1)":"llamafile_sgemm(m>=4)")+" use_ref=false contiguous_src1=true worker_threads=1 O="+std::to_string(output_);return true;
  }
  std::vector<Region>bytes_regions()const override{std::vector<Region>r{{w_.data(),w_.size(),w_.is_large(),"W_Q4_0"},{x_.data(),x_.size(),x_.is_large(),"X_Q8_0"},{y_.data(),y_.size()*sizeof(float),y_.is_large(),"Y"}};if(!work_.empty())r.push_back({work_.data(),work_.size(),work_.is_large(),"ggml_work"});return r;}
  t0common::Cell&cell()override{return c_;}
};
std::unique_ptr<Provider>make_ggml_flat(){return std::make_unique<GgmlFlat>();}
}
