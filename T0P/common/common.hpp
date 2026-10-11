#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <utility>
#include <cstddef>
#include <cstring>

namespace t0common {
struct PageRegion {
    void* data=nullptr; std::size_t bytes=0; bool large=false;
    PageRegion()=default; explicit PageRegion(std::size_t n);
    PageRegion(const PageRegion&)=delete; PageRegion& operator=(const PageRegion&)=delete;
    PageRegion(PageRegion&&) noexcept; PageRegion& operator=(PageRegion&&) noexcept; ~PageRegion();
    void reset(std::size_t n=0);
};
bool enable_large_page_privilege(std::string* status=nullptr);
template<class T> class Buffer {
    PageRegion region_; std::size_t size_=0;
public:
    Buffer()=default; Buffer(const Buffer&)=delete; Buffer& operator=(const Buffer&)=delete;
    Buffer(Buffer&&)=default; Buffer& operator=(Buffer&&)=default;
    Buffer& operator=(const std::vector<T>& v){resize(v.size());std::copy(v.begin(),v.end(),begin());return *this;}
    void resize(std::size_t n){ if(n<=size_){size_=n;return;} region_.reset(n*sizeof(T)); size_=n; }
    T* data(){return static_cast<T*>(region_.data);}
    const T* data()const{return static_cast<const T*>(region_.data);}
    std::size_t size()const{return size_;} bool empty()const{return size_==0;}
    T* begin(){return data();} T* end(){return data()+size_;} const T* begin()const{return data();} const T* end()const{return data()+size_;}
    T& operator[](std::size_t i){return data()[i];} const T& operator[](std::size_t i)const{return data()[i];}
    bool is_large()const{return region_.large;} bool operator==(const Buffer&b)const{return size_==b.size_&&std::equal(begin(),end(),b.begin());}
};
constexpr int O=1024, I=512, G=16;
enum class Fixture { Uniform, MixedCancellation, VariedScales, Sub127, Extremes, ZeroInput, SingleCoordinate, CancellationHigh, CancellationExtreme, ExactCancellation, Count };
struct Cell {
    int M=0; Fixture fixture=Fixture::Uniform;
    std::vector<std::int8_t> wq, xq;
    std::vector<std::uint16_t> ws_half, xs_half;
    std::vector<float> ws, xs;
    std::string sha256;
};
struct Verification { bool pass=false; double max_abs=0, max_rel=0; std::size_t failures=0; };
struct Ref64 { std::vector<double> r64, S, Splus, B; };
struct Failure { int m=0,o=0; double r64=0,y=0,error=0,S=0,B=0,error_over_B=0,S_over_abs_r=0; };
struct S12Verification { bool pass=false; std::size_t failures=0; double max_error_over_B=0; std::vector<Failure> failed; };
const char* fixture_name(Fixture);
Cell generate(int M, Fixture);
Cell generate_single_coordinate(int M, int coordinate);
void save_cell(const Cell&, const std::filesystem::path&);
Cell load_cell(const std::filesystem::path&);
std::vector<float> reference(const Cell&);
Ref64 reference64(const Cell&);
Verification verify(const Cell&, const std::vector<float>& output);
S12Verification verify_s12(const Cell&, const std::vector<float>& output);
bool exact_representation(const Cell&, const std::function<bool(std::vector<std::int8_t>&,std::vector<std::uint16_t>&,std::vector<std::int8_t>&,std::vector<std::uint16_t>&)>&);
bool exact_group_dots(const Cell&, const std::function<bool(std::vector<std::int32_t>&)>&);
bool s12_compare(double r64,double S,double B,double y);
void verifier_unit_tests();
std::vector<std::uint8_t> ggml_q4_0(const Cell&); // [O,G] block_q4_0: q+8, ggml half scale
std::vector<std::uint8_t> ggml_q8_0(const Cell&); // [M,G] scale FP16 then original int8 bytes
std::vector<std::uint8_t> omega_q4_18(const Cell&); // signed nibble Q4, low j then high j+1, scale at +16
}





