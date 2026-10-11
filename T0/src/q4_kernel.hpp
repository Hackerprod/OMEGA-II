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

// Output memory is allocated before timing. The measured kernel only writes this contractual tensor.
struct TimedOutput {
    std::vector<float> values;
};

#ifdef T0_Q4_QA
extern volatile std::uint64_t g_qa_batch_call_count;
struct Evaluation {
    std::vector<std::int32_t> group_dot;
    std::vector<float> output;
};
#endif

bool avx2_available();
bool avxvnni256_available();
bool f16c_available();
WeightMatrix generate_weights(std::size_t O, std::size_t I, std::uint64_t seed, bool extremes=false);
Activations generate_activations(std::size_t M, std::size_t I, std::uint64_t seed, bool extremes=false);
void validate_inputs(const WeightMatrix& weights, const Activations& activations);
void prepare_timed_output(TimedOutput& output, std::size_t M, std::size_t O);
using KernelEntry = void(*)(const WeightMatrix&, const Activations&, float*);
KernelEntry select_prevalidated_kernel(Route route, std::size_t M);
using KernelBatchEntry = void(*)(const WeightMatrix&, const Activations&, float*, int calls);
KernelBatchEntry select_prevalidated_batch_kernel(Route route, std::size_t M);
std::size_t kernel_m_tile(std::size_t M);
// Convenience entrypoint: one route dispatch only, no validation and no allocation/resize.
void execute_into_prechecked(const WeightMatrix& weights, const Activations& activations, Route route, TimedOutput& output);
#ifdef T0_Q4_QA
Evaluation execute(const WeightMatrix& weights, const Activations& activations, Route route);
void execute_into(const WeightMatrix& weights, const Activations& activations, Route route, Evaluation& output);
#endif
const char* route_name(Route route);
}
