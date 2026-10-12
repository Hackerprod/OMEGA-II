#define main qualify_torch_entry_unused
#include "../ref_torch/adapter/ref_torch.cpp"
#undef main
#include "harness.hpp"
#include <mutex>

namespace harness {
class TorchOut final : public Provider {
  t0common::Cell c_;
  t0common::Buffer<float> xbuf_,wbuf_,wtbuf_,ybuf_;
  at::Tensor X_,W_,Wt_,Y_;
  std::string route_="mm";
  static void init(){static std::once_flag once;std::call_once(once,[]{configure();});}
public:
  const char*name()const noexcept override{return "torch_out";}
  void prepare(const CellSpec&s,unsigned)override{
    if(s.output!=t0common::O)throw std::runtime_error("Torch congelado requiere O=1024");route_=s.variant;if(route_!="mm"&&route_!="matmul"&&route_!="linear"&&route_!="mv")throw std::runtime_error("Torch route no congelada");if(route_=="mv"&&s.m!=1)throw std::runtime_error("Torch mv requiere m=1");init();c_=t0common::generate(s.m,t0common::Fixture::Uniform);
    std::vector<float>x(size_t(s.m)*t0common::I),w=unpack_weights(c_,false),wt=unpack_weights(c_,true);
    for(int m=0;m<s.m;++m)for(int i=0;i<t0common::I;++i)x[size_t(m)*t0common::I+i]=float(c_.xq[size_t(m)*t0common::I+i])*c_.xs[size_t(m)*t0common::G+i/32];
    xbuf_=x;wbuf_=w;wtbuf_=wt;ybuf_.resize(size_t(s.m)*t0common::O);std::fill(ybuf_.begin(),ybuf_.end(),0.f);
    auto opt=at::TensorOptions().dtype(at::kFloat).device(at::kCPU);
    X_=at::from_blob(xbuf_.data(),{s.m,t0common::I},[](void*){},opt);W_=at::from_blob(wbuf_.data(),{t0common::O,t0common::I},[](void*){},opt);Wt_=at::from_blob(wtbuf_.data(),{t0common::I,t0common::O},[](void*){},opt);Y_=route_=="mv"?at::from_blob(ybuf_.data(),{t0common::O},[](void*){},opt):at::from_blob(ybuf_.data(),{s.m,t0common::O},[](void*){},opt);
    if(!X_.defined()||!Y_.defined()||Y_.data_ptr<float>()!=ybuf_.data())throw std::runtime_error("Torch _out storage bind failed");
  }
  void run_once(unsigned,int)override{if(route_=="mm")at::mm_out(Y_,X_,Wt_);else if(route_=="matmul")at::matmul_out(Y_,X_,Wt_);else if(route_=="linear")at::linear_out(Y_,X_,W_,std::optional<at::Tensor>());else at::mv_out(Y_,W_,X_[0]);}
  bool validate(std::string&why)override{std::vector<float>ex(size_t(c_.M)*t0common::I),ew=unpack_weights(c_,false),ewt=unpack_weights(c_,true);for(int m=0;m<c_.M;++m)for(int i=0;i<t0common::I;++i)ex[size_t(m)*t0common::I+i]=float(c_.xq[size_t(m)*t0common::I+i])*c_.xs[size_t(m)*t0common::G+i/32];bool rep=std::equal(ex.begin(),ex.end(),xbuf_.begin())&&std::equal(ew.begin(),ew.end(),wbuf_.begin())&&std::equal(ewt.begin(),ewt.end(),wtbuf_.begin());
    auto sx=std::vector<float>(xbuf_.begin(),xbuf_.end()),sw=std::vector<float>(wbuf_.begin(),wbuf_.end()),swt=std::vector<float>(wtbuf_.begin(),wtbuf_.end());auto probe=c_;std::fill(probe.ws_half.begin(),probe.ws_half.end(),0x3c00);std::fill(probe.ws.begin(),probe.ws.end(),1.f);std::fill(probe.xs_half.begin(),probe.xs_half.end(),0x3c00);std::fill(probe.xs.begin(),probe.xs.end(),1.f);for(int m=0;m<probe.M;++m)for(int k=32;k<t0common::I;++k)probe.xq[m*t0common::I+k]=0;auto px=std::vector<float>(size_t(probe.M)*t0common::I);for(int m=0;m<probe.M;++m)for(int k=0;k<t0common::I;++k)px[size_t(m)*t0common::I+k]=float(probe.xq[size_t(m)*t0common::I+k]);auto pw=unpack_weights(probe,false),pwt=unpack_weights(probe,true);std::copy(px.begin(),px.end(),xbuf_.begin());std::copy(pw.begin(),pw.end(),wbuf_.begin());std::copy(pwt.begin(),pwt.end(),wtbuf_.begin());run_once(0,c_.M);bool dots=true;for(int m=0;m<c_.M;++m)for(int o=0;o<t0common::O;++o){int32_t d=0;for(int k=0;k<32;++k)d+=int32_t(c_.wq[o*t0common::I+k])*int32_t(c_.xq[m*t0common::I+k]);dots&=ybuf_[size_t(m)*t0common::O+o]==float(d);}std::copy(sx.begin(),sx.end(),xbuf_.begin());std::copy(sw.begin(),sw.end(),wbuf_.begin());std::copy(swt.begin(),swt.end(),wtbuf_.begin());run_once(0,c_.M);std::vector<float>y(ybuf_.data(),ybuf_.data()+ybuf_.size());if(Y_.data_ptr<float>()!=ybuf_.data()||!rep||!dots||!t0common::verify_s12(c_,y).pass){why="storage _out, representación, dots enteros o S12 FAIL";return false;}why="finite=PASS representation=PASS exact_dots=PASS tolerance=PASS; direct_dispatch=at::"+route_+"_out";return true;}
  std::vector<Region>bytes_regions()const override{return{{xbuf_.data(),xbuf_.size()*sizeof(float),xbuf_.is_large(),"X"},{wbuf_.data(),wbuf_.size()*sizeof(float),wbuf_.is_large(),"W"},{wtbuf_.data(),wtbuf_.size()*sizeof(float),wtbuf_.is_large(),"Wt"},{ybuf_.data(),ybuf_.size()*sizeof(float),ybuf_.is_large(),"Y"}};}
  t0common::Cell&cell()override{return c_;}
};
std::unique_ptr<Provider>make_torch_out(){return std::make_unique<TorchOut>();}
}
