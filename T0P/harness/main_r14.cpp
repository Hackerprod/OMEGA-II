#define NOMINMAX
#include "harness_r14.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--residency-smoke-r14")
      return harness::run_residency_smoke_r14(argv[2], argv[3]);
    if (argc == 2 && std::string(argv[1]) == "--official") {
      std::cerr << "HARNESS_R14_OFFICIAL REJECT AUTHORIZED.json_real_forbidden measurements_started=false\n";
      return 20;
    }
    std::cerr << "HARNESS_R14 usage=--residency-smoke-r14 <new-attempt-dir> <r14-smoke-schedule>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "HARNESS_R14_ERROR " << e.what() << " measurements_label=SMOKE numeric_metrics_used=false\n";
    return 1;
  }
}
