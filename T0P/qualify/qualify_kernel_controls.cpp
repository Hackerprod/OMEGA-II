#include "../kernel/q4_kernel.hpp"
#include <cstring>
#include <immintrin.h>
#include <iostream>

namespace {
bool activation_planes(const t0common::Cell& c, const omega_q4::PackedActivations& a) {
    const std::size_t qbytes = std::size_t(c.M) * t0common::G * 8 * sizeof(std::uint32_t);
    const std::size_t plane = std::size_t(c.M) * t0common::G * sizeof(std::uint32_t);
    const std::size_t scale_off = qbytes;
    const std::size_t sum_off = qbytes + plane;
    if (a.m != c.M || a.bytes.size() != std::size_t(c.M) * 672) return false;
    for (int m = 0; m < c.M; ++m) for (int g = 0; g < t0common::G; ++g) {
        const std::size_t ix = std::size_t(m) * t0common::G + g;
        float got_scale = 0;
        std::int32_t got_sum = 0, expected_sum = 0;
        std::memcpy(&got_scale, a.bytes.data() + scale_off + ix * sizeof(float), sizeof(float));
        std::memcpy(&got_sum, a.bytes.data() + sum_off + ix * sizeof(std::int32_t), sizeof(got_sum));
        std::uint16_t got_half = 0;
        const std::size_t half_off = sum_off + plane;
        std::memcpy(&got_half, a.bytes.data() + half_off + ix * sizeof(got_half), sizeof(got_half));
        const float expected_scale = _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(c.xs_half[ix])));
        for (int k = 0; k < 32; ++k) expected_sum += c.xq[std::size_t(m) * t0common::I + g * 32 + k];
        if (std::memcmp(&got_scale, &expected_scale, sizeof(float)) != 0 || got_sum != expected_sum ||
            got_half != c.xs_half[ix]) return false;
    }
    return true;
}
}

int main() try {
    std::string privilege;
    if (!t0common::enable_large_page_privilege(&privilege)) throw std::runtime_error("large page privilege unavailable");
    t0common::verifier_unit_tests();
    bool all = true;
    for (int m : {1, 4, 8, 16}) {
        for (int fi = 0; fi < int(t0common::Fixture::Count); ++fi) {
            const auto c = t0common::generate(m, t0common::Fixture(fi));
            omega_q4::PackedActivations a;
            omega_q4::prepare_activations(c, a);
            const bool baseline = activation_planes(c, a);
            if (!baseline) throw std::runtime_error("Q1 activation plane baseline failed");
            const std::size_t qbytes = std::size_t(m) * t0common::G * 8 * sizeof(std::uint32_t);
            const std::size_t scale_byte = qbytes;
            const std::size_t sum_byte = qbytes + std::size_t(m) * t0common::G * sizeof(float);
            a.bytes[scale_byte] ^= 1u;
            const bool rejects_scale = !activation_planes(c, a);
            a.bytes[scale_byte] ^= 1u;
            a.bytes[sum_byte] ^= 1u;
            const bool rejects_sum = !activation_planes(c, a);
            if (!rejects_scale || !rejects_sum) throw std::runtime_error("Q1 negative mutation was accepted");
            a.bytes[sum_byte] ^= 1u;
            std::cout << "DRY_KERNEL_NEGATIVES m=" << m << " fixture=" << t0common::fixture_name(c.fixture)
                      << " fp32_byte=" << (rejects_scale ? "REJECTED" : "ACCEPTED")
                      << " sum_byte=" << (rejects_sum ? "REJECTED" : "ACCEPTED") << " exact=true\n";
            all &= baseline;
        }
        const auto c = t0common::generate(m, t0common::Fixture::Uniform);
        omega_q4::PackedWeights w; omega_q4::PackedActivations a; t0common::Buffer<float> y;
        omega_q4::prepare_weights(c, w); omega_q4::prepare_activations(c, a); y.resize(std::size_t(m) * t0common::O);
        omega_q4::run(w, a, y.data());
        std::vector<float> out(y.begin(), y.end());
        const auto v = t0common::verify_s12(c, out);
        const bool buffers_large = w.bytes.is_large() && a.bytes.is_large() && y.is_large();
        const bool pass = v.pass && buffers_large;
        std::cout << "DRY_KERNEL_CONTROL m=" << m << " Q1_FP32_SCALE=PASS Q1_INT32_SUM=PASS"
                  << " negative_scale=REJECTED negative_sum=REJECTED S12=" << (v.pass ? "PASS" : "FAIL")
                  << " large_pages=" << (buffers_large ? "PASS" : "FAIL") << " timing_fields=null\n";
        all &= pass;
    }
    std::cout << "DRY_KERNEL_CONTROLS status=" << (all ? "PASS" : "FAIL") << " timed=false\n";
    return all ? 0 : 41;
} catch (const std::exception& e) { std::cerr << "DRY_KERNEL_CONTROLS_ERROR " << e.what() << "\n"; return 42; }
