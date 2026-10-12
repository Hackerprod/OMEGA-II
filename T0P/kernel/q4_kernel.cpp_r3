#include "q4_kernel.hpp"

#include <immintrin.h>
#include <cstring>

namespace omega_q4 {
namespace {

constexpr std::size_t code_offset(int ot, int g, int kp) noexcept {
    // One 32-byte vector stores two adjacent K4 blocks in low/high nibbles.
    return ((std::size_t(ot) * kGroups + g) * (kK4 / 2) + kp) * 32;
}
constexpr std::size_t scale_offset(int o, int g) noexcept {
    return kCodeBytes + ((std::size_t(o / kRows) * kGroups + g) * kRows + (o % kRows)) * sizeof(std::uint16_t);
}

inline void expand_q4_pair(const std::uint8_t* p, __m256i& lo, __m256i& hi) noexcept {
    const __m256i packed = _mm256_load_si256(reinterpret_cast<const __m256i*>(p));
    const __m256i mask = _mm256_set1_epi8(0x0f);
    lo = _mm256_and_si256(packed, mask);
    hi = _mm256_and_si256(_mm256_srli_epi16(packed, 4), mask);
}

inline __m256i dpbusd(__m256i acc, __m256i unsigned_weights, std::uint32_t activation4) noexcept {
    const __m256i x = _mm256_set1_epi32(static_cast<int>(activation4));
    return _mm256_dpbusd_avx_epi32(acc, unsigned_weights, x);
}

template<int T>
inline void run_m1_tile_chain(const std::uint8_t* w, int ot0, int g, int kp,
                             __m256i x0, __m256i x1, __m256i* dots) noexcept {
    if constexpr (T < 8) {
        __m256i wl, wh;
        expand_q4_pair(w + code_offset(ot0 + T, g, kp), wl, wh);
        dots[T] = _mm256_dpbusd_avx_epi32(dots[T], wl, x0);
        dots[T] = _mm256_dpbusd_avx_epi32(dots[T], wh, x1);
        run_m1_tile_chain<T + 1>(w, ot0, g, kp, x0, x1, dots);
    }
}

template<int R>
inline void run_m4_row_chain(const std::uint32_t* qwords, std::size_t qi,
                             __m256i wl0, __m256i wh0, __m256i wl1, __m256i wh1,
                             __m256i& d0, __m256i& d1) noexcept {
    const std::size_t ri = qi + std::size_t(R) * kGroups * kK4;
    const __m256i x0 = _mm256_set1_epi32(static_cast<int>(qwords[ri]));
    const __m256i x1 = _mm256_set1_epi32(static_cast<int>(qwords[ri + 1]));
    d0 = _mm256_dpbusd_avx_epi32(d0, wl0, x0);
    d0 = _mm256_dpbusd_avx_epi32(d0, wh0, x1);
    d1 = _mm256_dpbusd_avx_epi32(d1, wl1, x0);
    d1 = _mm256_dpbusd_avx_epi32(d1, wh1, x1);
}

template<int M_TILE>
__declspec(noinline) void run_tile(const std::uint8_t* w, const std::uint32_t* qwords, const float* xs,
                                   const std::int32_t* xsum, float* y) noexcept {
    if constexpr (M_TILE == 1) {
        // Eight output tiles share each activation broadcast; the low/high
        // K4 VPDPBUSD chains remain independent without overlapping tiles.
        for (int ot0 = 0; ot0 < kOutput / kRows; ot0 += 8) {
            const int ot = ot0;
            __m256 acc[8];
            for (int t = 0; t < 8; ++t) acc[t] = _mm256_setzero_ps();
            for (int g = 0; g < kGroups; ++g) {
                __m256i dots[8];
                for (int t = 0; t < 8; ++t) dots[t] = _mm256_setzero_si256();
                for (int kp = 0; kp < kK4 / 2; ++kp) {
                    const std::size_t qi = std::size_t(g) * kK4 + kp * 2;
                    const __m256i x0 = _mm256_set1_epi32(static_cast<int>(qwords[qi]));
                    const __m256i x1 = _mm256_set1_epi32(static_cast<int>(qwords[qi + 1]));
                    run_m1_tile_chain<0>(w, ot, g, kp, x0, x1, dots);
                }
                const std::int32_t correction = xsum[g] * 8;
                for (int t = 0; t < 8; ++t) {
                    const __m128i half_bits = _mm_load_si128(reinterpret_cast<const __m128i*>(w + scale_offset((ot + t) * kRows, g)));
                    const __m256 scale_w = _mm256_cvtph_ps(half_bits);
                    const __m256i signed_dot = _mm256_sub_epi32(dots[t], _mm256_set1_epi32(correction));
                    const __m256 dot_f = _mm256_cvtepi32_ps(signed_dot);
                    const __m256 scale = _mm256_mul_ps(scale_w, _mm256_set1_ps(xs[g]));
                    acc[t] = _mm256_fmadd_ps(dot_f, scale, acc[t]);
                }
            }
            for (int t = 0; t < 8; ++t) _mm256_store_ps(y + (ot + t) * kRows, acc[t]);
        }
    } else {
        for (int base = 0; base < M_TILE; base += 4) {
            for (int ot0 = 0; ot0 < kOutput / kRows; ot0 += 2) {
                __m256 a00 = _mm256_setzero_ps(), a01 = _mm256_setzero_ps();
                __m256 a02 = _mm256_setzero_ps(), a03 = _mm256_setzero_ps();
                __m256 a10 = _mm256_setzero_ps(), a11 = _mm256_setzero_ps();
                __m256 a12 = _mm256_setzero_ps(), a13 = _mm256_setzero_ps();
                for (int g = 0; g < kGroups; ++g) {
                    __m256i d00 = _mm256_setzero_si256(), d01 = _mm256_setzero_si256();
                    __m256i d02 = _mm256_setzero_si256(), d03 = _mm256_setzero_si256();
                    __m256i d10 = _mm256_setzero_si256(), d11 = _mm256_setzero_si256();
                    __m256i d12 = _mm256_setzero_si256(), d13 = _mm256_setzero_si256();
                    for (int kp = 0; kp < kK4 / 2; ++kp) {
                        __m256i wl0, wh0, wl1, wh1;
                        expand_q4_pair(w + code_offset(ot0, g, kp), wl0, wh0);
                        expand_q4_pair(w + code_offset(ot0 + 1, g, kp), wl1, wh1);
                        const std::size_t qi = std::size_t(g) * kK4 + kp * 2;
                        run_m4_row_chain<0>(qwords + std::size_t(base) * kGroups * kK4, qi,
                                            wl0, wh0, wl1, wh1, d00, d10);
                        run_m4_row_chain<1>(qwords + std::size_t(base) * kGroups * kK4, qi,
                                            wl0, wh0, wl1, wh1, d01, d11);
                        run_m4_row_chain<2>(qwords + std::size_t(base) * kGroups * kK4, qi,
                                            wl0, wh0, wl1, wh1, d02, d12);
                        run_m4_row_chain<3>(qwords + std::size_t(base) * kGroups * kK4, qi,
                                            wl0, wh0, wl1, wh1, d03, d13);
                    }
                    const __m128i h0 = _mm_load_si128(reinterpret_cast<const __m128i*>(w + scale_offset(ot0 * kRows, g)));
                    const __m128i h1 = _mm_load_si128(reinterpret_cast<const __m128i*>(w + scale_offset((ot0 + 1) * kRows, g)));
                    const __m256 sw0 = _mm256_cvtph_ps(h0), sw1 = _mm256_cvtph_ps(h1);
                    const __m256 c0 = _mm256_set1_ps(xs[std::size_t(base) * kGroups + g]);
                    const __m256 c1 = _mm256_set1_ps(xs[std::size_t(base + 1) * kGroups + g]);
                    const __m256 c2 = _mm256_set1_ps(xs[std::size_t(base + 2) * kGroups + g]);
                    const __m256 c3 = _mm256_set1_ps(xs[std::size_t(base + 3) * kGroups + g]);
                    const __m256i z0 = _mm256_set1_epi32(xsum[std::size_t(base) * kGroups + g] * 8);
                    const __m256i z1 = _mm256_set1_epi32(xsum[std::size_t(base + 1) * kGroups + g] * 8);
                    const __m256i z2 = _mm256_set1_epi32(xsum[std::size_t(base + 2) * kGroups + g] * 8);
                    const __m256i z3 = _mm256_set1_epi32(xsum[std::size_t(base + 3) * kGroups + g] * 8);
                    a00 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d00, z0)), _mm256_mul_ps(sw0, c0), a00);
                    a01 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d01, z1)), _mm256_mul_ps(sw0, c1), a01);
                    a02 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d02, z2)), _mm256_mul_ps(sw0, c2), a02);
                    a03 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d03, z3)), _mm256_mul_ps(sw0, c3), a03);
                    a10 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d10, z0)), _mm256_mul_ps(sw1, c0), a10);
                    a11 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d11, z1)), _mm256_mul_ps(sw1, c1), a11);
                    a12 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d12, z2)), _mm256_mul_ps(sw1, c2), a12);
                    a13 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(d13, z3)), _mm256_mul_ps(sw1, c3), a13);
                }
                _mm256_store_ps(y + std::size_t(base) * kOutput + ot0 * kRows, a00);
                _mm256_store_ps(y + std::size_t(base + 1) * kOutput + ot0 * kRows, a01);
                _mm256_store_ps(y + std::size_t(base + 2) * kOutput + ot0 * kRows, a02);
                _mm256_store_ps(y + std::size_t(base + 3) * kOutput + ot0 * kRows, a03);
                _mm256_store_ps(y + std::size_t(base) * kOutput + (ot0 + 1) * kRows, a10);
                _mm256_store_ps(y + std::size_t(base + 1) * kOutput + (ot0 + 1) * kRows, a11);
                _mm256_store_ps(y + std::size_t(base + 2) * kOutput + (ot0 + 1) * kRows, a12);
                _mm256_store_ps(y + std::size_t(base + 3) * kOutput + (ot0 + 1) * kRows, a13);
            }
        }
    }
}

template<int M_TILE>
void run_all(const PackedWeights& weights, const PackedActivations& a, float* y) noexcept {
    for (int base = 0; base < a.m; base += M_TILE) {
        // Prepared activation slices are contiguous in each of the three planes.
        const std::size_t qbytes_per_row = kGroups * kK4 * sizeof(std::uint32_t);
        const std::size_t sbytes_per_row = kGroups * sizeof(float);
        const std::size_t plane_scales = std::size_t(a.m) * qbytes_per_row;
        const std::size_t plane_sums = plane_scales + std::size_t(a.m) * sbytes_per_row;
        const auto* qall = reinterpret_cast<const std::uint32_t*>(a.bytes.data());
        const auto* sall = reinterpret_cast<const float*>(a.bytes.data() + plane_scales);
        const auto* sumall = reinterpret_cast<const std::int32_t*>(a.bytes.data() + plane_sums);
        run_tile<M_TILE>(weights.bytes.data(), qall + std::size_t(base) * kGroups * kK4,
                         sall + std::size_t(base) * kGroups, sumall + std::size_t(base) * kGroups,
                         y + std::size_t(base) * kOutput);
    }
}

} // namespace

std::size_t activation_bytes(int m) {
    return std::size_t(m) * kGroups * kK4 * sizeof(std::uint32_t) +
           std::size_t(m) * kGroups * (sizeof(float) + sizeof(std::int32_t) + sizeof(std::uint16_t));
}

void prepare_weights(const t0common::Cell& c, PackedWeights& dst) {
    dst.bytes.resize(kWeightBytes);
    auto* packed = dst.bytes.data();
    for (int ot = 0; ot < kOutput / kRows; ++ot) {
        for (int g = 0; g < kGroups; ++g) {
            for (int kp = 0; kp < kK4 / 2; ++kp) {
                auto* block = packed + code_offset(ot, g, kp);
                for (int row = 0; row < kRows; ++row) {
                    const int o = ot * kRows + row;
                    const int k = g * 32 + kp * 8;
                    const auto a0 = static_cast<std::uint8_t>(c.wq[o * kInput + k] + 8);
                    const auto a1 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 1] + 8);
                    const auto a2 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 2] + 8);
                    const auto a3 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 3] + 8);
                    const auto b0 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 4] + 8);
                    const auto b1 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 5] + 8);
                    const auto b2 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 6] + 8);
                    const auto b3 = static_cast<std::uint8_t>(c.wq[o * kInput + k + 7] + 8);
                    block[row * 4] = static_cast<std::uint8_t>(a0 | (b0 << 4));
                    block[row * 4 + 1] = static_cast<std::uint8_t>(a1 | (b1 << 4));
                    block[row * 4 + 2] = static_cast<std::uint8_t>(a2 | (b2 << 4));
                    block[row * 4 + 3] = static_cast<std::uint8_t>(a3 | (b3 << 4));
                }
            }
            for (int row = 0; row < kRows; ++row) {
                const int o = ot * kRows + row;
                std::memcpy(packed + scale_offset(o, g), &c.ws_half[o * kGroups + g], sizeof(std::uint16_t));
            }
        }
    }
}

void prepare_activations(const t0common::Cell& c, PackedActivations& dst) {
    dst.m = c.M;
    dst.bytes.resize(activation_bytes(c.M));
    const std::size_t qbytes = std::size_t(c.M) * kGroups * kK4 * sizeof(std::uint32_t);
    const std::size_t sbytes = std::size_t(c.M) * kGroups * sizeof(float);
    auto* q = reinterpret_cast<std::uint32_t*>(dst.bytes.data());
    auto* scale = reinterpret_cast<float*>(dst.bytes.data() + qbytes);
    auto* sums = reinterpret_cast<std::int32_t*>(dst.bytes.data() + qbytes + sbytes);
    auto* half = reinterpret_cast<std::uint16_t*>(dst.bytes.data() + qbytes + sbytes + std::size_t(c.M) * kGroups * sizeof(std::int32_t));
    for (int m = 0; m < c.M; ++m) {
        for (int g = 0; g < kGroups; ++g) {
            std::int32_t sum = 0;
            for (int kb = 0; kb < kK4; ++kb) {
                std::uint32_t v = 0;
                for (int j = 0; j < 4; ++j) {
                    const auto b = static_cast<std::uint8_t>(c.xq[m * kInput + g * 32 + kb * 4 + j]);
                    v |= std::uint32_t(b) << (j * 8);
                    sum += c.xq[m * kInput + g * 32 + kb * 4 + j];
                }
                q[(std::size_t(m) * kGroups + g) * kK4 + kb] = v;
            }
            scale[std::size_t(m) * kGroups + g] = c.xs[m * kGroups + g];
            sums[std::size_t(m) * kGroups + g] = sum;
            half[std::size_t(m) * kGroups + g] = c.xs_half[m * kGroups + g];
        }
    }
}

void run(const PackedWeights& weights, const PackedActivations& a, float* output) noexcept {
    switch (a.m) {
        case 1: run_all<1>(weights, a, output); break;
        case 4: run_all<4>(weights, a, output); break;
        case 8: run_all<4>(weights, a, output); break;
        case 16: run_all<4>(weights, a, output); break;
        default: break;
    }
}

void qa_group_dots(const PackedWeights& weights, const PackedActivations& a, std::int32_t* dots) noexcept {
    const auto* q = reinterpret_cast<const std::uint32_t*>(a.bytes.data());
    const auto* xsum = reinterpret_cast<const std::int32_t*>(a.bytes.data() +
        std::size_t(a.m) * kGroups * kK4 * sizeof(std::uint32_t) + std::size_t(a.m) * kGroups * sizeof(float));
    for (int m = 0; m < a.m; ++m) for (int ot = 0; ot < kOutput / kRows; ++ot) for (int g = 0; g < kGroups; ++g) {
        __m256i acc = _mm256_setzero_si256();
        for (int kp = 0; kp < kK4 / 2; ++kp) {
            __m256i wl, wh;
            expand_q4_pair(weights.bytes.data() + code_offset(ot, g, kp), wl, wh);
            const std::size_t qi = (std::size_t(m) * kGroups + g) * kK4 + kp * 2;
            acc = dpbusd(acc, wl, q[qi]);
            acc = dpbusd(acc, wh, q[qi + 1]);
        }
        acc = _mm256_sub_epi32(acc, _mm256_set1_epi32(xsum[std::size_t(m) * kGroups + g] * 8));
        alignas(32) std::int32_t lanes[kRows];
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), acc);
        for (int r = 0; r < kRows; ++r) dots[(std::size_t(m) * kOutput + ot * kRows + r) * kGroups + g] = lanes[r];
    }
}

bool qa_reconstruct(const PackedWeights& weights, std::vector<std::int8_t>& wq,
                    std::vector<std::uint16_t>& ws) noexcept {
    if (weights.bytes.size() != kWeightBytes) return false;
    wq.resize(std::size_t(kOutput) * kInput);
    ws.resize(std::size_t(kOutput) * kGroups);
    const auto* packed = weights.bytes.data();
    for (int ot = 0; ot < kOutput / kRows; ++ot) for (int g = 0; g < kGroups; ++g) {
        for (int row = 0; row < kRows; ++row) {
            const int o = ot * kRows + row;
            std::memcpy(&ws[o * kGroups + g], packed + scale_offset(o, g), sizeof(std::uint16_t));
            for (int kp = 0; kp < kK4 / 2; ++kp) {
                const auto* b = packed + code_offset(ot, g, kp) + row * 4;
                const int k = g * 32 + kp * 8;
                for (int j = 0; j < 4; ++j) {
                    wq[o * kInput + k + j] = static_cast<std::int8_t>((b[j] & 15) - 8);
                    wq[o * kInput + k + j + 4] = static_cast<std::int8_t>((b[j] >> 4) - 8);
                }
            }
        }
    }
    return true;
}

bool qa_reconstruct_activations(const PackedActivations& a, std::vector<std::int8_t>& xq,
                                std::vector<std::uint16_t>& xs) noexcept {
    if (a.m != 1 && a.m != 4 && a.m != 8 && a.m != 16) return false;
    if (a.bytes.size() != activation_bytes(a.m)) return false;
    xq.resize(std::size_t(a.m) * kInput);
    xs.resize(std::size_t(a.m) * kGroups);
    const auto* q = reinterpret_cast<const std::uint32_t*>(a.bytes.data());
    const std::size_t qbytes = std::size_t(a.m) * kGroups * kK4 * sizeof(std::uint32_t);
    const std::size_t sbytes = std::size_t(a.m) * kGroups * sizeof(float);
    const std::size_t sumbytes = std::size_t(a.m) * kGroups * sizeof(std::int32_t);
    const auto* scales = reinterpret_cast<const float*>(a.bytes.data() + qbytes);
    const auto* sums = reinterpret_cast<const std::int32_t*>(a.bytes.data() + qbytes + sbytes);
    const auto* half = reinterpret_cast<const std::uint16_t*>(a.bytes.data() + qbytes + sbytes + sumbytes);
    for (int m = 0; m < a.m; ++m) for (int g = 0; g < kGroups; ++g) {
        const std::size_t ix = std::size_t(m) * kGroups + g;
        const std::uint16_t h = half[ix];
        const float decoded = _mm_cvtss_f32(_mm_cvtph_ps(_mm_cvtsi32_si128(h)));
        if (std::memcmp(&scales[ix], &decoded, sizeof(decoded)) != 0) return false;
        xs[ix] = h;
        std::int32_t expected_sum = 0;
        for (int kb = 0; kb < kK4; ++kb) {
            const std::uint32_t v = q[(std::size_t(m) * kGroups + g) * kK4 + kb];
            for (int j = 0; j < 4; ++j) {
                const auto code = static_cast<std::int8_t>(v >> (j * 8));
                xq[std::size_t(m) * kInput + g * 32 + kb * 4 + j] = code;
                expected_sum += code;
            }
        }
        if (sums[ix] != expected_sum) return false;
    }
    return true;
}

} // namespace omega_q4
