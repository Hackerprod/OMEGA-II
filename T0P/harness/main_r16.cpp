#define NOMINMAX
#include "harness_r16.hpp"
#include "topology_r16.hpp"
#include "common.hpp"
#include <windows.h>
#include <process.h>
#include <psapi.h>
#include <filesystem>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
int run_collector_entry(const wchar_t* mode, const wchar_t* session_id = nullptr) {
  std::string privilege_status;
  if (!t0common::enable_large_page_privilege(&privilege_status)) {
    std::cerr << "HARNESS_R16_ENTRY HOLD_REAL large_page_privilege=" << privilege_status
              << " measurements_started=false\n";
    return 20;
  }
  std::cout << "LARGE_PAGE_PRIVILEGE=" << privilege_status
            << " enable_large_page_privilege_called=true\n";
  std::wstring module(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (length == 0 || length >= module.size()) {
    std::cerr << "HARNESS_R16_ENTRY REJECT executable_path_unavailable measurements_started=false\n";
    return 20;
  }
  module.resize(length);
  const std::filesystem::path executable(module);
  const auto root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
  const auto script = root / L"T0P" / L"collector" / L"official_run_r16.py";
  if (!std::filesystem::is_regular_file(script)) {
    std::wcerr << L"HARNESS_R16_ENTRY REJECT orchestrator_missing=" << script.c_str()
               << L" measurements_started=false\n";
    return 20;
  }
  const std::filesystem::path python = L"C:\\Program Files\\Python313\\python.exe";
  if (!std::filesystem::is_regular_file(python)) {
    std::cerr << "HARNESS_R16_ENTRY REJECT python_runtime_missing measurements_started=false\n";
    return 20;
  }

  std::vector<std::wstring> args{
      L"-B", script.wstring(), mode,
      L"--integrated-executable", executable.wstring()};
  if (session_id) {
    args.push_back(L"--session-id");
    args.emplace_back(session_id);
  }
  const auto quote_arg = [](const std::wstring& value) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
      if (ch == L'\\') { ++slashes; continue; }
      if (ch == L'\"') {
        quoted.append(slashes * 2 + 1, L'\\');
        quoted.push_back(ch);
      } else {
        quoted.append(slashes, L'\\');
        quoted.push_back(ch);
      }
      slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
  };
  std::wstring command_line = quote_arg(python.wstring());
  for (const auto& arg : args) {
    command_line.push_back(L' ');
    command_line += quote_arg(arg);
  }
  if (_wputenv_s(L"OMEGA_R16_INTEGRATED_LAUNCH", L"1") != 0) {
    std::cerr << "HARNESS_R16_ENTRY REJECT integrated_launch_token_failed measurements_started=false\n";
    return 20;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(python.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                      0, nullptr, root.c_str(), &startup, &process)) {
    std::cerr << "HARNESS_R16_ENTRY REJECT python_launch_failed measurements_started=false error="
              << GetLastError() << "\n";
    return 20;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 20;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(exit_code);
}

bool large_page_range(const void* ptr, std::size_t bytes) {
  if (!ptr || !bytes) return false;
  SYSTEM_INFO info{}; GetSystemInfo(&info);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t end = begin + bytes - 1;
  const uintptr_t first = begin & ~(uintptr_t(info.dwPageSize) - 1);
  const uintptr_t last = end & ~(uintptr_t(info.dwPageSize) - 1);
  std::vector<PSAPI_WORKING_SET_EX_INFORMATION> pages;
  for (uintptr_t page = first;; page += info.dwPageSize) {
    PSAPI_WORKING_SET_EX_INFORMATION row{}; row.VirtualAddress = reinterpret_cast<void*>(page); pages.push_back(row);
    if (page == last) break;
  }
  if (!QueryWorkingSetEx(GetCurrentProcess(), pages.data(), DWORD(pages.size() * sizeof(pages[0])))) return false;
  for (const auto& page : pages) if (!page.VirtualAttributes.Valid || !page.VirtualAttributes.LargePage) return false;
  return true;
}

int run_integrated_provider_qa_r16() {
  std::string lp; if (!t0common::enable_large_page_privilege(&lp)) {
    std::cerr << "PROVIDER_QA_R16 BLOCKED_REAL privilege=" << lp << " measurements_started=false\n"; return 3;
  }
  std::string topology;
  if (!harness::discover_topology_r16(topology)) {
    std::cerr << "PROVIDER_QA_R16 BLOCKED_REAL topology=" << topology << " measurements_started=false\n"; return 3;
  }
  struct ProviderCase { std::string name, variant; std::function<std::unique_ptr<harness::Provider>()> make; };
  const std::vector<ProviderCase> cases = {
    {"OMEGA", "mm", harness::make_omega},
    {"ggml_flat", "mm", harness::make_ggml_flat},
    {"ggml_repack", "mm", harness::make_ggml_repack},
    {"Torch_mm", "mm", harness::make_torch_out},
    {"Torch_matmul", "matmul", harness::make_torch_out},
    {"Torch_linear", "linear", harness::make_torch_out},
    {"Torch_mv", "mv", harness::make_torch_out}};
  std::atomic<bool> pass{true};
  std::mutex error_mutex; std::string error;
  std::uint64_t configurations = 0, worker_dispatches = 0;
  std::atomic<std::uint64_t> large_page_regions{0};
  for (const auto& item : cases) for (int m : {1, 4, 8, 16}) {
    if (item.variant == "mv" && m != 1) continue;
    for (const std::string frontier : {"prepared", "dynamic"}) {
      std::array<std::string, 8> worker_messages{}; std::vector<std::thread> workers; workers.reserve(8);
      for (unsigned worker = 0; worker < 8; ++worker) workers.emplace_back([&, worker] {
        try {
          if (!harness::set_worker_affinity_r16(worker) || !harness::worker_affinity_matches_r16(worker))
            throw std::runtime_error("worker_to_LP_affinity_failed");
          auto provider = item.make();
          harness::CellSpec spec; spec.m = m; spec.output = 1024; spec.variant = item.variant; spec.frontier = frontier;
          provider->prepare(spec, worker);
          std::string why; if (!provider->validate(why)) throw std::runtime_error("validate=" + why);
          provider->run_once_frontier(worker, m, frontier);
          const auto regions = provider->bytes_regions();
          if (regions.empty()) throw std::runtime_error("no_effective_provider_buffers");
          for (const auto& region : regions) {
            if (!region.data || !region.bytes || !region.large || !large_page_range(region.data, region.bytes))
              throw std::runtime_error(std::string("buffer_not_large_pages:") + region.name);
            ++large_page_regions;
          }
          worker_messages[worker] = std::string(provider->name()) + ":" + why + ":buffers=" + std::to_string(regions.size());
        } catch (const std::exception& e) {
          pass = false; std::lock_guard<std::mutex> lock(error_mutex);
          if (error.empty()) error = item.name + " m=" + std::to_string(m) + " frontier=" + frontier +
                                     " worker=" + std::to_string(worker) + " " + e.what();
        }
      });
      for (auto& worker : workers) worker.join();
      if (!pass) { std::cerr << "PROVIDER_QA_R16 HOLD_REAL " << error << "\n"; return 3; }
      ++configurations; worker_dispatches += 8;
      std::cout << "PROVIDER_DISPATCH_R16 PASS family=" << item.name << " m=" << m << " frontier=" << frontier
                << " workers=8 LP_map=topology_r16 buffers=large_pages effective_dispatch=8/8\n";
    }
  }
  std::cout << "PROVIDER_QA_R16 PASS configurations=" << configurations << " worker_dispatches=" << worker_dispatches
            << " large_page_regions=" << large_page_regions.load() << " privilege=" << lp << " providers=OMEGA,ggml_flat,ggml_repack,Torch_mm,matmul,linear,mv frontiers=prepared,dynamic\n";
  return 0;
}

int run_instrument_qa_r16(const std::string& outdir, const std::string& disassembly) {
  if (harness::run_kernel_qa_r16() != 0) return 3;
  if (run_integrated_provider_qa_r16() != 0) return 3;
  std::ofstream raw(outdir + "\\aux_qualification_raw_r16.jsonl", std::ios::binary | std::ios::trunc);
  std::ofstream summary(outdir + "\\aux_qualification_summary_r16.jsonl", std::ios::binary | std::ios::trunc);
  if (!raw || !summary) throw std::runtime_error("instrument QA output files could not be opened");
  const int rc = harness::run_aux_smoke(raw, summary, 3, disassembly);
  raw.close(); summary.close();
  std::cout << "INSTRUMENT_QA_R16 " << (rc == 0 ? "PASS" : "HOLD_REAL") << " P_instr=9_variants_N3 BW_DRAM=256MiB_8_workers label=SMOKE metrics_used=false\n";
  return rc;
}
}

int main(int argc, char** argv) {
  try {
    if (argc == 5 && std::string(argv[1]) == "--residency-smoke-r16")
      return harness::run_residency_smoke_r16(argv[2], argv[3], argv[4]);
    if (argc == 4 && std::string(argv[1]) == "--plan-qa-r16")
      return harness::verify_full_plan_smoke_slice_r16(argv[2], argv[3]);
    if (argc == 4 && std::string(argv[1]) == "--instrument-qa-r16")
      return run_instrument_qa_r16(argv[2], argv[3]);
    if (argc == 2 && std::string(argv[1]) == "--provider-qa-r16")
      return run_integrated_provider_qa_r16();
    if (argc == 2 && std::string(argv[1]) == "--qa-r16")
      return harness::run_kernel_qa_r16();
    if (argc == 4 && std::string(argv[1]) == "--official" &&
        std::string(argv[2]) == "--session-id")
      return run_collector_entry(L"--official", std::wstring(argv[3], argv[3] + std::strlen(argv[3])).c_str());
    if (argc == 2 && std::string(argv[1]) == "--synthetic-self-test")
      return run_collector_entry(L"--self-test");
    std::cerr << "HARNESS_R16 usage=--official --session-id <id> | --synthetic-self-test | --qa-r16 | --plan-qa-r16 <plan.json> <schedule.tsv> | --provider-qa-r16 | --instrument-qa-r16 <outdir> <disassembly.json> | "
                 "--residency-smoke-r16 <new-attempt-dir> <official-plan-r16.json> <schedule.tsv>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "HARNESS_R16_ERROR " << e.what()
              << " measurements_label=SMOKE numeric_metrics_used=false\n";
    return 1;
  }
}
