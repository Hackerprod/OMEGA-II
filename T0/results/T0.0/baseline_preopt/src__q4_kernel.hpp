#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace t0q4 {
enum class Route { Scalar, AVX2, AVXVNNI256 };

struct WeightMatrix {
    std::size_t O=0, I=0, groups_per_row=0;
    std::vector<std::uint8_t> storage;
    std::uint64_t payload_bytes() const { return static_cast<std::uint64_t>(storage.size()); }
    std::uint64_t physical_allocated_bytes() const;
    std::uint64_t metadata_bytes() const;
};

struct Activations {
    std::size_t M=0, I=0, groups_per_row=0;
    std::vector<std::int8_t> q;
    std::vector<float> scale;
};

struct Evaluation {
    std::vector<std::int32_t> group_dot;
    std::vector<double> accumulator;
    std::vector<float> output;
};

bool avx2_available();
bool avxvnni256_available();
WeightMatrix generate_weights(std::size_t O, std::size_t I, std::uint64_t seed, bool extremes=false);
Activations generate_activations(std::size_t M, std::size_t I, std::uint64_t seed, bool extremes=false);
Evaluation execute(const WeightMatrix& weights, const Activations& activations, Route route);
void execute_into(const WeightMatrix& weights, const Activations& activations, Route route, Evaluation& output);
// Only for a caller that has already checked route support after fixing affinity.
void execute_into_prechecked(const WeightMatrix& weights, const Activations& activations, Route route, Evaluation& output);
const char* route_name(Route route);
}
