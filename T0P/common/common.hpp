#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <functional>

namespace t0common {
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
