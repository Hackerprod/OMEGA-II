#include "ggml.h"
#include "ggml-cpu.h"
#include "q4_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int64_t O = 1024, I = 512;
struct Q4Block { ggml_fp16_t d; uint8_t qs[16]; };
struct Q8Block { ggml_fp16_t d; int8_t qs[32]; };
static_assert(sizeof(Q4Block) == 18 && sizeof(Q8Block) == 34);

int q4(const uint8_t *p, int j) {
    const unsigned n = (p[j / 2] >> ((j & 1) * 4)) & 15;
    return n >= 8 ? int(n) - 16 : int(n);
}

void run(int64_t m) {
    auto w = t0q4::generate_weights(O, I, 0x23456789abcdef01ULL);
    auto a = t0q4::generate_activations(m, I, 0x1020304050607080ULL);
    const int groups = int(I / 32);
    std::vector<Q4Block> qw(size_t(O) * groups);
    for (int64_t o = 0; o < O; ++o) for (int g = 0; g < groups; ++g) {
        const uint8_t *src = w.storage.data() + (size_t(o) * groups + g) * 18;
        Q4Block &dst = qw[size_t(o) * groups + g];
        std::memcpy(&dst.d, src + 16, 2);
        // ggml Q4_0 stores q[0..15] in low nibbles and q[16..31] in high nibbles.
        for (int j = 0; j < 16; ++j) {
            const unsigned lo = unsigned(q4(src, j) + 8), hi = unsigned(q4(src, j + 16) + 8);
            dst.qs[j] = uint8_t(lo | (hi << 4));
        }
    }
    std::vector<Q8Block> qa(size_t(m) * groups);
    for (int64_t row = 0; row < m; ++row) for (int g = 0; g < groups; ++g) {
        Q8Block &dst = qa[size_t(row) * groups + g];
        const float scale = a.scale[size_t(row) * groups + g];
        dst.d = ggml_fp32_to_fp16(scale);
        if (ggml_fp16_to_fp32(dst.d) != scale) throw std::runtime_error("Q8 scale no representable en FP16");
        std::memcpy(dst.qs, a.q.data() + size_t(row) * I + size_t(g) * 32, 32);
    }

    ggml_init_params params{16 * 1024 * 1024, nullptr, false};
    ggml_context *ctx = ggml_init(params);
    if (!ctx) throw std::runtime_error("ggml_init fallo");
    ggml_tensor *tw = ggml_new_tensor_2d(ctx, GGML_TYPE_Q4_0, I, O);
    ggml_tensor *ta = ggml_new_tensor_2d(ctx, GGML_TYPE_Q8_0, I, m);
    tw->data = qw.data(); ta->data = qa.data();
    ggml_tensor *out = ggml_mul_mat(ctx, tw, ta);
    ggml_cgraph *graph = ggml_new_graph(ctx);
    ggml_build_forward_expand(graph, out);
    const ggml_status status = ggml_graph_compute_with_ctx(ctx, graph, 1);
    if (status != GGML_STATUS_SUCCESS) { ggml_free(ctx); throw std::runtime_error("graph compute fallo"); }

    const float *actual = static_cast<const float *>(out->data);
    double max_error = 0, max_ratio = 0;
    size_t mismatches = 0;
    for (int64_t row = 0; row < m; ++row) for (int64_t o = 0; o < O; ++o) {
        float ref = 0;
        for (int g = 0; g < groups; ++g) {
            const uint8_t *src = w.storage.data() + (size_t(o) * groups + g) * 18;
            int32_t dot = 0;
            for (int j = 0; j < 32; ++j)
                dot += q4(src, j) * int(a.q[size_t(row) * I + size_t(g) * 32 + j]);
            const float ws = ggml_fp16_to_fp32(ggml_fp16_t(src[16] | (uint16_t(src[17]) << 8)));
            ref += float(dot) * ws * a.scale[size_t(row) * groups + g];
        }
        const float err = std::abs(actual[size_t(row) * O + o] - ref);
        const float tol = 1e-5f + 1e-5f * std::abs(ref);
        max_error = std::max(max_error, double(err));
        max_ratio = std::max(max_ratio, double(err / tol));
        if (!(err <= tol)) ++mismatches;
    }
    std::cout << "m=" << m << " op=ggml_mul_mat CPU n_threads=1"
              << " avx2=" << ggml_cpu_has_avx2() << " avx_vnni=" << ggml_cpu_has_avx_vnni()
              << " route=ggml_vec_dot_q4_0_q8_0->mul_sum_us8_pairs_float(AVX-VNNI vpdpbusd)"
              << " gemm=repack-not-selected"
              << " max_abs_error=" << std::setprecision(9) << max_error
              << " max_error_over_tolerance=" << max_ratio << " mismatches=" << mismatches << std::endl;
    ggml_free(ctx);
    if (mismatches) throw std::runtime_error("equivalencia fuera de tolerancia");
}
}

int main() {
    std::cout << "CPU features: AVX2=" << ggml_cpu_has_avx2() << " AVX-VNNI=" << ggml_cpu_has_avx_vnni() << std::endl;
    for (int m : {1, 4, 8, 16}) run(m);
    return 0;
}
