#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>

namespace harness {
struct WorkerProcessorR12 {
  WORD group = 0;
  KAFFINITY mask = 0;
  DWORD lp = 0;
  DWORD p_core = 0;
  BYTE efficiency_class = 0;
  KAFFINITY siblings = 0;
};

bool discover_topology_r12(std::string& why);
const WorkerProcessorR12& worker_processor_r12(unsigned worker);
bool set_worker_affinity_r12(unsigned worker);
bool worker_affinity_matches_r12(unsigned worker);
std::string topology_json_r12();
}
