#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace omega_evict {
constexpr std::size_t kLineBytes = 64;
enum class RegionKind { WeightCodes, WeightScales, X, Y, PreparedActivations, CorrectiveSums, Scratch, Output };
struct Region { const void* ptr; std::size_t bytes; RegionKind kind; const char* name; };
enum class FlushKind { CLFLUSH, CLFLUSHOPT };
struct Choice { FlushKind kind; const char* name; };
using LineSink = void(*)(std::uintptr_t, void*);

// Selection is made once by CPUID.07H:EBX[23] and then frozen for this process.
Choice selected_flush() noexcept;
// Governing exported cache-eviction primitive. Integrators link this API and evict.cpp.
void evict_range(const void* ptr, std::size_t bytes);
// Comparative diagnostic only; never changes the process-wide CPUID choice.
void evict_range_diagnostic(const void* ptr, std::size_t bytes, FlushKind kind);
void evict_range_record(const void* ptr, std::size_t bytes, LineSink sink, void* context);
// Eight-worker typed Q4 batching; delegates every contiguous span to the same range implementation.
void evict_weights_regions(const std::vector<Region>& regions, unsigned workers = 8);
void evict_weights_regions_diagnostic(const std::vector<Region>& regions, unsigned workers, FlushKind kind);
void evict_weights_regions_record(const std::vector<Region>& regions, unsigned workers,
                                  LineSink sink, void* context);
const char* kind_name(RegionKind) noexcept;
}
