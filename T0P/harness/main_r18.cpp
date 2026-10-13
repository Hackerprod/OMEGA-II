#define NOMINMAX
#include "harness_r18.hpp"
#include "topology_r18.hpp"
#include "common.hpp"
#include "evict.hpp"
#include <windows.h>
#include <process.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <intrin.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <array>
#include <atomic>
#include <cmath>
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
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {
int run_integrated_provider_qa_r18();
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
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
      const auto tab = line.find('\t', start);
      if (tab == std::string::npos) { fields.push_back(line.substr(start)); break; }
      fields.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
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

bool emon_reader_process_running_r18() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"emon.exe") == 0) { found = true; break; }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

std::string topology_sha256(std::vector<DWORD>& lps) {
  std::string why;
  if (!harness::discover_topology_r18(why)) throw std::runtime_error("topology_discovery_failed:" + why);
  const auto topology = harness::topology_json_r18();
  lps.clear();
  for (unsigned i = 0; i < 8; ++i) lps.push_back(harness::worker_processor_r18(i).lp);
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

nlohmann::json validate_aux_acquisition_r18(const std::string& raw_path,
                                             const std::string& expected_disassembly_sha) {
  const std::set<std::string> expected_cells = {
    "P_instr_c1_U8", "P_instr_c2_U8", "P_instr_c4_U8", "P_instr_c8_U8",
    "P_instr_c10_U8", "P_instr_c12_U8", "P_instr_c14_U8",
    "P_instr_c12_U16", "P_instr_c14_U16", "BW_DRAM_256MiB_8P"};
  std::ifstream input(raw_path, std::ios::binary);
  if (!input) return {{"complete", false}, {"reason", "acquired_aux_raw_missing"},
                      {"rows", 0}, {"cells", 0}};
  std::set<std::tuple<std::string, int, int>> observed;
  std::map<std::string, std::uint64_t> common_r;
  std::map<std::string, unsigned> cell_rows;
  std::set<std::string> cells;
  std::string line, reason;
  std::size_t rows = 0;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    ++rows;
    try {
      const auto row = nlohmann::json::parse(line);
      const std::string provider = row.at("provider").get<std::string>();
      const std::string cell = row.at("cell").get<std::string>();
      const int replica = row.at("replica").get<int>();
      const int worker = row.at("worker").get<int>();
      const auto r = row.at("R").get<std::uint64_t>();
      if (!expected_cells.count(cell) || row.at("label") != "SMOKE" ||
          row.at("status") != "SMOKE_TIMED" || row.at("N") != 3 ||
          row.at("R_common").get<std::uint64_t>() != r || r == 0 ||
          row.at("affinity") != "PASS" || !row.at("sink_verified").get<bool>() ||
          !row.at("all_workers_concurrent").get<bool>() || replica < 1 || replica > 3 ||
          worker < 0 || worker >= 8 || !std::isfinite(row.at("t_app_us").get<double>()) ||
          row.at("t_app_us").get<double>() <= 0) {
        reason = "acquired_aux_row_gate_failed:" + cell;
        break;
      }
      if (provider == "P_instr") {
        if (cell.rfind("P_instr_", 0) != 0 || row.at("calibration_all_workers") != true ||
            row.at("calibration_target_seconds") != 2.5 ||
            row.at("disassembly_qa") != "PASS" ||
            row.at("disassembly_sha256") != expected_disassembly_sha) {
          reason = "P_instr_row_provenance_or_calibration_failed:" + cell;
          break;
        }
      } else if (provider == "BW_DRAM") {
        if (cell != "BW_DRAM_256MiB_8P" || row.at("calibration_all_workers") != true ||
            row.at("calibration_target_seconds") != 2.5 ||
            row.at("bytes_per_pass_per_worker") != (256ull * 1024 * 1024) ||
            !row.at("large_pages").get<bool>()) {
          reason = "BW_DRAM_row_provenance_or_calibration_failed";
          break;
        }
      } else {
        reason = "unexpected_aux_provider:" + provider;
        break;
      }
      if (!observed.emplace(cell, replica, worker).second) {
        reason = "duplicate_aux_observation:" + cell;
        break;
      }
      auto [it, inserted] = common_r.emplace(cell, r);
      if (!inserted && it->second != r) {
        reason = "R_common_changed_within_cell:" + cell;
        break;
      }
      ++cell_rows[cell]; cells.insert(cell);
    } catch (const std::exception&) {
      reason = "acquired_aux_jsonl_invalid";
      break;
    }
  }
  if (reason.empty() && input.bad()) reason = "acquired_aux_raw_read_failed";
  if (reason.empty() && cells != expected_cells) reason = "acquired_aux_cell_coverage_invalid";
  if (reason.empty()) {
    for (const auto& cell : expected_cells)
      if (cell_rows[cell] != 24) { reason = "acquired_aux_replica_worker_multiplicity_invalid:" + cell; break; }
  }
  return {{"complete", reason.empty()}, {"reason", reason.empty() ? "none" : reason},
          {"rows", rows}, {"cells", cells.size()}, {"P_instr_variants", cells.size() - (cells.count("BW_DRAM_256MiB_8P") ? 1 : 0)},
          {"BW_DRAM_rows", cell_rows["BW_DRAM_256MiB_8P"]},
          {"N", 3}, {"workers_per_replica", 8}, {"R_common_by_cell", common_r}};
}

int acquire_phase_r18(const std::string& phase, const std::map<std::string, std::string>& args) {
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
  const bool coordinator_ok = phase == "preflight" || harness::set_coordinator_affinity_r18(coordinator_affinity);
  nlohmann::json evidence = {{"phase", phase}, {"session_id", session_id}, {"plan_sha256", plan_sha},
    {"topology_map_sha256", topology_sha}, {"worker_lps", lps}, {"qpc_hz", qpf.QuadPart},
    {"qpc_source", "QueryPerformanceCounter"}, {"previous_evidence_sha256", previous_sha},
    {"numeric_metrics_used", false}, {"coordinator_affinity_ok", coordinator_ok},
    {"coordinator_affinity", coordinator_affinity}};

  if (phase == "preflight") {
    std::string privilege;
    const bool privilege_ok = t0common::enable_large_page_privilege(&privilege);
    std::string coordinator;
    const bool preflight_coordinator_ok = harness::set_coordinator_affinity_r18(coordinator);
    std::string page_reason;
    const bool pages = privilege_ok && large_page_probe(page_reason);
    const bool workers_ok = lps == std::vector<DWORD>{0, 2, 4, 6, 8, 10, 12, 14};
    evidence["large_pages"] = pages; evidence["large_page_privilege"] = privilege;
    evidence["state"] = pages && workers_ok && preflight_coordinator_ok ? "PASS" : "HOLD_RESOURCE";
    evidence["reason"] = page_reason; evidence["coordinator_affinity"] = coordinator;
    evidence["topology_map"] = nlohmann::json::parse(harness::topology_json_r18());
    evidence["preflight_qpc_start"] = start_qpc;
  } else if (phase == "qa_instruments_expulsion") {
    const int kernel = harness::run_kernel_qa_r18();
    const int providers = kernel == 0 ? run_integrated_provider_qa_r18() : 3;
    std::string why; const SIZE_T bytes = GetLargePageMinimum();
    void* buffer = bytes ? VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE) : nullptr;
    bool expulsion = buffer != nullptr;
    if (expulsion) { std::memset(buffer, 0xA5, bytes); omega_evict::evict_range(buffer, bytes); VirtualFree(buffer, 0, MEM_RELEASE); }
    std::cout << "EXPULSION_QA_R18 " << (expulsion ? "PASS" : "HOLD_REAL")
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
    std::ofstream raw(obj / "aux_qualification_raw_r18.jsonl", std::ios::binary | std::ios::out);
    std::ofstream summary(obj / "aux_qualification_summary_r18.jsonl", std::ios::binary | std::ios::out);
    if (!raw || !summary) throw std::runtime_error("qualification_output_open_failed");
    const int rc = coordinator_ok && disasm.at("state") == "PASS" ? harness::run_aux_smoke(raw, summary, 3, disassembly_path) : 3;
    raw.close(); summary.close();
    auto acquired = validate_aux_acquisition_r18((obj / "aux_qualification_raw_r18.jsonl").string(),
                                                 disasm.at("sha256").get<std::string>());
    if (rc != 0 && acquired["reason"] == "none") {
      acquired["complete"] = false; acquired["reason"] = "aux_acquisition_returned_failure";
    }
    const bool acquired_ok = rc == 0 && acquired.at("complete").get<bool>();
    evidence["state"] = acquired_ok ? "SMOKE_ONLY" : "HOLD_INSTRUMENTATION";
    evidence["numeric_metrics_used"] = false;
    evidence["bw_dram"] = acquired_ok && acquired.at("BW_DRAM_rows").get<unsigned>() == 24 ? "ACQUIRED_N3" : "HOLD";
    evidence["p_instr"] = acquired_ok ? "ACQUIRED_N3" : "HOLD";
    evidence["p_instr_variants"] = acquired.at("P_instr_variants");
    evidence["acquired_aux_validation"] = acquired;
    evidence["all_workers_calibrated"] = acquired_ok && acquired.at("cells").get<unsigned>() == 10;
    evidence["N"] = acquired.at("N");
    evidence["disassembly_sha256"] = disasm.at("sha256");
    evidence["qualification_label"] = "SMOKE_MINIMUM_NO_SCIENTIFIC_METRICS";
  } else if (phase == "residency") {
    const std::string outdir = std::filesystem::path(output_path).parent_path().string();
    const auto selected = read_schedule_ids(schedule_path, true);
    const auto sample_ids = read_schedule_ids(schedule_path, false);
    const bool reader_qualified = get("--reader-state") == "QUALIFIED_FOR_REQUIRED_CHANNELS_WITH_DECLARED_LIMITATION";
    const int rc = coordinator_ok ? harness::run_residency_smoke_r18(outdir, plan_path, schedule_path, reader_qualified) : 3;
    const auto actual = read_event_ids(outdir + "\\residency_events_r18.jsonl");
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
    std::cout << "ACQUIRE_RESIDENCY_R18 " << (evidence["block_complete"].get<bool>() ? "PASS" : "HOLD")
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
  std::cout << "ACQUIRE_R18 phase=" << phase << " state=" << state
            << " plan_sha256=" << plan_sha << " topology_map_sha256=" << topology_sha
            << " previous_evidence=" << (previous_sha.empty() ? "NONE" : "VERIFIED") << "\n";
  return 0;
}

int run_instrument_qualification_r18(const std::string& outdir, const std::string& plan_path,
                                     const std::string& schedule_path) {
  const std::string evidence_path = outdir + "\\instrument_qualification_acquisition_r18.json";
  try {
    if (!emon_reader_process_running_r18())
      throw std::runtime_error("EMON_reader_process_absent_measurements_not_started");
    std::ifstream plan_input(plan_path, std::ios::binary);
    if (!plan_input) throw std::runtime_error("S25B_plan_missing");
    const auto plan = nlohmann::json::parse(plan_input);
    const auto selected = read_schedule_ids(schedule_path, true);
    if (selected.size() != 6 && selected.size() != 48)
      throw std::runtime_error("S25B_schedule_must_be_N1_smoke_or_36_blocks");
    const bool smoke_n1 = selected.size() == 6;
    std::vector<DWORD> worker_lps;
    const std::string topology_sha = topology_sha256(worker_lps);
    std::string coordinator_affinity;
    if (!harness::set_coordinator_affinity_r18(coordinator_affinity))
      throw std::runtime_error("S25B_coordinator_affinity_failed:" + coordinator_affinity);
    const int rc = harness::run_residency_smoke_r18(outdir, plan_path, schedule_path, true);
    const auto actual = read_event_ids(outdir + "\\residency_events_r18.jsonl");
    bool order_ok = actual == selected;
    bool measurements_ok = rc == 0 && order_ok;
    std::map<std::pair<int, int>, std::map<std::string, std::uint64_t>> calibration_r;
    std::map<std::tuple<int, int, int>, std::map<std::string, std::uint64_t>> residency_r;
    std::ifstream event_input(outdir + "\\residency_events_r18.jsonl", std::ios::binary);
    std::string line; std::size_t calibration_count = 0, residency_count = 0;
    while (std::getline(event_input, line)) {
      if (line.empty()) continue;
      const auto row = nlohmann::json::parse(line);
      const std::string phase = row.at("phase").get<std::string>();
      const int m = row.at("m").get<int>(), k = row.at("K").get<int>();
      const std::string variant = row.at("variant").get<std::string>();
      const auto r = row.at("R").get<std::uint64_t>();
      if (phase == "calibration") {
        ++calibration_count;
        if (row.at("R_policy") != "R(W,m,K)_common_A_B_C_fixed_across_S25B_replicas" ||
            row.at("R_unit_id") != row.at("unit_id") ||
            row.at("condition_region_seconds").get<double>() < 75.0 ||
            row.at("slowest_worker_kernel_seconds").get<double>() < .150 ||
            row.at("instrumentation_overhead_fraction_max").get<double>() > .01 ||
            row.at("worker_evidence").size() != 8) measurements_ok = false;
        calibration_r[{m, k}][variant] = r;
      } else if (phase == "residency") {
        ++residency_count;
        if (row.at("instrument_qualification") != "S25B" ||
            row.at("region_target_seconds").get<double>() != 75.0 ||
            row.at("region_minimum_seconds").get<double>() != 60.0 ||
            row.at("condition_region_seconds").get<double>() < 60.0 ||
            row.at("instrumentation_overhead_fraction_max").get<double>() > .01 ||
            row.at("worker_evidence").size() != 8) measurements_ok = false;
        residency_r[{m, k, row.at("replica").get<int>()}][variant] = r;
      } else measurements_ok = false;
    }
    const auto expected_calibration = smoke_n1 ? 3u : 12u;
    const auto expected_residency = smoke_n1 ? 3u : 36u;
    if (calibration_count != expected_calibration || residency_count != expected_residency || event_input.bad())
      measurements_ok = false;
    for (const auto& [mk, variants] : calibration_r) {
      if (variants.size() != 3 || variants.at("A") != variants.at("B") || variants.at("A") != variants.at("C"))
        measurements_ok = false;
    }
    for (const auto& [key, variants] : residency_r) {
      const auto [m, k, replica] = key; (void)replica;
      const auto cal = calibration_r.find({m, k});
      if (variants.size() != 3 || variants.at("A") != variants.at("B") ||
          variants.at("A") != variants.at("C") || cal == calibration_r.end() ||
          cal->second.at("A") != variants.at("A")) measurements_ok = false;
    }
    const bool coverage_ok = calibration_r.size() == (smoke_n1 ? 1u : 4u) &&
                             residency_r.size() == (smoke_n1 ? 1u : 12u);
    measurements_ok = measurements_ok && coverage_ok;
    nlohmann::json evidence = {
      {"schema_version", "S25B_ACQUISITION_R18_V1"}, {"plan_sha256", plan.at("sha256")},
      {"schedule_sha256", sha256_file(schedule_path)}, {"topology_map_sha256", topology_sha},
      {"mode", smoke_n1 ? "SMOKE_N1" : "FULL_36_BLOCKS"}, {"selected_event_count", selected.size()},
      {"acquired_event_count", actual.size()}, {"event_order_matches_schedule", order_ok},
      {"calibration_events", calibration_count}, {"residency_events", residency_count},
      {"R_common_by_m_K_A_B_C", coverage_ok && measurements_ok},
      {"coordinator_affinity", coordinator_affinity}, {"reader_process_observed", true},
      {"state", measurements_ok ? "ACQUISITION_COMPLETE" : "HOLD_INSTRUMENTATION_QUALIFICATION"},
      {"numeric_metrics_used", false}, {"attempt_label", "SMOKE"}};
    write_json_new(evidence_path, evidence);
    std::cout << "INSTRUMENT_QUALIFICATION_R18 "
              << (measurements_ok ? "ACQUISITION_COMPLETE" : "HOLD_INSTRUMENTATION_QUALIFICATION")
              << " mode=" << (smoke_n1 ? "SMOKE_N1" : "FULL_36_BLOCKS")
              << " calibration=" << calibration_count << " residency=" << residency_count
              << " R_common_by_m_K=" << (coverage_ok ? "true" : "false")
              << " event_order=" << (order_ok ? "PASS" : "FAIL") << " S20_verdict=pending\n";
    return measurements_ok ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "INSTRUMENT_QUALIFICATION_R18 HOLD_INSTRUMENTATION_QUALIFICATION reason="
              << error.what() << " measurements_started="
              << (emon_reader_process_running_r18() ? "possible" : "false") << "\n";
    return 3;
  }
}

int run_collector_entry(const wchar_t* mode, const wchar_t* session_id = nullptr) {
  std::string privilege_status;
  if (!t0common::enable_large_page_privilege(&privilege_status)) {
    std::cerr << "HARNESS_R18_ENTRY HOLD_REAL large_page_privilege=" << privilege_status
              << " measurements_started=false\n";
    return 20;
  }
  std::cout << "LARGE_PAGE_PRIVILEGE=" << privilege_status
            << " enable_large_page_privilege_called=true\n";
  std::wstring module(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (length == 0 || length >= module.size()) {
    std::cerr << "HARNESS_R18_ENTRY REJECT executable_path_unavailable measurements_started=false\n";
    return 20;
  }
  module.resize(length);
  const std::filesystem::path executable(module);
  const auto root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
  const auto script = root / L"T0P" / L"collector" / L"official_run_r18.py";
  if (!std::filesystem::is_regular_file(script)) {
    std::wcerr << L"HARNESS_R18_ENTRY REJECT orchestrator_missing=" << script.c_str()
               << L" measurements_started=false\n";
    return 20;
  }
  const std::filesystem::path python = L"C:\\Program Files\\Python313\\python.exe";
  if (!std::filesystem::is_regular_file(python)) {
    std::cerr << "HARNESS_R18_ENTRY REJECT python_runtime_missing measurements_started=false\n";
    return 20;
  }

  std::vector<std::wstring> args{
      L"-B", script.wstring(), mode,
      L"--integrated-executable", executable.wstring()};
  if (std::wstring(mode) == L"--official") {
    args.push_back(L"--authorized");
    args.push_back((root / L"T0P" / L"harness" / L"AUTHORIZED.json").wstring());
  }
  if (std::wstring(mode) == L"--self-test") {
    args.push_back(L"--output");
    args.push_back((root / L"T0P" / L"collector" / L"logs_r18" / L"obj_r18" /
                    (L"official_run_r18_qa_" + std::to_wstring(GetTickCount64()) + L".txt")).wstring());
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
  if (_wputenv_s(L"OMEGA_R18_INTEGRATED_LAUNCH", L"1") != 0) {
    std::cerr << "HARNESS_R18_ENTRY REJECT integrated_launch_token_failed measurements_started=false\n";
    return 20;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(python.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                      0, nullptr, root.c_str(), &startup, &process)) {
    std::cerr << "HARNESS_R18_ENTRY REJECT python_launch_failed measurements_started=false error="
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

int run_integrated_provider_qa_r18() {
  std::string lp; if (!t0common::enable_large_page_privilege(&lp)) {
    std::cerr << "PROVIDER_QA_R18 BLOCKED_REAL privilege=" << lp << " measurements_started=false\n"; return 3;
  }
  std::string topology;
  if (!harness::discover_topology_r18(topology)) {
    std::cerr << "PROVIDER_QA_R18 BLOCKED_REAL topology=" << topology << " measurements_started=false\n"; return 3;
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
          if (!harness::set_worker_affinity_r18(worker) || !harness::worker_affinity_matches_r18(worker))
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
      if (!pass) { std::cerr << "PROVIDER_QA_R18 HOLD_REAL " << error << "\n"; return 3; }
      ++configurations; worker_dispatches += 8;
      std::cout << "PROVIDER_DISPATCH_R18 PASS family=" << item.name << " m=" << m << " frontier=" << frontier
                << " workers=8 LP_map=topology_r18 buffers=large_pages effective_dispatch=8/8\n";
    }
  }
  std::cout << "PROVIDER_QA_R18 PASS configurations=" << configurations << " worker_dispatches=" << worker_dispatches
            << " large_page_regions=" << large_page_regions.load() << " privilege=" << lp << " providers=OMEGA,ggml_flat,ggml_repack,Torch_mm,matmul,linear,mv frontiers=prepared,dynamic\n";
  return 0;
}

int run_instrument_qa_r18(const std::string& outdir, const std::string& disassembly) {
  if (harness::run_kernel_qa_r18() != 0) return 3;
  if (run_integrated_provider_qa_r18() != 0) return 3;
  std::ofstream raw(outdir + "\\aux_qualification_raw_r18.jsonl", std::ios::binary | std::ios::trunc);
  std::ofstream summary(outdir + "\\aux_qualification_summary_r18.jsonl", std::ios::binary | std::ios::trunc);
  if (!raw || !summary) throw std::runtime_error("instrument QA output files could not be opened");
  const int rc = harness::run_aux_smoke(raw, summary, 3, disassembly);
  raw.close(); summary.close();
  std::cout << "INSTRUMENT_QA_R18 " << (rc == 0 ? "PASS" : "HOLD_REAL") << " P_instr=9_variants_N3 BW_DRAM=256MiB_8_workers label=SMOKE metrics_used=false\n";
  return rc;
}
}

int main(int argc, char** argv) {
  try {
    std::string lock_privilege_status;
    if (!t0common::enable_large_page_privilege(&lock_privilege_status)) {
      std::cerr << "HARNESS_R18_BLOCKED_REAL SeLockMemoryPrivilege=" << lock_privilege_status
                << " measurements_started=false\n";
      return 20;
    }
    std::cout << "PROCESS_PRIVILEGE_R18 SeLockMemoryPrivilege=ENABLED\n";
    if (argc == 2 && std::string(argv[1]) == "--barrier-cancel-test-r18")
      return harness::barrier_cancel_test_r18();
    if (argc == 5 && std::string(argv[1]) == "--instrument-qualification-r18")
      return run_instrument_qualification_r18(argv[2], argv[3], argv[4]);
    if (argc >= 3 && std::string(argv[1]) == "--acquire-r18") {
      std::map<std::string, std::string> options;
      for (int i = 3; i + 1 < argc; i += 2) {
        if (argv[i][0] != '-' || !options.emplace(argv[i], argv[i + 1]).second)
          throw std::runtime_error("acquisition_cli_duplicate_or_invalid_option");
      }
      if (((argc - 3) % 2) != 0) throw std::runtime_error("acquisition_cli_option_value_missing");
      return acquire_phase_r18(argv[2], options);
    }
    if (argc >= 2 && std::string(argv[1]) == "--residency-smoke-r18") {
      std::cerr << "ACQUISITION_GATE_REQUIRED measurements_started=false\n";
      return 2;
    }
    if (argc == 4 && std::string(argv[1]) == "--plan-qa-r18")
      return harness::verify_full_plan_smoke_slice_r18(argv[2], argv[3]);
    if (argc == 4 && std::string(argv[1]) == "--instrument-qa-r18")
      return run_instrument_qa_r18(argv[2], argv[3]);
    if (argc == 2 && std::string(argv[1]) == "--provider-qa-r18")
      return run_integrated_provider_qa_r18();
    if (argc == 2 && std::string(argv[1]) == "--qa-r18")
      return harness::run_kernel_qa_r18();
    if (argc == 4 && std::string(argv[1]) == "--official" &&
        std::string(argv[2]) == "--session-id")
      return run_collector_entry(L"--official", std::wstring(argv[3], argv[3] + std::strlen(argv[3])).c_str());
    if (argc == 2 && std::string(argv[1]) == "--synthetic-self-test")
      return run_collector_entry(L"--self-test");
    std::cerr << "HARNESS_R18 usage=--official --session-id <id> | --synthetic-self-test | --qa-r18 | --plan-qa-r18 <plan.json> <schedule.tsv> | --provider-qa-r18 | --instrument-qa-r18 <outdir> <disassembly.json> | "
                 "--residency-smoke-r18 <new-attempt-dir> <official-plan-r18.json> <schedule.tsv>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "HARNESS_R18_ERROR " << e.what()
              << " measurements_label=SMOKE numeric_metrics_used=false\n";
    return 1;
  }
}
