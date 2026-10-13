#pragma once
#include "common.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace harness::r17 {
constexpr int I=512,G=16;

struct Rng { uint64_t x=0x6a09e667f3bcc909ULL; uint64_t next(){uint64_t z=(x+=0x9e3779b97f4a7c15ULL);z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);} };
inline uint16_t f32half(float f){uint32_t x;std::memcpy(&x,&f,4);uint32_t sign=(x>>16)&0x8000,e=(x>>23)&255,m=x&0x7fffff;if(e==255)return uint16_t(sign|0x7c00|(m?0x200:0));int he=int(e)-112;if(he>=31)return uint16_t(sign|0x7c00);if(he<=0){if(he < -10)return uint16_t(sign);m|=0x800000;unsigned shift=14-he;return uint16_t(sign+((m+(1u<<(shift-1))-1+((m>>shift)&1))>>shift));}m+=0xfff+((m>>13)&1);if(m&0x800000){m=0;++he;if(he>=31)return uint16_t(sign|0x7c00);}return uint16_t(sign|(unsigned(he)<<10)|(m>>13));}
inline float half32(uint16_t h){unsigned s=h>>15,e=(h>>10)&31,m=h&1023;if(!e)return std::ldexp(float(m),-24)*(s?-1.f:1.f);if(e==31)return m?NAN:(s?-INFINITY:INFINITY);return (s?-1.f:1.f)*std::ldexp(1.f+float(m)/1024.f,int(e)-15);}

inline t0common::Cell generate_cell(int m,int o){
  if((m!=1&&m!=4&&m!=8&&m!=16)||o<8||o>58256||o%8)throw std::invalid_argument("dynamic cell geometry invalid");
  t0common::Cell c;c.M=m;c.fixture=t0common::Fixture::Uniform;c.wq.resize(size_t(o)*I);c.ws_half.resize(size_t(o)*G);c.xq.resize(size_t(m)*I);c.xs_half.resize(size_t(m)*G);Rng r;r.x^=(uint64_t(m)<<24);
  for(auto&v:c.wq)v=int8_t(int(r.next()%15)-7);for(auto&v:c.xq)v=int8_t(int(r.next()%255)-127);
  for(auto&v:c.ws_half)v=f32half(0.25f+float(r.next()%100)/97.f);for(auto&v:c.xs_half)v=f32half(0.3f+float(r.next()%100)/101.f);
  c.ws.resize(c.ws_half.size());for(size_t i=0;i<c.ws.size();++i)c.ws[i]=half32(c.ws_half[i]);c.xs.resize(c.xs_half.size());for(size_t i=0;i<c.xs.size();++i)c.xs[i]=half32(c.xs_half[i]);
  if(o==t0common::O){auto ref=t0common::generate(m,t0common::Fixture::Uniform);if(c.wq!=ref.wq||c.xq!=ref.xq||c.ws_half!=ref.ws_half||c.xs_half!=ref.xs_half)throw std::runtime_error("dynamic fixture differs from governing O=1024 fixture");}
  return c;
}
inline std::vector<uint8_t> ggml_q4_0(const t0common::Cell&c,int o){std::vector<uint8_t>b(size_t(o)*G*18);for(int row=0;row<o;++row)for(int g=0;g<G;++g){size_t d=(size_t(row)*G+g)*18;b[d]=uint8_t(c.ws_half[row*G+g]);b[d+1]=uint8_t(c.ws_half[row*G+g]>>8);for(int j=0;j<16;++j)b[d+2+j]=uint8_t(unsigned(c.wq[row*I+g*32+j]+8)| (unsigned(c.wq[row*I+g*32+j+16]+8)<<4));}return b;}
inline std::vector<uint8_t> ggml_q8_0(const t0common::Cell&c){std::vector<uint8_t>b(size_t(c.M)*G*34);for(int m=0;m<c.M;++m)for(int g=0;g<G;++g){size_t d=(size_t(m)*G+g)*34;b[d]=uint8_t(c.xs_half[m*G+g]);b[d+1]=uint8_t(c.xs_half[m*G+g]>>8);for(int k=0;k<32;++k)b[d+2+k]=uint8_t(c.xq[m*I+g*32+k]);}return b;}
inline std::vector<float> torch_x(const t0common::Cell&c){std::vector<float>x(size_t(c.M)*I);for(int m=0;m<c.M;++m)for(int i=0;i<I;++i)x[size_t(m)*I+i]=float(c.xq[size_t(m)*I+i])*c.xs[size_t(m)*G+i/32];return x;}
inline std::vector<float> torch_w(const t0common::Cell&c,int o){std::vector<float>w(size_t(o)*I);for(int row=0;row<o;++row)for(int i=0;i<I;++i)w[size_t(row)*I+i]=float(c.wq[size_t(row)*I+i])*c.ws[size_t(row)*G+i/32];return w;}
inline double up_add(double a,double b){return std::nextafter(a+b,std::numeric_limits<double>::infinity());}
inline double up_mul(double a,double b){return std::nextafter(a*b,std::numeric_limits<double>::infinity());}
inline double up_div(double a,double b){return std::nextafter(a/b,std::numeric_limits<double>::infinity());}
inline double down_sub(double a,double b){return std::nextafter(a-b,-std::numeric_limits<double>::infinity());}
inline double gamma_up(int n,double u){double nu=up_mul(double(n),u);return up_div(nu,down_sub(1.0,nu));}
struct Numeric {bool pass=false;size_t failures=0;double max_error_over_B=0;};
inline Numeric verify_s12(const t0common::Cell&c,int o,const float*y,size_t n){Numeric v;size_t want=size_t(c.M)*o;if(n!=want){v.failures=n>want?n-want:want-n;return v;}v.pass=true;const double u64=std::ldexp(1.0,-53),u32=std::ldexp(1.0,-24);const double g514_64=gamma_up(514,u64),g18_64=gamma_up(18,u64),g514_32=gamma_up(514,u32);for(int m=0;m<c.M;++m)for(int row=0;row<o;++row){double sum=0,shat=0;for(int g=0;g<G;++g){int32_t dot=0;for(int k=0;k<32;++k)dot+=int(c.wq[size_t(row)*I+g*32+k])*int(c.xq[size_t(m)*I+g*32+k]);double scale=double(c.ws[size_t(row)*G+g])*double(c.xs[size_t(m)*G+g]);sum+=double(dot)*scale;for(int k=0;k<32;++k){double term=(double(c.wq[size_t(row)*I+g*32+k])*double(c.ws[size_t(row)*G+g]))*(double(c.xq[size_t(m)*I+g*32+k])*double(c.xs[size_t(m)*G+g]));shat+=std::abs(term);}}double splus=up_div(shat,down_sub(1.0,g514_64));double bound=up_mul(up_add(g514_32,g18_64),splus);double value=y[size_t(m)*o+row];bool ok=t0common::s12_compare(sum,shat,bound,value);if(!ok){++v.failures;v.pass=false;}else if(bound>0)v.max_error_over_B=std::max(v.max_error_over_B,std::abs(value-sum)/bound);}return v;}
}
