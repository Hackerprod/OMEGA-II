#pragma once

#include "common.hpp"
#include <cstddef>
#include <cstdint>

namespace omega_q4 {

constexpr int kOutput = 1024;
constexpr int kOutputMax = 58256;
constexpr int kInput = 512;
constexpr int kGroups = 16;
constexpr int kRows = 8;
constexpr int kK4 = 8;
constexpr std::size_t code_bytes(int output) noexcept { return std::size_t(output) * kGroups * kK4 * 2; }
constexpr std::size_t scale_bytes(int output) noexcept { return std::size_t(output) * kGroups * sizeof(std::uint16_t); }
constexpr std::size_t weight_bytes(int output) noexcept { return code_bytes(output) + scale_bytes(output); }

struct PackedWeights {
    t0common::Buffer<std::uint8_t> bytes;
    int output = 0;
};

struct PackedActivations {
    // qwords[M,G,8] followed by FP32 scales[M,G], then int32 sums[M,G].
    t0common::Buffer<std::uint8_t> bytes;
    int m = 0;
    int output = 0;
};

std::size_t activation_bytes(int m);
std::size_t output_bytes(int m, int output) noexcept;
void prepare_weights(const t0common::Cell& cell, int output, PackedWeights& dst);
void prepare_activations(const t0common::Cell& cell, int output, PackedActivations& dst);

// Precondition: buffers are prepared, sizes and M are valid, output is 32-byte aligned,
// and AVX-VNNI is available.
// This function performs no allocation, validation, dispatch, or instrumentation.
void run(const PackedWeights& weights, const PackedActivations& activations, int output_rows, float* output) noexcept;

// QA-only observation using the same VPDPBUSD primitive and packed storage as run().
// Writes exact signed INT32 dots [M,O,G]; no scaling or floating-point reduction.
void qa_group_dots(const PackedWeights& weights, const PackedActivations& activations,
                   std::int32_t* dots) noexcept;
void qa_run_selected_tiles(const PackedWeights& weights, const PackedActivations& activations,
                           int first_tile, int last_tile, float* output) noexcept;

// Reconstruct the logical Q4 codes and FP16 scale bits from effective packed storage.
bool qa_reconstruct(const PackedWeights& weights, std::vector<std::int8_t>& wq,
                    std::vector<std::uint16_t>& ws) noexcept;
bool qa_reconstruct_activations(const PackedActivations& activations, std::vector<std::int8_t>& xq,
                                std::vector<std::uint16_t>& xs) noexcept;

} // namespace omega_q4
