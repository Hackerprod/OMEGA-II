#include "harness_r10.hpp"
#include "provider_common_r10.hpp"
#include "repack.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <mutex>

extern "C" { extern float ggml_table_f32_f16[1<<16]; }

namespace harness {
namespace {
struct Q4x8 { uint16_t d[8]; uint8_t qs[128]; };
struct Q8x4 { uint16_t d[4]; int8_t qs[128]; };
static_assert(sizeof(Q4x8)==144 && sizeof(Q8x4)==136);
void ensure_ggml_fp16_table(){static std::once_flag once;std::call_once(once,[]{for(unsigned i=0;i<(1u<<16);++i)ggml_table_f32_f16[i]=ggml_fp16_to_fp32((ggml_fp16_t)i);});}
}

class GgmlRepack final : public Provider {
  t0common::Cell c_;
  t0common::Buffer<uint8_t> weights_, x_flat_, x_pack_;
  t0common::Buffer<float> y_;
  int output_=0;
  void dispatch() {
    const void* x=c_.M==1?static_cast<const void*>(x_flat_.data()):static_cast<const void*>(x_pack_.data());
    if(c_.M==1) ggml_gemv_q4_0_8x8_q8_0(r10::I,y_.data(),size_t(output_),weights_.data(),x,c_.M,output_);
    else ggml_gemm_q4_0_8x8_q8_0(r10::I,y_.data(),size_t(output_),weights_.data(),x,c_.M,output_);
  }
public:
  const char* name() const noexcept override { return "ggml_repack"; }
  void prepare(const CellSpec&s,unsigned) override {
    ensure_ggml_fp16_table();
    if(s.m!=1&&s.m!=4&&s.m!=8&&s.m!=16) throw std::runtime_error("ggml repack M must be 1,4,8,16");
    if(s.output%8) throw std::runtime_error("ggml repack O must be divisible by 8");
    output_=s.output;c_=r10::generate_cell(s.m,output_);
    weights_.resize(size_t(output_/8)*r10::G*sizeof(Q4x8));
    for(int ot=0;ot<output_/8;++ot) for(int g=0;g<r10::G;++g) {
      auto*d=reinterpret_cast<Q4x8*>(weights_.data())+size_t(ot)*r10::G+g;
      for(int j=0;j<8;++j)d->d[j]=c_.ws_half[(ot*8+j)*r10::G+g];
      for(int i=0;i<16;++i){const int row=i%8,off=(i/8)*8,o=ot*8+row;for(int b=0;b<8;++b){const int j=off+b;uint8_t q=uint8_t((unsigned(c_.wq[size_t(o)*r10::I+g*32+j]+8)&15)|((unsigned(c_.wq[size_t(o)*r10::I+g*32+j+16]+8)&15)<<4));d->qs[i*8+b]=q^0x88;}}
    }
    y_.resize(size_t(s.m)*output_);
    if(s.m==1){x_flat_=r10::ggml_q8_0(c_);}
    else {
      x_pack_.resize(size_t(s.m/4)*r10::G*sizeof(Q8x4));
      for(int rt=0;rt<s.m/4;++rt)for(int g=0;g<r10::G;++g){auto*d=reinterpret_cast<Q8x4*>(x_pack_.data())+size_t(rt)*r10::G+g;for(int row=0;row<4;++row)d->d[row]=c_.xs_half[(rt*4+row)*r10::G+g];for(int i=0;i<16;++i){const int row=i%4,off=(i/4)*8;for(int b=0;b<8;++b)d->qs[i*8+b]=c_.xq[size_t((rt*4+row))*r10::I+g*32+off+b];}}
    }
  }
  void run_once(unsigned,int) override { dispatch(); }
  bool validate(std::string&why) override {
    std::vector<int8_t>wq(size_t(output_)*r10::I),xq(size_t(c_.M)*r10::I);
    std::vector<uint16_t>ws(size_t(output_)*r10::G),xs(size_t(c_.M)*r10::G);
    for(int ot=0;ot<output_/8;++ot)for(int g=0;g<r10::G;++g){const auto*d=reinterpret_cast<const Q4x8*>(weights_.data())+size_t(ot)*r10::G+g;for(int j=0;j<8;++j)ws[(ot*8+j)*r10::G+g]=d->d[j];for(int i=0;i<16;++i){const int row=i%8,off=(i/8)*8;uint64_t v;std::memcpy(&v,d->qs+i*8,8);v^=0x8888888888888888ULL;for(int b=0;b<8;++b){const uint8_t q=uint8_t(v>>(8*b));wq[size_t(ot*8+row)*r10::I+g*32+off+b]=int8_t((q&15)-8);wq[size_t(ot*8+row)*r10::I+g*32+off+b+16]=int8_t((q>>4)-8);}}}
    if(c_.M==1){for(int g=0;g<r10::G;++g){const size_t b=size_t(g)*34;xs[g]=uint16_t(x_flat_[b]|(uint16_t(x_flat_[b+1])<<8));for(int k=0;k<32;++k)xq[g*32+k]=int8_t(x_flat_[b+2+k]);}}
    else for(int rt=0;rt<c_.M/4;++rt)for(int g=0;g<r10::G;++g){const auto*d=reinterpret_cast<const Q8x4*>(x_pack_.data())+size_t(rt)*r10::G+g;for(int row=0;row<4;++row){xs[(rt*4+row)*r10::G+g]=d->d[row];for(int k=0;k<32;++k)xq[size_t(rt*4+row)*r10::I+g*32+k]=d->qs[(k/8*4+row)*8+k%8];}}
    if(wq!=c_.wq||ws!=c_.ws_half||xq!=c_.xq||xs!=c_.xs_half){why="ggml repack exact Q4_0x8/Q8_0x4 representation FAIL";return false;}
    dispatch();auto v=r10::verify_s12(c_,output_,y_.data(),y_.size());if(!v.pass){auto ref=t0common::reference(c_);size_t ix=0;while(ix<ref.size()&&ix<y_.size()&&double(ref[ix])==double(y_[ix]))++ix;std::ostringstream s;s<<"ggml repack S12 FAIL failures="<<v.failures<<" max_error_over_B="<<v.max_error_over_B<<" M="<<c_.M<<" O="<<output_;if(ix<ref.size()&&ix<y_.size())s<<" first_mismatch="<<ix<<" y="<<y_[ix]<<" ref="<<ref[ix];s<<" first_scale_w="<<unsigned(weights_[0])<<":"<<unsigned(weights_[1])<<" first_code="<<unsigned(weights_[2]);if(c_.M==1)s<<" x_scale="<<unsigned(x_flat_[0])<<":"<<unsigned(x_flat_[1])<<" x_code="<<unsigned(x_flat_[2]);else s<<" x_scale="<<unsigned(x_pack_[0])<<":"<<unsigned(x_pack_[1])<<" x_code="<<unsigned(x_pack_[8]);why=s.str();return false;}
    why="finite=PASS representation=PASS S12=PASS dispatch=ggml_gemv/gemm_q4_0_8x8_q8_0 O="+std::to_string(output_);return true;
  }
  std::vector<Region> bytes_regions()const override {
    std::vector<Region>r{{weights_.data(),weights_.size(),weights_.is_large(),"W_Q4_0x8"},{y_.data(),y_.size()*sizeof(float),y_.is_large(),"Y"}};
    if(c_.M==1)r.push_back({x_flat_.data(),x_flat_.size(),x_flat_.is_large(),"X_Q8_0"});else r.push_back({x_pack_.data(),x_pack_.size(),x_pack_.is_large(),"X_Q8_0x4"});return r;
  }
  t0common::Cell& cell()override{return c_;}
};

std::unique_ptr<Provider> make_ggml_repack(){return std::make_unique<GgmlRepack>();}
}
