#define NOMINMAX
#include "harness_r16.hpp"
#include "topology_r16.hpp"
#include "common.hpp"
#include "evict.hpp"
#include <windows.h>
#include <process.h>
#include <psapi.h>
#include <bcrypt.h>
#include <intrin.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
int run_integrated_provider_qa_r16();
std::string hex_sha256(const std::string& text) {
  BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_bytes = 0, read = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("SHA256_PROVIDER_FAILED");
  if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &read, 0) < 0) {
    BCryptCloseAlgorithmProvider(alg, 0); throw std::runtime_error("SHA256_OBJECT_LENGTH_FAILED");
  }
  std::vector<UCHAR> object(object_bytes), digest(32);
  NTSTATUS status = BCryptCreateHash(alg, &hash, object.data(), object_bytes, nullptr, 0, 0);
  if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), ULONG(text.size()), 0);
  if (status >= 0) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
  if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
  if (status < 0) throw std::runtime_error("SHA256_DIGEST_FAILED");
  std::ostringstream out; out << std::hex << std::setfill('0');
  for (auto value : digest) out << std::setw(2) << unsigned(value);
  return out.str();
}

std::string sha256_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot_open_evidence_for_hash:" + path);
  std::string bytes((std::istreambuf_iterator<char>(input)), {});
  return hex_sha256(bytes);
}

void write_json_new(const std::string& path, const nlohmann::json& value) {
  if (std::filesystem::exists(path)) throw std::runtime_error("phase_evidence_already_exists:" + path);
  std::ofstream out(path, std::ios::binary | std::ios::out);
  if (!out) throw std::runtime_error("phase_evidence_open_failed:" + path);
  out << value.dump() << "\n"; out.flush();
  if (!out) throw std::runtime_error("phase_evidence_write_failed:" + path);
}

std::vector<std::string> read_schedule_ids(const std::string& path, bool include_calibration) {
  std::ifstream in(path); if (!in) throw std::runtime_error("schedule_open_failed:" + path);
  std::string line; std::vector<std::string> ids;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("sequence\t", 0) == 0) continue;
    std::vector<std::string> fields; std::stringstream row(line); std::string field;
    while (std::getline(row, field, '\t')) fields.push_back(field);
    if (fields.size() != 6) throw std::runtime_error("schedule_row_field_count_invalid");
    if (include_calibration || fields[2] != "calibration") ids.push_back(fields[1]);
  }
  return ids;
}

std::vector<std::string> read_event_ids(const std::string& path) {
  std::ifstream in(path); if (!in) return {};
  std::vector<std::string> ids; std::string line;
  while (std::getline(in, line)) if (!line.empty()) {
    try { ids.push_back(nlohmann::json::parse(line).at("event_id").get<std::string>()); }
    catch (...) { throw std::runtime_error("residency_event_jsonl_invalid"); }
  }
  return ids;
}

std::string topology_sha256(std::vector<DWORD>& lps) {
  std::string why;
  if (!harness::discover_topology_r16(why)) throw std::runtime_error("topology_discovery_failed:" + why);
  const auto topology = harness::topology_json_r16();
  lps.clear();
  for (unsigned i = 0; i < 8; ++i) lps.push_back(harness::worker_processor_r16(i).lp);
  return hex_sha256(topology);
}

bool large_page_probe(std::string& why) {
  const SIZE_T minimum = GetLargePageMinimum();
  if (!minimum) { why = "GetLargePageMinimum=0:" + std::to_string(GetLastError()); return false; }
  void* memory = VirtualAlloc(nullptr, minimum, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
  if (!memory) { why = "VirtualAlloc(MEM_LARGE_PAGES)=" + std::to_string(GetLastError()); return false; }
  static_cast<volatile std::uint8_t*>(memory)[0] = 0x5a;
  PSAPI_WORKING_SET_EX_INFORMATION page{}; page.VirtualAddress = memory;
  const bool ok = QueryWorkingSetEx(GetCurrentProcess(), &page, sizeof(page)) &&
      page.VirtualAttributes.Valid && page.VirtualAttributes.LargePage;
  VirtualFree(memory, 0, MEM_RELEASE);
  if (!ok) why = "QueryWorkingSetEx_large_page_probe_failed";
  return ok;
}

int acquire_phase_r16(const std::string& phase, const std::map<std::string, std::string>& args) {
  auto get = [&](const std::string& key) -> std::string {
    auto it = args.find(key); if (it == args.end()) throw std::runtime_error("missing_phase_argument:" + key); return it->second;
  };
  const std::string plan_path = get("--plan"), output_path = get("--output");
  const std::string session_id = get("--session-id");
  const std::string schedule_path = get("--schedule"), disassembly_path = get("--disassembly");
  std::ifstream plan_input(plan_path, std::ios::binary);
  if (!plan_input) throw std::runtime_error("official_plan_missing:" + plan_path);
  const auto plan = nlohmann::json::parse(plan_input);
  const std::string plan_sha = plan.at("sha256").get<std::string>();
  std::vector<DWORD> lps;
  const std::string topology_sha = topology_sha256(lps);
  LARGE_INTEGER qpf{}; if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) throw std::runtime_error("QPF_UNAVAILABLE");
  const auto start_qpc = [] { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart; }();
  std::string previous_sha;
  const std::map<std::string, std::string> predecessor = {
    {"qa_instruments_expulsion", "preflight"}, {"qualification", "qa_instruments_expulsion"},
    {"residency", "qualification"}, {"final_controls", "residency"},
    {"derivation_verdict", "final_controls"}};
  if (phase != "preflight") {
    const auto prev_path = get("--previous-evidence"); previous_sha = get("--previous-evidence-sha256");
    if (sha256_file(prev_path) != previous_sha) throw std::runtime_error("previous_evidence_hash_mismatch");
    std::ifstream prev_in(prev_path, std::ios::binary); const auto prev = nlohmann::json::parse(prev_in);
    if (prev.at("phase").get<std::string>() != predecessor.at(phase) ||
        prev.at("plan_sha256").get<std::string>() != plan_sha ||
        prev.at("topology_map_sha256").get<std::string>() != topology_sha)
      throw std::runtime_error("immediate_phase_input_identity_or_order_invalid:" + phase);
  }
  std::string coordinator_affinity;
  const bool coordinator_ok = phase == "preflight" || harness::set_coordinator_affinity_r16(coordinator_affinity);
  nlohmann::json evidence = {{"phase", phase}, {"session_id", session_id}, {"plan_sha256", plan_sha},
    {"topology_map_sha256", topology_sha}, {"worker_lps", lps}, {"qpc_hz", qpf.QuadPart},
    {"qpc_source", "QueryPerformanceCounter"}, {"previous_evidence_sha256", previous_sha},
    {"numeric_metrics_used", false}, {"coordinator_affinity_ok", coordinator_ok},
    {"coordinator_affinity", coordinator_affinity}};

  if (phase == "preflight") {
    std::string privilege;
    const bool privilege_ok = t0common::enable_large_page_privilege(&privilege);
    std::string coordinator;
    const bool preflight_coordinator_ok = harness::set_coordinator_affinity_r16(coordinator);
    std::string page_reason;
    const bool pages = privilege_ok && large_page_probe(page_reason);
    const bool workers_ok = lps == std::vector<DWORD>{0, 2, 4, 6, 8, 10, 12, 14};
    evidence["large_pages"] = pages; evidence["large_page_privilege"] = privilege;
    evidence["state"] = pages && workers_ok && preflight_coordinator_ok ? "PASS" : "HOLD_RESOURCE";
    evidence["reason"] = page_reason; evidence["coordinator_affinity"] = coordinator;
    evidence["topology_map"] = nlohmann::json::parse(harness::topology_json_r16());
    evidence["preflight_qpc_start"] = start_qpc;
  } else if (phase == "qa_instruments_expulsion") {
    const int kernel = harness::run_kernel_qa_r16();
    const int providers = kernel == 0 ? run_integrated_provider_qa_r16() : 3;
    std::string why; const SIZE_T bytes = GetLargePageMinimum();
    void* buffer = bytes ? VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE) : nullptr;
    bool expulsion = buffer != nullptr;
    if (expulsion) { std::memset(buffer, 0xA5, bytes); omega_evict::evict_range(buffer, bytes); VirtualFree(buffer, 0, MEM_RELEASE); }
    std::cout << "EXPULSION_QA_R16 " << (expulsion ? "PASS" : "HOLD_REAL")
              << " primitive=omega_evict::evict_range weights_only_probe=true callback=none\n";
    evidence["state"] = coordinator_ok && kernel == 0 && providers == 0 && expulsion ? "PASS" : "HOLD_INSTRUMENTATION";
    evidence["kernel_qa"] = kernel == 0 ? "PASS" : "HOLD";
    evidence["instrumentation"] = providers == 0 ? "PASS" : "HOLD";
    evidence["expulsion_qa"] = expulsion ? "PASS" : "HOLD";
    evidence["reason"] = expulsion ? "none" : "large_page_eviction_probe_failed";
  } else if (phase == "qualification") {
    std::string privilege_status;
    if (!t0common::enable_large_page_privilege(&privilege_status))
      throw std::runtime_error("qualification_SeLockMemoryPrivilege_HOLD:" + privilege_status);
    std::ifstream disassembly(disassembly_path, std::ios::binary);
    const auto disasm = nlohmann::json::parse(disassembly);
    const auto obj = std::filesystem::path(output_path).parent_path();
    std::ofstream raw(obj / "aux_qualification_raw_r16.jsonl", std::ios::binary | std::ios::out);
    std::ofstream summary(obj / "aux_qualification_summary_r16.jsonl", std::ios::binary | std::ios::out);
    if (!raw || !summary) throw std::runtime_error("qualification_output_open_failed");
    const int rc = coordinator_ok && disasm.at("state") == "PASS" ? harness::run_aux_smoke(raw, summary, 3, disassembly_path) : 3;
    raw.close(); summary.close();
    evidence["state"] = rc == 0 ? "SMOKE_ONLY" : "HOLD_INSTRUMENTATION";
    evidence["numeric_metrics_used"] = false; evidence["bw_dram"] = rc == 0 ? "PASS" : "HOLD";
    evidence["p_instr"] = rc == 0 ? "PASS" : "HOLD"; evidence["p_instr_variants"] = 9;
    evidence["N"] = 3; evidence["all_workers_calibrated"] = rc == 0;
    evidence["disassembly_sha256"] = disasm.at("sha256");
    evidence["qualification_label"] = "SMOKE_MINIMUM_NO_SCIENTIFIC_METRICS";
  } else if (phase == "residency") {
    const std::string outdir = std::filesystem::path(output_path).parent_path().string();
    const auto selected = read_schedule_ids(schedule_path, true);
    const auto sample_ids = read_schedule_ids(schedule_path, false);
    const bool reader_qualified = get("--reader-state") == "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION";
    const int rc = coordinator_ok ? harness::run_residency_smoke_r16(outdir, plan_path, schedule_path, reader_qualified) : 3;
    const auto actual = read_event_ids(outdir + "\\residency_events_r16.jsonl");
    const bool order_ok = actual == selected;
    const std::string joined = [&] { std::string s; for (const auto& id : actual) { s += id; s.push_back('\n'); } return s; }();
    evidence["state"] = rc == 0 && order_ok ? "SMOKE_ONLY" : "HOLD_INSTRUMENTATION";
    evidence["N"] = 3; evidence["numeric_metrics_used"] = false;
    evidence["event_count"] = actual.size(); evidence["selected_schedule_rows"] = selected.size();
    evidence["calibration_events"] = selected.size() - sample_ids.size();
    evidence["event_order_matches_schedule"] = order_ok;
    evidence["event_order_sha256"] = hex_sha256(joined);
    evidence["block_id"] = get("--residency-block");
    evidence["block_complete"] = rc == 0 && order_ok && selected.size() == 264 && sample_ids.size() == 204;
    evidence["reason"] = order_ok ? "none" : "residency_event_order_or_multiplicity_invalid";
    std::cout << "ACQUIRE_RESIDENCY_R16 " << (evidence["block_complete"].get<bool>() ? "PASS" : "HOLD")
              << " schedule_rows=" << selected.size() << " emitted_events=" << actual.size()
              << " event_order_and_multiplicity=" << (order_ok ? "PASS" : "FAIL") << "\n";
  } else {
    evidence["state"] = "HOLD_INSTRUMENTATION";
    evidence["reason"] = "official_final_controls_not_acquirable_in_SMOKE_mode";
  }
  const auto end_qpc = [] { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart; }();
  evidence["phase_qpc_start"] = start_qpc; evidence["phase_qpc_end"] = end_qpc;
  write_json_new(output_path, evidence);
  const auto state = evidence.value("state", std::string("UNKNOWN"));
  std::cout << "ACQUIRE_R16 phase=" << phase << " state=" << state
            << " plan_sha256=" << plan_sha << " topology_map_sha256=" << topology_sha
            << " previous_evidence=" << (previous_sha.empty() ? "NONE" : "VERIFIED") << "\n";
  return 0;
}

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
  if (std::wstring(mode) == L"--self-test") {
    args.push_back(L"--output");
    args.push_back((root / L"T0P" / L"collector" / L"logs_r16" / L"obj_r16" /
                    (L"official_run_r16_qa_" + std::to_wstring(GetTickCount64()) + L".txt")).wstring());
  }
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
    if (argc >= 3 && std::string(argv[1]) == "--acquire-r16") {
      std::map<std::string, std::string> options;
      for (int i = 3; i + 1 < argc; i += 2) {
        if (argv[i][0] != '-' || !options.emplace(argv[i], argv[i + 1]).second)
          throw std::runtime_error("acquisition_cli_duplicate_or_invalid_option");
      }
      if (((argc - 3) % 2) != 0) throw std::runtime_error("acquisition_cli_option_value_missing");
      return acquire_phase_r16(argv[2], options);
    }
    if (argc >= 2 && std::string(argv[1]) == "--residency-smoke-r16") {
      std::cerr << "ACQUISITION_GATE_REQUIRED measurements_started=false\n";
      return 2;
    }
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
