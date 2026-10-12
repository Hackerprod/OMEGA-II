#define main qualify_torch_entry_unused
#include "../ref_torch/adapter/ref_torch.cpp"
#undef main
#include "harness_r9.hpp"
#include "provider_common_r9.hpp"
#include <mutex>

namespace harness {
class TorchOut final:public Provider{
  t0common::Cell c_;
  t0common::Buffer<float>xbuf_,wbuf_,ybuf_;
  at::Tensor X_,W_,Wt_,Y_;
  std::vector<at::Tensor>xrows_,yrows_;
  std::string route_="mm";int output_=0;
  static void init(){static std::once_flag once;std::call_once(once,[]{configure();});}
public:
  const char*name()const noexcept override{return "torch_out";}
  void prepare(const CellSpec&s,unsigned)override{
    route_=s.variant;output_=s.output;if(route_!="mm"&&route_!="matmul"&&route_!="linear"&&route_!="mv")throw std::runtime_error("unknown Torch _out route");
    init();c_=r9::generate_cell(s.m,output_);auto xv=r9::torch_x(c_),wv=r9::torch_w(c_,output_);xbuf_=xv;wbuf_=wv;ybuf_.resize(size_t(s.m)*output_);std::fill(ybuf_.begin(),ybuf_.end(),0.f);
    auto opt=at::TensorOptions().dtype(at::kFloat).device(at::kCPU);X_=at::from_blob(xbuf_.data(),{s.m,r9::I},[](void*){},opt);W_=at::from_blob(wbuf_.data(),{output_,r9::I},[](void*){},opt);Wt_=W_.transpose(0,1);Y_=at::from_blob(ybuf_.data(),{s.m,output_},[](void*){},opt);
    if(!X_.defined()||!W_.defined()||!Wt_.defined()||!Y_.defined()||Y_.data_ptr<float>()!=ybuf_.data())throw std::runtime_error("Torch _out large-page storage bind failed");
    xrows_.clear();yrows_.clear();if(route_=="mv")for(int m=0;m<s.m;++m){xrows_.push_back(X_[m]);yrows_.push_back(Y_[m]);}
  }
  void run_once(unsigned,int)override{
    if(route_=="mm")at::mm_out(Y_,X_,Wt_);
    else if(route_=="matmul")at::matmul_out(Y_,X_,Wt_);
    else if(route_=="linear")at::linear_out(Y_,X_,W_,std::optional<at::Tensor>());
    else for(size_t m=0;m<xrows_.size();++m)at::mv_out(yrows_[m],W_,xrows_[m]);
  }
  bool validate(std::string&why)override{
    auto ex=r9::torch_x(c_),ew=r9::torch_w(c_,output_);bool representation=std::equal(ex.begin(),ex.end(),xbuf_.begin())&&std::equal(ew.begin(),ew.end(),wbuf_.begin());run_once(0,c_.M);
    if(Y_.data_ptr<float>()!=ybuf_.data()||!representation){why="Torch _out storage/exact Q4 dequantization FAIL";return false;}
    auto v=r9::verify_s12(c_,output_,ybuf_.data(),ybuf_.size());if(!v.pass){why="Torch S12 FAIL";return false;}
    why="finite=PASS representation=PASS S12=PASS direct_dispatch=at::"+route_+"_out Y_large_page=PASS";return true;
  }
  std::vector<Region>bytes_regions()const override{return{{xbuf_.data(),xbuf_.size()*sizeof(float),xbuf_.is_large(),"X"},{wbuf_.data(),wbuf_.size()*sizeof(float),wbuf_.is_large(),"W"},{ybuf_.data(),ybuf_.size()*sizeof(float),ybuf_.is_large(),"Y"}};}
  t0common::Cell&cell()override{return c_;}
};
std::unique_ptr<Provider>make_torch_out(){return std::make_unique<TorchOut>();}
}
