#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>

namespace harness {
struct WorkerProcessorR16 {
  WORD group = 0;
  KAFFINITY mask = 0;
  DWORD lp = 0;
  DWORD p_core = 0;
  BYTE efficiency_class = 0;
  KAFFINITY siblings = 0;
};

bool discover_topology_r16(std::string& why);
const WorkerProcessorR16& worker_processor_r16(unsigned worker);
bool set_worker_affinity_r16(unsigned worker);
bool worker_affinity_matches_r16(unsigned worker);
bool set_coordinator_affinity_r16(std::string& why);
std::string topology_json_r16();
}
