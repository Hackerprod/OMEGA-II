#define main qualify_ggml_entry_unused
#include "../ref_ggml/adapter/main.cpp"
#undef main
#include "harness.hpp"

namespace harness {
class GgmlFlat final : public Provider {
  Prepared p_;
public:
  const char* name() const noexcept override{return "ggml_flat";}
  void prepare(const CellSpec&s,unsigned) override {
    if(s.output!=O)throw std::runtime_error("GGML congelado requiere O=1024");
    p_.c=t0common::generate(s.m,t0common::Fixture::Uniform);
    prepare_weights(p_.c,p_,CandidateRoute::Flat);
    p_.out.resize(size_t(s.m)*O);
    ggml_init_params ip{32*1024*1024,nullptr,true};p_.ctx=ggml_init(ip);if(!p_.ctx)throw std::runtime_error("ggml_init");
    p_.weight=ggml_new_tensor_2d(p_.ctx,GGML_TYPE_Q4_0,I,O);p_.input=ggml_new_tensor_2d(p_.ctx,GGML_TYPE_Q8_0,I,s.m);
    p_.weight->data=p_.flatw.data();p_.result=ggml_mul_mat(p_.ctx,p_.weight,p_.input);p_.result->data=p_.out.data();
    p_.graph=ggml_new_graph(p_.ctx);ggml_build_forward_expand(p_.graph,p_.result);
    auto tp=ggml_threadpool_params_default(1);p_.threadpool=ggml_threadpool_new(&tp);if(!p_.threadpool)throw std::runtime_error("ggml threadpool");
    p_.plan=ggml_graph_plan(p_.graph,1,p_.threadpool);p_.plan.use_ref=false;p_.work.resize(p_.plan.work_size);p_.plan.work_data=p_.work.empty()?nullptr:reinterpret_cast<uint8_t*>(p_.work.data());
    prepare_dynamic(p_,CandidateRoute::Flat);if(p_.input->data!=p_.flata.data())throw std::runtime_error("ggml X not bound to prepared large page");
  }
  void run_once(unsigned,int) override{run_once_flat(p_);}
  bool validate(std::string&why) override {run_once_flat(p_);const auto&c=p_.c;bool rep=t0common::exact_representation(c,[&](auto&w,auto&ws,auto&x,auto&xs){w.resize(O*I);ws.resize(O*G);x.resize(c.M*I);xs.resize(c.M*G);for(int o=0;o<O;++o)for(int g=0;g<G;++g){auto b=(size_t(o)*G+g)*18;ws[o*G+g]=uint16_t(p_.flatw[b]|(uint16_t(p_.flatw[b+1])<<8));for(int j=0;j<16;++j){w[o*I+g*32+j]=int8_t((p_.flatw[b+2+j]&15)-8);w[o*I+g*32+j+16]=int8_t((p_.flatw[b+2+j]>>4)-8);}}for(int m=0;m<c.M;++m)for(int g=0;g<G;++g){auto b=(size_t(m)*G+g)*34;xs[m*G+g]=uint16_t(p_.flata[b]|(uint16_t(p_.flata[b+1])<<8));for(int k=0;k<32;++k)x[m*I+g*32+k]=int8_t(p_.flata[b+2+k]);}return true;});
    Prepared q;q.c=c;std::fill(q.c.ws_half.begin(),q.c.ws_half.end(),0x3c00);std::fill(q.c.ws.begin(),q.c.ws.end(),1.f);std::fill(q.c.xs_half.begin(),q.c.xs_half.end(),0x3c00);std::fill(q.c.xs.begin(),q.c.xs.end(),1.f);for(int m=0;m<c.M;++m)for(int k=32;k<I;++k)q.c.xq[m*I+k]=0;prepare_weights(q.c,q,CandidateRoute::Flat);q.out.resize(size_t(c.M)*O);ggml_init_params ip{32*1024*1024,nullptr,true};q.ctx=ggml_init(ip);if(!q.ctx)throw std::runtime_error("exact-dot probe ggml_init");q.weight=ggml_new_tensor_2d(q.ctx,GGML_TYPE_Q4_0,I,O);q.input=ggml_new_tensor_2d(q.ctx,GGML_TYPE_Q8_0,I,c.M);q.weight->data=q.flatw.data();q.result=ggml_mul_mat(q.ctx,q.weight,q.input);q.result->data=q.out.data();q.graph=ggml_new_graph(q.ctx);ggml_build_forward_expand(q.graph,q.result);auto tp=ggml_threadpool_params_default(1);q.threadpool=ggml_threadpool_new(&tp);q.plan=ggml_graph_plan(q.graph,1,q.threadpool);q.plan.use_ref=false;q.work.resize(q.plan.work_size);q.plan.work_data=q.work.empty()?nullptr:reinterpret_cast<uint8_t*>(q.work.data());prepare_dynamic(q,CandidateRoute::Flat);run_once_flat(q);bool dots=true;for(int m=0;m<c.M;++m)for(int o=0;o<O;++o){int32_t d=0;for(int k=0;k<32;++k)d+=int32_t(c.wq[o*I+k])*int32_t(c.xq[m*I+k]);dots&=q.out[size_t(m)*O+o]==float(d);}
    std::vector<float> y(p_.out.data(),p_.out.data()+p_.out.size());if(!rep||!dots||!t0common::verify_s12(c,y).pass){why="representación exacta, dots enteros o S12 FAIL";return false;}why="finite=PASS representation=PASS exact_dots=PASS tolerance=PASS";return true;}
  std::vector<Region> bytes_regions()const override{std::vector<Region> r{{p_.flatw.data(),p_.flatw.size(),p_.flatw.is_large(),"W_flat"},{p_.flata.data(),p_.flata.size(),p_.flata.is_large(),"X"},{p_.out.data(),p_.out.size()*sizeof(float),p_.out.is_large(),"Y"}};if(p_.work.size())r.push_back({p_.work.data(),p_.work.size(),p_.work.is_large(),"ggml_work"});return r;}
  t0common::Cell&cell()override{return p_.c;}
};
std::unique_ptr<Provider>make_ggml_flat(){return std::make_unique<GgmlFlat>();}
}
