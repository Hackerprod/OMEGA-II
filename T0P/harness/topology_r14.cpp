#define NOMINMAX
#include "topology_r14.hpp"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace harness {
namespace {
std::vector<WorkerProcessorR14> workers;
std::vector<std::pair<DWORD, BYTE>> all_cores;
}

bool discover_topology_r14(std::string& why) {
  workers.clear();
  all_cores.clear();
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)) {
    why = "GetLogicalProcessorInformationEx(size) error=" + std::to_string(GetLastError());
    return false;
  }
  std::vector<std::byte> buffer(bytes);
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) {
    why = "GetLogicalProcessorInformationEx(data) error=" + std::to_string(GetLastError());
    return false;
  }
  struct Core { WORD group; KAFFINITY mask; BYTE cls; DWORD ordinal; };
  std::vector<Core> cores;
  DWORD ordinal = 0;
  for (DWORD off = 0; off < bytes;) {
    auto* item = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + off);
    if (item->Size < sizeof(DWORD)*2 || off + item->Size > bytes) {
      why = "GetLogicalProcessorInformationEx returned malformed record at=" + std::to_string(off) +
            " size=" + std::to_string(item->Size) + " bytes=" + std::to_string(bytes);
      return false;
    }
    if (item->Relationship == RelationProcessorCore) {
      const auto& core = item->Processor;
      const std::size_t required = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) +
          offsetof(PROCESSOR_RELATIONSHIP, GroupMask) + std::size_t(core.GroupCount)*sizeof(GROUP_AFFINITY);
      if (!core.GroupCount || item->Size < required) {
        why = "GetLogicalProcessorInformationEx core group mask truncated size=" +
              std::to_string(item->Size) + " required=" + std::to_string(required);
        return false;
      }
      for (WORD g = 0; g < core.GroupCount; ++g)
        cores.push_back({core.GroupMask[g].Group, core.GroupMask[g].Mask, core.EfficiencyClass, ordinal});
      ++ordinal;
    }
    off += item->Size;
  }
  if (cores.empty()) { why = "no processor-core topology records"; return false; }
  const BYTE p_class = std::max_element(cores.begin(), cores.end(),
      [](const Core& a, const Core& b) { return a.cls < b.cls; })->cls;
  std::vector<Core> pcores;
  for (const auto& c : cores) if (c.cls == p_class) pcores.push_back(c);
  std::sort(pcores.begin(), pcores.end(), [](const Core& a, const Core& b) {
    if (a.group != b.group) return a.group < b.group;
    const DWORD abit = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(a.mask)));
    const DWORD bbit = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(b.mask)));
    return abit < bbit;
  });
  if (pcores.size() != 8) { why = "expected 8 highest-efficiency-class P cores, found " + std::to_string(pcores.size()); return false; }
  for (DWORD i = 0; i < pcores.size(); ++i) {
    const auto& c = pcores[i];
    const DWORD bit = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(c.mask)));
    DWORD lp = bit;
    for (WORD g = 0; g < c.group; ++g) lp += GetActiveProcessorCount(g);
    workers.push_back({c.group, KAFFINITY(1) << bit, lp, i, c.cls, c.mask});
  }
  for (const auto& c : cores) {
    const DWORD bit = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(c.mask)));
    all_cores.emplace_back((DWORD(c.group) << 16) | bit, c.cls);
  }
  std::sort(all_cores.begin(), all_cores.end());
  why = "GetLogicalProcessorInformationEx=PASS p_class=" + std::to_string(p_class) + " P_cores=8";
  return true;
}

const WorkerProcessorR14& worker_processor_r14(unsigned worker) {
  if (workers.size() != 8 || worker >= workers.size()) throw std::runtime_error("P-core map is not initialized");
  return workers[worker];
}

bool set_worker_affinity_r14(unsigned worker) {
  const auto& p = worker_processor_r14(worker);
  GROUP_AFFINITY affinity{}; affinity.Group = p.group; affinity.Mask = p.mask;
  return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != FALSE && worker_affinity_matches_r14(worker);
}

bool worker_affinity_matches_r14(unsigned worker) {
  const auto& p = worker_processor_r14(worker);
  GROUP_AFFINITY got{};
  return GetThreadGroupAffinity(GetCurrentThread(), &got) && got.Group == p.group && got.Mask == p.mask && GetCurrentProcessorNumber() == static_cast<WORD>(std::countr_zero(static_cast<unsigned long long>(p.mask)));
}

std::string topology_json_r14() {
  if (workers.size() != 8) throw std::runtime_error("P-core map is not initialized");
  std::ostringstream o;
  o << "{\"source\":\"GetLogicalProcessorInformationEx(RelationProcessorCore)\",\"p_core_efficiency_class\":" << unsigned(workers[0].efficiency_class) << ",\"workers\":[";
  for (unsigned i = 0; i < workers.size(); ++i) {
    if (i) o << ',';
    const auto& p = workers[i];
    o << "{\"worker\":" << i << ",\"p_core\":" << p.p_core << ",\"group\":" << p.group
      << ",\"lp\":" << p.lp << ",\"mask\":" << p.mask << ",\"siblings_mask\":" << p.siblings
      << ",\"efficiency_class\":" << unsigned(p.efficiency_class) << '}';
  }
  o << "],\"all_core_records\":[";
  for (size_t i = 0; i < all_cores.size(); ++i) {
    if (i) o << ',';
    o << "{\"group_lp_key\":" << all_cores[i].first << ",\"efficiency_class\":" << unsigned(all_cores[i].second) << '}';
  }
  o << "]}";
  return o.str();
}
}
