#pragma once

#include "common.hpp"
#include <cstddef>
#include <cstdint>

namespace omega_q4 {

constexpr int kOutput = 1024;
constexpr int kInput = 512;
constexpr int kGroups = 16;
constexpr int kRows = 8;
constexpr int kK4 = 8;
constexpr std::size_t kCodeBytes = std::size_t(kOutput) * kGroups * kK4 * 2;
constexpr std::size_t kScaleBytes = std::size_t(kOutput) * kGroups * sizeof(std::uint16_t);
constexpr std::size_t kWeightBytes = kCodeBytes + kScaleBytes;

struct PackedWeights {
    t0common::Buffer<std::uint8_t> bytes;
};

struct PackedActivations {
    // qwords[M,G,8] followed by FP32 scales[M,G], then int32 sums[M,G].
    t0common::Buffer<std::uint8_t> bytes;
    int m = 0;
};

std::size_t activation_bytes(int m);
void prepare_weights(const t0common::Cell& cell, PackedWeights& dst);
void prepare_activations(const t0common::Cell& cell, PackedActivations& dst);

// Precondition: buffers are prepared, sizes and M are valid, output is 32-byte aligned,
// and AVX-VNNI is available.
// This function performs no allocation, validation, dispatch, or instrumentation.
void run(const PackedWeights& weights, const PackedActivations& activations, float* output) noexcept;

// QA-only observation using the same VPDPBUSD primitive and packed storage as run().
// Writes exact signed INT32 dots [M,O,G]; no scaling or floating-point reduction.
void qa_group_dots(const PackedWeights& weights, const PackedActivations& activations,
                   std::int32_t* dots) noexcept;

// Reconstruct the logical Q4 codes and FP16 scale bits from effective packed storage.
bool qa_reconstruct(const PackedWeights& weights, std::vector<std::int8_t>& wq,
                    std::vector<std::uint16_t>& ws) noexcept;
bool qa_reconstruct_activations(const PackedActivations& activations, std::vector<std::int8_t>& xq,
                                std::vector<std::uint16_t>& xs) noexcept;

} // namespace omega_q4
