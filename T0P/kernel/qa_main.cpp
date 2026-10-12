#define NOMINMAX
#include "q4_kernel.hpp"

#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using t0common::Cell;
using t0common::Fixture;
int O = t0common::O;
constexpr int I = t0common::I, G = t0common::G;
constexpr int output_sweep[] = {912,1024,1824,2728,3640,4552,5464,6368,7280,9104,10920,14560,21848,29128,43688,58256};
std::size_t identity_checks = 0, identity_failures = 0;
std::filesystem::path baseline_path;
const Fixture all_fixtures[] = {Fixture::Uniform, Fixture::MixedCancellation, Fixture::VariedScales,
    Fixture::Sub127, Fixture::Extremes, Fixture::ZeroInput, Fixture::SingleCoordinate,
    Fixture::CancellationHigh, Fixture::CancellationExtreme, Fixture::ExactCancellation};
const Fixture group_fixtures[] = {Fixture::Uniform, Fixture::MixedCancellation, Fixture::VariedScales,
    Fixture::Sub127, Fixture::Extremes};

bool avx_vnni_available() {
    int r[4]{};
    __cpuid(r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0;
    const bool avx = (r[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (_xgetbv(0) & 0x6) != 0x6) return false;
    __cpuidex(r, 7, 0);
    if ((r[1] & (1 << 5)) == 0) return false; // AVX2
    __cpuidex(r, 7, 1);
    return (r[0] & (1 << 4)) != 0; // AVX-VNNI (256 bit)
}

bool scales_finite(const Cell& c) {
    for (float x : c.ws) if (!std::isfinite(x)) return false;
    for (float x : c.xs) if (!std::isfinite(x)) return false;
    for (auto q : c.wq) if (q < -8 || q > 7) return false;
    return true;
}

bool query_pages(const char* fixture, int m, const char* buffer, const char* function,
                 const std::string& alias, const void* ptr, std::size_t bytes, bool large_region) {
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    const std::uintptr_t p = si.dwPageSize;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(ptr);
    bool ok = ptr && bytes && p && bytes <= UINTPTR_MAX - a;
    std::size_t expected = 0, valid = 0, large = 0;
    BOOL queried = FALSE;
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> ws;
    if (ok) {
        expected = static_cast<std::size_t>(((a % p) + bytes + p - 1) / p);
        const std::uintptr_t start = a - (a % p);
        ws.resize(expected);
        for (std::size_t i = 0; i < ws.size(); ++i) ws[i].VirtualAddress = reinterpret_cast<void*>(start + i * p);
        queried = QueryWorkingSetEx(GetCurrentProcess(), ws.data(), DWORD(ws.size() * sizeof(ws[0])));
        if (queried) for (const auto& item : ws) {
            valid += item.VirtualAttributes.Valid != 0;
            large += item.VirtualAttributes.LargePage != 0;
        }
    }
    const std::size_t expected_bytes = std::string(buffer) == "weights" ? omega_q4::weight_bytes(O) :
        (std::string(buffer) == "activations" ? omega_q4::activation_bytes(m) :
         (std::string(buffer) == "Y" ? omega_q4::output_bytes(m, O) : std::size_t(64) * m * O));
    const bool pass = ok && bytes == expected_bytes && queried && !ws.empty() && valid == ws.size() && large == ws.size() && large_region;
    std::cout << "PAGE fixture=" << fixture << " m=" << m << " O=" << O << " candidate=omega_q4 buffer=" << buffer
              << " function=" << function << " alias=" << alias << "_o" << O << " address=0x" << std::hex << a << std::dec
              << " bytes=" << bytes << " queried_pages=" << (queried ? ws.size() : 0)
              << " valid_pages=" << valid << " large_pages=" << large
              << " QueryWorkingSetEx=" << (queried ? "PASS" : "FAIL")
              << " LargePage=" << ((large_region && large == ws.size() && !ws.empty()) ? "true" : "false") << "\n";
    return pass;
}

bool equal_bits(const std::vector<std::uint16_t>& a, const std::vector<std::uint16_t>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

void expand_output_rows(Cell& c, int output) {
    const auto old_wq = c.wq; const auto old_wh = c.ws_half; const auto old_ws = c.ws;
    c.wq.resize(std::size_t(output) * I);
    c.ws_half.resize(std::size_t(output) * G); c.ws.resize(std::size_t(output) * G);
    for (int o = 0; o < output; ++o) {
        const unsigned source = output == t0common::O ? unsigned(o) :
            (unsigned(o) * 1664525u + 1013904223u + unsigned(output) + 0x4f4d4547u) % t0common::O;
        std::copy_n(old_wq.data() + std::size_t(source) * I, I, c.wq.data() + std::size_t(o) * I);
        std::copy_n(old_wh.data() + std::size_t(source) * G, G, c.ws_half.data() + std::size_t(o) * G);
        std::copy_n(old_ws.data() + std::size_t(source) * G, G, c.ws.data() + std::size_t(o) * G);
    }
}

std::size_t mapped_source(int o, int output) {
    return output == t0common::O ? std::size_t(o) :
        (std::size_t(unsigned(o) * 1664525u + 1013904223u + unsigned(output) + 0x4f4d4547u) % t0common::O);
}

t0common::Ref64 variable_reference(const Cell& original, int output) {
    const auto base = t0common::reference64(original);
    t0common::Ref64 result;
    const std::size_t n = std::size_t(original.M) * output;
    result.r64.resize(n); result.S.resize(n); result.Splus.resize(n); result.B.resize(n);
    for (int m = 0; m < original.M; ++m) for (int o = 0; o < output; ++o) {
        const auto src = std::size_t(m) * t0common::O + mapped_source(o, output);
        const auto dst = std::size_t(m) * output + o;
        result.r64[dst] = base.r64[src]; result.S[dst] = base.S[src];
        result.Splus[dst] = base.Splus[src]; result.B[dst] = base.B[src];
    }
    return result;
}

t0common::S12Verification variable_verify_s12(const t0common::Ref64& ref, const std::vector<float>& y) {
    t0common::S12Verification v; v.pass = y.size() == ref.r64.size();
    if (!v.pass) { v.failures = y.size() > ref.r64.size() ? y.size()-ref.r64.size() : ref.r64.size()-y.size(); return v; }
    for (std::size_t i = 0; i < y.size(); ++i) {
        if (!t0common::s12_compare(ref.r64[i], ref.S[i], ref.B[i], y[i])) ++v.failures;
        if (ref.B[i] != 0) v.max_error_over_B = (std::max)(v.max_error_over_B, std::abs(double(y[i])-ref.r64[i])/ref.B[i]);
    }
    v.pass = v.failures == 0; return v;
}

t0common::Verification variable_verify_old(const t0common::Ref64& ref, const std::vector<float>& y) {
    t0common::Verification v{};
    if (y.size() != ref.r64.size()) { v.failures = y.size() > ref.r64.size() ? y.size()-ref.r64.size() : ref.r64.size()-y.size(); return v; }
    for (std::size_t i=0;i<y.size();++i) {
        const double error=std::abs(double(y[i])-ref.r64[i]);
        if (!std::isfinite(y[i]) || !std::isfinite(ref.r64[i]) || error > 1e-5+1e-5*std::abs(ref.r64[i])) ++v.failures;
    }
    v.pass=v.failures==0; return v;
}

std::size_t exact_outputs(const Cell& c, const float* y, const std::vector<double>& ref) {
    std::size_t failures = 0;
    for (std::size_t i = 0; i < ref.size(); ++i) if (double(y[i]) != ref[i]) ++failures;
    return failures;
}

void print_s12_failures(const char* route, const Cell& c, const t0common::S12Verification& v) {
    for (const auto& f : v.failed)
        std::cout << "S12_FAIL route=" << route << " fixture=" << t0common::fixture_name(c.fixture)
                  << " m=" << f.m << " o=" << f.o << " r64=" << std::setprecision(17) << f.r64
                  << " y=" << f.y << " abs=" << f.error << " S=" << f.S << " B=" << f.B
                  << " error_over_B=" << f.error_over_B << " S_over_abs_r=" << f.S_over_abs_r << "\n";
}

bool negative_representation_checks(const Cell& c, omega_q4::PackedWeights& w,
                                    omega_q4::PackedActivations& x,
                                    bool& rejects_fp32_byte, bool& rejects_sum_byte) {
    std::vector<std::int8_t> wq, xq;
    std::vector<std::uint16_t> wh, xh;
    if (!omega_q4::qa_reconstruct(w, wq, wh) || !omega_q4::qa_reconstruct_activations(x, xq, xh)) return false;
    const auto original_code = w.bytes[0]; w.bytes[0] ^= 1;
    const bool code_rejected = !omega_q4::qa_reconstruct(w, wq, wh) || wq != c.wq;
    w.bytes[0] = original_code;
    auto* scale_bits = reinterpret_cast<std::uint16_t*>(w.bytes.data() + omega_q4::code_bytes(w.output));
    const auto original_scale = scale_bits[0]; scale_bits[0] ^= 1;
    const bool scale_rejected = !omega_q4::qa_reconstruct(w, wq, wh) || wh != c.ws_half;
    scale_bits[0] = original_scale;
    auto* act_q = reinterpret_cast<std::uint32_t*>(x.bytes.data());
    const auto original_x = act_q[0]; act_q[0] ^= 1;
    const bool xcode_rejected = !omega_q4::qa_reconstruct_activations(x, xq, xh) || xq != c.xq;
    act_q[0] = original_x;
    const std::size_t xq_bytes = std::size_t(c.M) * G * 8 * sizeof(std::uint32_t);
    const std::size_t xs_bytes = std::size_t(c.M) * G * sizeof(float);
    const std::size_t sum_bytes = std::size_t(c.M) * G * sizeof(std::int32_t);
    auto* scale_plane = x.bytes.data() + xq_bytes;
    scale_plane[0] ^= 1;
    rejects_fp32_byte = !omega_q4::qa_reconstruct_activations(x, xq, xh);
    scale_plane[0] ^= 1;
    auto* sum_plane = x.bytes.data() + xq_bytes + xs_bytes;
    sum_plane[0] ^= 1;
    rejects_sum_byte = !omega_q4::qa_reconstruct_activations(x, xq, xh);
    sum_plane[0] ^= 1;
    auto* xhalf = reinterpret_cast<std::uint16_t*>(x.bytes.data() + xq_bytes + xs_bytes + sum_bytes);
    const auto original_xscale = xhalf[0]; xhalf[0] ^= 1;
    const bool xscale_rejected = !omega_q4::qa_reconstruct_activations(x, xq, xh) || !equal_bits(xh, c.xs_half);
    xhalf[0] = original_xscale;
    return code_rejected && scale_rejected && xcode_rejected && xscale_rejected &&
           rejects_fp32_byte && rejects_sum_byte &&
           omega_q4::qa_reconstruct(w, wq, wh) && wq == c.wq && equal_bits(wh, c.ws_half) &&
           omega_q4::qa_reconstruct_activations(x, xq, xh) && xq == c.xq && equal_bits(xh, c.xs_half);
}

bool run_cells(const std::string& data_dir, bool& pages_pass) {
    bool pass = true;
    std::size_t numerical_checks = 0, numerical_failures = 0, old_checks = 0, old_failures = 0;
    std::size_t negative_checks = 0, negative_failures = 0, exact_zero_checks = 0, exact_zero_failures = 0;
    std::size_t finite_checks = 0, finite_failures = 0;
    bool privilege = false;
    std::string privilege_state;
    privilege = t0common::enable_large_page_privilege(&privilege_state);
    std::cout << "LARGE_PAGE_PRIVILEGE=" << privilege_state << "\n";
    pages_pass = privilege;
    for (Fixture f : all_fixtures) for (int m : {1, 4, 8, 16}) {
        const std::string name = "m" + std::to_string(m) + "_" + t0common::fixture_name(f) + ".bin";
        const auto path = std::filesystem::path(data_dir) / name;
        Cell c = t0common::load_cell(path);
        const Cell generated = t0common::generate(m, f);
        if (c.M != m || c.fixture != f || c.sha256 != generated.sha256 || !scales_finite(c)) {
            std::cout << "CELL_INPUT fixture=" << t0common::fixture_name(f) << " m=" << m << " status=FAIL hash=" << c.sha256 << "\n";
            pass = false; continue;
        }
        expand_output_rows(c, O);
        omega_q4::PackedWeights w;
        omega_q4::PackedActivations x;
        t0common::Buffer<float> y;
        omega_q4::prepare_weights(c, O, w);
        omega_q4::prepare_activations(c, O, x);
        y.resize(omega_q4::output_bytes(m, O) / sizeof(float));
        omega_q4::run(w, x, O, y.data());

        std::vector<std::int8_t> rw, rx;
        std::vector<std::uint16_t> rws, rxs;
        const bool repr = omega_q4::qa_reconstruct(w, rw, rws) && rw == c.wq && equal_bits(rws, c.ws_half) &&
                          omega_q4::qa_reconstruct_activations(x, rx, rxs) && rx == c.xq && equal_bits(rxs, c.xs_half);
        bool rejects_fp32_byte = false, rejects_sum_byte = false;
        const bool negative = negative_representation_checks(c, w, x, rejects_fp32_byte, rejects_sum_byte);
        ++negative_checks; if (!negative) ++negative_failures;
        const std::vector<float> output(y.begin(), y.end());
        bool bit_identity = true;
        if (O == t0common::O) {
            const std::size_t m_index = m == 1 ? 0 : m == 4 ? 1 : m == 8 ? 2 : 3;
            const std::size_t prior_m = m_index == 0 ? 0 : m_index == 1 ? 1 : m_index == 2 ? 5 : 13;
            const std::size_t offset = (std::size_t(f) * 29 + prior_m) * O * sizeof(float);
            std::ifstream baseline(baseline_path, std::ios::binary);
            std::vector<float> frozen(output.size());
            if (!baseline || output.size() != std::size_t(m) * O) bit_identity = false;
            else { baseline.seekg(std::streamoff(offset)); baseline.read(reinterpret_cast<char*>(frozen.data()), std::streamsize(frozen.size() * sizeof(float))); bit_identity = baseline.good() && std::memcmp(frozen.data(), output.data(), frozen.size() * sizeof(float)) == 0; }
            ++identity_checks; if (!bit_identity) ++identity_failures;
            std::cout << "BIT_IDENTITY O=1024 fixture=" << t0common::fixture_name(f) << " m=" << m
                      << " values=" << output.size() << " status=" << (bit_identity ? "PASS" : "FAIL") << "\n";
        }
        const auto ref = variable_reference(generated, O);
        const auto s12 = variable_verify_s12(ref, output);
        const auto old = variable_verify_old(ref, output);
        const std::size_t exact = exact_outputs(c, y.data(), ref.r64);
        bool finite = true;
        for (std::size_t i = 0; i < y.size(); ++i) finite = finite && std::isfinite(y[i]) && std::isfinite(ref.r64[i]) && std::isfinite(ref.S[i]) && std::isfinite(ref.B[i]);
        ++finite_checks; if (!finite) ++finite_failures;
        const bool zero_exact = f != Fixture::ZeroInput && f != Fixture::ExactCancellation ? true : exact == 0;
        if (f == Fixture::ZeroInput || f == Fixture::ExactCancellation) {
            exact_zero_checks += y.size(); exact_zero_failures += exact;
        }
        numerical_checks += y.size(); numerical_failures += s12.failures + (finite ? 0 : 1) + (repr ? 0 : 1) + (zero_exact ? 0 : 1);
        old_checks += y.size(); old_failures += old.failures;
        std::cout << "CELL fixture=" << t0common::fixture_name(f) << " m=" << m << " O=" << O << " route=omega_q4"
                  << " finite=" << (finite ? 1 : 0) << " S12_pass=" << (s12.pass ? 1 : 0)
                  << " S12_failures=" << s12.failures << " max_error_over_B=" << std::setprecision(12)
                  << s12.max_error_over_B << " old_tolerance_pass=" << (old.pass ? 1 : 0)
                  << " old_tolerance_failures=" << old.failures << " representation=" << (repr ? "PASS" : "FAIL")
                  << " negative_representation=" << (negative ? "PASS" : "FAIL")
                  << " negative_fp32_byte=" << (rejects_fp32_byte ? "REJECTED" : "ACCEPTED")
                  << " negative_sum_byte=" << (rejects_sum_byte ? "REJECTED" : "ACCEPTED")
                  << " exact_output_failures=" << exact
                  << " hash=" << c.sha256 << "\n";
        print_s12_failures("omega_q4", c, s12);
        pass = pass && s12.pass && finite && repr && negative && zero_exact && bit_identity;
        pages_pass &= query_pages(t0common::fixture_name(f), m, "weights", "packed_q4_and_fp16_scales",
                                  "cell_" + std::to_string(m) + "_" + t0common::fixture_name(f) + "_weights",
                                  w.bytes.data(), w.bytes.size(), w.bytes.is_large());
        pages_pass &= query_pages(t0common::fixture_name(f), m, "activations", "prepared_i8_scales_and_correction",
                                  "cell_" + std::to_string(m) + "_" + t0common::fixture_name(f) + "_activations",
                                  x.bytes.data(), x.bytes.size(), x.bytes.is_large());
        pages_pass &= query_pages(t0common::fixture_name(f), m, "Y", "kernel_output",
                                  "cell_" + std::to_string(m) + "_" + t0common::fixture_name(f) + "_Y",
                                  y.data(), y.size() * sizeof(float), y.is_large());
    }
    std::cout << "CONTROL_COUNTS control=QA_NUMERICAL checks=" << numerical_checks << " failures=" << numerical_failures << "\n";
    std::cout << "CONTROL_COUNTS control=OLD_TOLERANCE_DIAGNOSTIC checks=" << old_checks << " failures=" << old_failures << "\n";
    std::cout << "CONTROL_COUNTS control=NEGATIVE_REPRESENTATION checks=" << negative_checks << " failures=" << negative_failures << "\n";
    std::cout << "CONTROL_COUNTS control=FINITE checks=" << finite_checks << " failures=" << finite_failures << "\n";
    std::cout << "CONTROL_COUNTS control=ZERO_AND_EXACT_CANCELLATION checks=" << exact_zero_checks << " failures=" << exact_zero_failures << "\n";
    std::cout << "QA_NUMERICAL=" << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

bool single_coordinate_sweep(bool& pages_pass) {
    std::size_t checks = 0, failures = 0;
    const bool full_observation = O == t0common::O;
    for (int m : {1, 4, 8, 16}) {
        Cell base = t0common::generate_single_coordinate(m, 0);
        expand_output_rows(base, O);
        omega_q4::PackedWeights w; omega_q4::prepare_weights(base, O, w);
        omega_q4::PackedActivations x;
        t0common::Buffer<float> y; y.resize(omega_q4::output_bytes(m, O) / sizeof(float));
        for (int pos = 0; pos < I; ++pos) {
            Cell c = t0common::generate_single_coordinate(m, pos);
            omega_q4::prepare_activations(c, O, x);
            omega_q4::run(w, x, O, y.data());
            std::size_t coordinate_checks = 0, coordinate_failures = 0;
            for (int row = 0; row < m; ++row) {
                const int first = 0, last = full_observation ? O : 8;
                for (int edge = first; edge < last; ++edge) {
                    const int o = edge;
                    const std::size_t src = mapped_source(o, O);
                    const int g = pos / 32;
                    const float product = c.ws[src * G + g] * c.xs[row * G + g];
                    const float expected = std::fma(float(c.wq[src * I + pos]), product, 0.0f);
                    ++coordinate_checks;
                    if (y[std::size_t(row) * O + o] != expected) ++coordinate_failures;
                }
                if (!full_observation) {
                    for (int o = O - 8; o < O; ++o) {
                        const std::size_t src = mapped_source(o, O);
                        const int g = pos / 32;
                        const float product = c.ws[src * G + g] * c.xs[row * G + g];
                        const float expected = std::fma(float(c.wq[src * I + pos]), product, 0.0f);
                        ++coordinate_checks;
                        if (y[std::size_t(row) * O + o] != expected) ++coordinate_failures;
                    }
                }
            }
            checks += coordinate_checks;
            failures += coordinate_failures;
            std::cout << "SINGLE_SWEEP_COORD route=omega_q4.run O=" << O << " M=" << m
                      << " position=" << pos << " compared=" << coordinate_checks
                      << " failures=" << coordinate_failures << "\n";
        }
        const std::size_t per_position = std::size_t(m) * (full_observation ? O : 16);
        std::cout << "SINGLE_SWEEP route=omega_q4.run O=" << O << " M=" << m
                  << " mode=" << (full_observation ? "all_outputs" : "first8_last8")
                  << " positions=512 run_calls=512 compared=" << std::size_t(I) * per_position
                  << " checks=" << std::size_t(I) * per_position << " failures=" << failures << " exact_integer_outputs="
                  << (failures == 0 ? "PASS" : "FAIL") << "\n";
        pages_pass &= query_pages("single_coordinate_sweep", m, "weights", "packed_q4_and_fp16_scales",
                                  "single_coordinate_sweep_" + std::to_string(m) + "_weights", w.bytes.data(), w.bytes.size(), w.bytes.is_large());
        pages_pass &= query_pages("single_coordinate_sweep", m, "activations", "prepared_i8_scales_and_correction",
                                  "single_coordinate_sweep_" + std::to_string(m) + "_activations", x.bytes.data(), x.bytes.size(), x.bytes.is_large());
        pages_pass &= query_pages("single_coordinate_sweep", m, "Y", "kernel_output",
                                  "single_coordinate_sweep_" + std::to_string(m) + "_Y", y.data(), y.size() * sizeof(float), y.is_large());
    }
    std::cout << "CONTROL_COUNTS control=SINGLE_COORDINATE_SWEEP checks=" << checks << " failures=" << failures << "\n";
    return failures == 0;
}

bool exact_group_isolation(bool& pages_pass) {
    std::size_t checks = 0, failures = 0;
    std::size_t checks_by_m[4]{}, failures_by_m[4]{};
    for (int m : {1, 4, 8, 16}) for (Fixture f : group_fixtures) {
        const int mi = m == 1 ? 0 : m == 4 ? 1 : m == 8 ? 2 : 3;
        Cell base = t0common::generate(m, f);
        expand_output_rows(base, O);
        std::fill(base.ws_half.begin(), base.ws_half.end(), 0x3c00);
        std::fill(base.xs_half.begin(), base.xs_half.end(), 0x3c00);
        std::fill(base.ws.begin(), base.ws.end(), 1.0f);
        std::fill(base.xs.begin(), base.xs.end(), 1.0f);
        omega_q4::PackedWeights w; omega_q4::prepare_weights(base, O, w);
        omega_q4::PackedActivations x;
        t0common::Buffer<float> y; y.resize(omega_q4::output_bytes(m, O) / sizeof(float));
        t0common::Buffer<std::int32_t> dots; dots.resize(std::size_t(m) * O * G);
        for (int g = 0; g < G; ++g) {
            Cell isolated = base;
            for (int row = 0; row < m; ++row)
                for (int j = 0; j < I; ++j) if (j / 32 != g) isolated.xq[row * I + j] = 0;
            omega_q4::prepare_activations(isolated, O, x);
            omega_q4::qa_group_dots(w, x, dots.data());
            omega_q4::run(w, x, O, y.data());
            for (int row = 0; row < m; ++row) for (int o = 0; o < O; ++o) {
                std::int32_t oracle = 0;
                for (int k = 0; k < 32; ++k) oracle += std::int32_t(base.wq[o * I + g * 32 + k]) * std::int32_t(isolated.xq[row * I + g * 32 + k]);
                checks += G; checks_by_m[mi] += G;
                if (dots[(std::size_t(row) * O + o) * G + g] != oracle || double(y[std::size_t(row) * O + o]) != double(oracle)) { ++failures; ++failures_by_m[mi]; }
                for (int other = 0; other < G; ++other) if (other != g && dots[(std::size_t(row) * O + o) * G + other] != 0) { ++failures; ++failures_by_m[mi]; }
            }
        }
        const std::string prefix = "group_" + std::to_string(m) + "_" + t0common::fixture_name(f) + "_";
        pages_pass &= query_pages(t0common::fixture_name(f), m, "weights", "packed_q4_and_fp16_scales",
                                  prefix + "weights", w.bytes.data(), w.bytes.size(), w.bytes.is_large());
        pages_pass &= query_pages(t0common::fixture_name(f), m, "activations", "prepared_i8_scales_and_correction",
                                  prefix + "activations", x.bytes.data(), x.bytes.size(), x.bytes.is_large());
        pages_pass &= query_pages(t0common::fixture_name(f), m, "Y", "kernel_output",
                                  prefix + "Y", y.data(), y.size() * sizeof(float), y.is_large());
        pages_pass &= query_pages(t0common::fixture_name(f), m, "dots", "qa_group_dot_int32",
                                  prefix + "dots", dots.data(), dots.size() * sizeof(std::int32_t), dots.is_large());
    }
    bool pass = failures == 0;
    const int ms[4] = {1,4,8,16};
    for (int i=0;i<4;++i) {
        const bool mp = failures_by_m[i] == 0;
        std::cout << "EXACT_GROUP_DOT_ISOLATION route=omega_q4 O=" << O << " m=" << ms[i]
                  << " checks=" << checks_by_m[i] << " failures=" << failures_by_m[i]
                  << " status=" << (mp ? "PASS" : "FAIL") << "\n";
        pass = pass && mp;
    }
    std::cout << "EXACT_GROUP_DOT_ISOLATION_SUMMARY O=" << O << " checks=" << checks << " failures=" << failures
              << " status=" << (pass ? "PASS" : "FAIL") << "\n";
    std::cout << "EXACT_GROUP_DOT_COVERAGE O=" << O << " rows=all_groups_all_outputs_and_m_samples groups=16 fixtures=5 checks=" << checks << " failures=" << failures << "\n";
    return pass;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (!avx_vnni_available()) throw std::runtime_error("CPU/OS no anuncia AVX-VNNI-256; no se ejecutó la primitiva requerida");
        std::cout << "QA_START\n" << std::flush;
        if (argc != 2 && argc != 3) throw std::runtime_error("uso: qa_kernel.exe <ruta T0P/common/data> [baseline-bin]");
        if (argc == 3) baseline_path = std::filesystem::path(argv[2]);
        else {
            std::wstring exe(32768, L'\\0');
            const DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
            if (!n || n >= exe.size()) throw std::runtime_error("no se pudo resolver la ruta del ejecutable para baseline");
            exe.resize(n);
            baseline_path = std::filesystem::path(exe).parent_path() / L"logs" / L"baseline_candidate_o1024.bin";
        }
        if (!std::filesystem::exists(baseline_path)) throw std::runtime_error("baseline O=1024 no encontrado: " + baseline_path.string());
        std::cout << "BASELINE_PATH=" << baseline_path.string() << "\n";
        t0common::verifier_unit_tests();
        std::cout << "verifier_unit_tests=PASS\n" << std::flush;
        bool all = true, page = true;
        for (int output : output_sweep) {
            O = output;
            const bool numerical = run_cells(argv[1], page);
            const bool sweep = single_coordinate_sweep(page);
            const bool group = exact_group_isolation(page);
            const bool pass = numerical && sweep && group;
            std::cout << "OUTPUT_QA O=" << O << " numerical=" << (numerical ? "PASS" : "FAIL")
                      << " coordinate_sweep=" << (sweep ? "PASS" : "FAIL") << " group_isolation=" << (group ? "PASS" : "FAIL") << "\n";
            all = all && pass;
        }
        std::cout << "BIT_IDENTITY_SUMMARY O=1024 checks=" << identity_checks << " failures=" << identity_failures
                  << " status=" << (identity_checks == 40 && identity_failures == 0 ? "PASS" : "FAIL") << "\n";
        all = all && identity_checks == 40 && identity_failures == 0;
        std::cout << "REFERENCE_LIKE=" << (all ? "PASS" : "FAIL") << "\n";
        std::cout << "PAGE_POLICY=" << (page ? "PASS" : "FAIL") << "\n";
        std::cout << "MANDATORY_GATE=" << (all && page ? "PASS" : "FAIL") << "\n";
        return all && page ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        std::cout << "MANDATORY_GATE=FAIL\n";
        return 2;
    }
}
