#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>

namespace t0common {
constexpr int O=1024, I=512, G=16;
enum class Fixture { Uniform, MixedCancellation, VariedScales, Sub127, Extremes, Count };
struct Cell {
    int M=0; Fixture fixture=Fixture::Uniform;
    std::vector<std::int8_t> wq, xq;
    std::vector<std::uint16_t> ws_half, xs_half;
    std::vector<float> ws, xs;
    std::string sha256;
};
struct Verification { bool pass=false; double max_abs=0, max_rel=0; std::size_t failures=0; };
const char* fixture_name(Fixture);
Cell generate(int M, Fixture);
void save_cell(const Cell&, const std::filesystem::path&);
Cell load_cell(const std::filesystem::path&);
std::vector<float> reference(const Cell&);
Verification verify(const Cell&, const std::vector<float>& output);
void verifier_unit_tests();
std::vector<std::uint8_t> ggml_q4_0(const Cell&); // [O,G] block_q4_0: q+8, ggml half scale
std::vector<std::uint8_t> ggml_q8_0(const Cell&); // [M,G] scale FP16 then original int8 bytes
std::vector<std::uint8_t> omega_q4_18(const Cell&); // signed nibble Q4, low j then high j+1, scale at +16
}
