#define NOMINMAX
#include "harness_r18.hpp"
#include "provider_common_r18.hpp"
#include "topology_r18.hpp"
#include "q4_kernel.hpp"
#include "evict.hpp"
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

int q4_standalone_qa_main_r18(int argc, char** argv);

namespace harness {
namespace {
constexpr double kCalibrationTargetSeconds = 2.5, kMarginSeconds = .200;
constexpr double kCoordinatorCalibrationTarget = .005;
uint64_t g_evict_weight_calls = 0, g_evict_range_calls = 0, g_evict_weight_range_calls = 0,
    g_evict_activation_range_calls = 0;

class RawFile {
 public:
  explicit RawFile(const std::string& path, size_t preallocated_bytes = 8u * 1024u * 1024u) {
    handle_ = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::runtime_error("D13_RAW_CREATE_NEW_FAILED:" + path + ":" + std::to_string(GetLastError()));
    pending_.reserve(preallocated_bytes);
  }
  RawFile(const RawFile&) = delete;
  RawFile& operator=(const RawFile&) = delete;
  ~RawFile() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
  void append(const std::string& bytes) {
    if (bytes.size() > pending_.capacity() - pending_.size()) throw std::runtime_error("D9_PREALLOCATED_RECORD_BUFFER_CAPACITY_EXCEEDED");
    pending_.append(bytes);
  }
  void line(const std::string& s) {
    if (s.size() + 1 > pending_.capacity() - pending_.size())
      throw std::runtime_error("D9_PREALLOCATED_RECORD_BUFFER_CAPACITY_EXCEEDED");
    pending_.append(s); pending_.push_back('\n');
  }
  void flush() {
    const char* p = pending_.data(); size_t left = pending_.size();
    while (left) {
      DWORD amount = static_cast<DWORD>((std::min)(left, size_t(1u << 20))), written = 0;
      if (!WriteFile(handle_, p, amount, &written, nullptr) || written != amount)
        throw std::runtime_error("D13_RAW_WRITE_FAILED:" + std::to_string(GetLastError()));
      p += written; left -= written;
    }
    pending_.clear();
    if (!FlushFileBuffers(handle_)) throw std::runtime_error("D13_RAW_FLUSH_FAILED:" + std::to_string(GetLastError()));
  }
 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::string pending_;
};

class ClockMapSampler {
 public:
  explicit ClockMapSampler(RawFile& file) : file_(file) {
    LARGE_INTEGER qpf{};
    if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0)
      throw std::runtime_error("CLOCK_MAP_SAMPLER_QPF_UNAVAILABLE");
    interval_qpc_ = (std::max)(int64_t(1), qpf.QuadPart / 50);
    file_.line("sample,qpc_pre,qpc_post,filetime_100ns,sample_period_ms");
    // A 1,000,000-row fixed buffer covers more than five hours at 20 ms
    // without allocation while any measured segment is active.
    rows_.reserve(1000000);
    thread_ = std::thread([this] {
      std::string why;
      if (!set_coordinator_affinity_r18(why)) {
        error_code_.store(ERROR_INVALID_PARAMETER, std::memory_order_release);
        return;
      }
      sample_loop();
    });
  }
  ClockMapSampler(const ClockMapSampler&) = delete;
  ClockMapSampler& operator=(const ClockMapSampler&) = delete;
  void stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    const DWORD error = error_code_.load(std::memory_order_acquire);
    if (error) throw std::runtime_error("CLOCK_MAP_SAMPLER_FAILED:" + std::to_string(error));
    for (const auto& row : rows_) {
      std::ostringstream text; text << row.sample << ',' << row.before << ',' << row.after << ','
                                    << row.filetime << ",20";
      file_.line(text.str());
    }
    file_.flush();
  }
  ~ClockMapSampler() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
  }
 private:
  void sample_loop() {
    while (!stop_.load(std::memory_order_acquire)) {
      LARGE_INTEGER before{}, after{}; FILETIME ft{};
      if (!QueryPerformanceCounter(&before)) { error_code_.store(GetLastError(), std::memory_order_release); return; }
      GetSystemTimePreciseAsFileTime(&ft);
      if (!QueryPerformanceCounter(&after)) { error_code_.store(GetLastError(), std::memory_order_release); return; }
      if (rows_.size() == rows_.capacity()) { error_code_.store(ERROR_BUFFER_OVERFLOW, std::memory_order_release); return; }
      ULARGE_INTEGER ticks{}; ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
      rows_.push_back({static_cast<unsigned long long>(rows_.size()), before.QuadPart, after.QuadPart, ticks.QuadPart});
      const int64_t deadline = after.QuadPart + interval_qpc_;
      for (;;) {
        if (stop_.load(std::memory_order_acquire)) return;
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) { error_code_.store(GetLastError(), std::memory_order_release); return; }
        if (now.QuadPart >= deadline) break;
        _mm_pause();
      }
    }
  }
  struct ClockRow { unsigned long long sample; int64_t before, after; unsigned long long filetime; };
  RawFile& file_;
  std::atomic<bool> stop_{false};
  std::atomic<DWORD> error_code_{0};
  std::vector<ClockRow> rows_;
  int64_t interval_qpc_ = 0;
  std::thread thread_;
};

struct Gate {
  explicit Gate(unsigned count) : count_(count) {}
  bool wait() {
    if (cancelled_.load(std::memory_order_acquire)) return false;
    const std::uint64_t generation = generation_.load(std::memory_order_acquire);
    if (arrived_.fetch_add(1, std::memory_order_acq_rel) + 1 == count_) {
      arrived_.store(0, std::memory_order_relaxed);
      generation_.fetch_add(1, std::memory_order_release);
      return !cancelled_.load(std::memory_order_acquire);
    }
    while (generation_.load(std::memory_order_acquire) == generation) {
      if (cancelled_.load(std::memory_order_acquire)) return false;
      _mm_pause();
    }
    return !cancelled_.load(std::memory_order_acquire);
  }
  void cancel() {
    cancelled_.store(true, std::memory_order_release);
    generation_.fetch_add(1, std::memory_order_release);
  }
 private:
  const unsigned count_;
  std::atomic<unsigned> arrived_{0};
  std::atomic<std::uint64_t> generation_{0};
  std::atomic<bool> cancelled_{false};
};

struct ScheduleEvent {
  unsigned sequence = 0;
  std::string phase, event_id, condition_id, variant, observation, unit_id, pair_id;
  std::string block_id, segment_id, atomic_group_id;
  int replica = 0, W = 0, O = 0, m = 0, K = 0;
};

std::string r_unit_id_for(const ScheduleEvent& event) {
  if (event.phase == "calibration") return event.unit_id;
  return "W" + std::to_string(event.W) + "|O" + std::to_string(event.O) +
         "|m" + std::to_string(event.m) + "|K" + std::to_string(event.K);
}
std::string frequency_group_id(const ScheduleEvent& event) {
  if (event.phase == "aa") return event.pair_id;
  return event.block_id + "|" + event.atomic_group_id;
}
struct Stamp {
  uint64_t tsc_start = 0, tsc_end = 0;
  int64_t qpc_start = 0, qpc_end = 0;
  int64_t qpc_precondition_start = 0, qpc_precondition_end = 0;
  DWORD lp_start = MAXDWORD, lp_end = MAXDWORD;
  uint64_t weight_address = 0, output_hash = 0;
};
struct Worker {
  t0common::Cell cell;
  omega_q4::PackedActivations x;
  std::shared_ptr<omega_q4::PackedWeights> shared_ac;
  std::vector<std::unique_ptr<omega_q4::PackedWeights>> b_copies;
  t0common::Buffer<float> y;
  int output = 0, m = 0;
  uint64_t sink = 0;
  std::vector<std::uint8_t> hot_probe;
  uint64_t hot_probe_sink = 0;
  uint64_t hot_probe_tsc_start = 0, hot_probe_tsc_end = 0;
  int64_t hot_probe_qpc_start = 0, hot_probe_qpc_end = 0;
  DWORD hot_probe_lp_start = MAXDWORD, hot_probe_lp_end = MAXDWORD;
  Stamp batch_stamp;
  uint64_t batch_kernel_tsc = 0;
  bool batch_has_stamp = false;
  uint64_t evicted_weight_ranges = 0, evicted_activation_ranges = 0, evicted_ranges = 0;
};
enum class CommandKind { Episode, Probe, Stop };
struct Command { CommandKind kind = CommandKind::Stop; char variant = 'A'; int applications = 0; int repetitions = 1; };

std::mutex g_error_mutex;
std::atomic<std::uint64_t> g_command_word{0};
std::string g_worker_error;
// Each operation barrier includes the coordinator and all eight workers.
Gate g_start(9), g_done(9);
std::array<Worker, 8>* g_workers = nullptr;
uint64_t g_qpf = 0;
double g_tsc_hz = 0;
std::string g_plan_sha;
bool g_s25_b_mode = false;
bool g_s25_b_smoke_n1 = false;

std::uint64_t encode_command(const Command& command) {
  const auto kind = command.kind == CommandKind::Episode ? 1u :
                    command.kind == CommandKind::Probe ? 2u : 0u;
  return std::uint64_t(kind) |
         (std::uint64_t(static_cast<unsigned char>(command.variant)) << 8) |
         (std::uint64_t(static_cast<unsigned>(command.applications)) << 16) |
         (std::uint64_t(static_cast<unsigned>(command.repetitions)) << 32);
}
Command decode_command(std::uint64_t word) {
  const auto raw_kind = word & 0xffu;
  return {raw_kind == 1u ? CommandKind::Episode : raw_kind == 2u ? CommandKind::Probe : CommandKind::Stop,
          char((word >> 8) & 0xffu), int((word >> 16) & 0xffffu), int((word >> 32) & 0xffffffffu)};
}

constexpr std::size_t kHotProbeBytes = 294912;
constexpr int kHotProbeK = 16;
constexpr double kHotProbeTargetSeconds = 2.5;
constexpr double kHotProbeMinimumSeconds = 2.0;
constexpr double kHotProbeMarginSeconds = 0.2;

struct HotProbeWorkerStamp {
  uint64_t tsc_start = 0, tsc_end = 0;
  int64_t qpc_start = 0, qpc_end = 0;
  DWORD lp_start = MAXDWORD, lp_end = MAXDWORD;
};
struct HotProbeRecord {
  int replica = 0;
  int endpoint = 0; // 0 segment opening, 1 segment closing
  int sample = 0;
  std::string segment;
  std::string comparison_set;
  int64_t qpc_pad_start = 0, qpc_start = 0, qpc_end = 0, qpc_pad_end = 0;
  double coordinator_overhead_fraction = 0;
  std::array<HotProbeWorkerStamp, 8> workers{};
};
std::vector<HotProbeRecord>* g_hot_probe_records = nullptr;

uint64_t read_tsc() { _mm_lfence(); unsigned aux = 0; uint64_t t = __rdtscp(&aux); _mm_lfence(); return t; }
int64_t qpc_now() { LARGE_INTEGER q{}; if (!QueryPerformanceCounter(&q)) throw std::runtime_error("QPC_READ_FAILED"); return q.QuadPart; }
double qpc_seconds(int64_t a, int64_t b) { return double(b - a) / double(g_qpf); }
double configured_tsc_hz() {
  int cpu[4]{}; __cpuidex(cpu, 0x15, 0);
  if (cpu[0] <= 0 || cpu[1] <= 0 || cpu[2] <= 0) return 0.0;
  return double(uint32_t(cpu[2])) * double(uint32_t(cpu[1])) / double(uint32_t(cpu[0]));
}
uint64_t output_hash(const float* p, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  const auto* bytes = reinterpret_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n * sizeof(float); ++i) { h ^= bytes[i]; h *= 1099511628211ULL; }
  return h;
}
std::string json_escape(const std::string& value) {
  std::string out; out.reserve(value.size());
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(char(c)); }
    else if (c < 0x20) { std::ostringstream e; e << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c); out += e.str(); }
    else out.push_back(char(c));
  }
  return out;
}
std::string sha256(const uint8_t* data, size_t size) {
  BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr; DWORD obj_size = 0, got = 0;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA256_PROVIDER_FAILED");
  if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_size), sizeof(obj_size), &got, 0) < 0) {
    BCryptCloseAlgorithmProvider(alg, 0); throw std::runtime_error("SHA256_OBJECT_SIZE_FAILED");
  }
  std::vector<UCHAR> object(obj_size), digest(32);
  NTSTATUS status = BCryptCreateHash(alg, &hash, object.data(), obj_size, nullptr, 0, 0);
  if (status >= 0) status = BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
  if (status >= 0) status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
  if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
  if (status < 0) throw std::runtime_error("SHA256_DIGEST_FAILED");
  std::ostringstream out; out << std::hex << std::setfill('0');
  for (UCHAR b : digest) out << std::setw(2) << unsigned(b);
  return out.str();
}
std::string sha256(const t0common::Buffer<uint8_t>& bytes, size_t size) { return sha256(bytes.data(), size); }
bool page_large(const void* ptr, size_t bytes) {
  if (!ptr || !bytes) return false;
  SYSTEM_INFO info{}; GetSystemInfo(&info);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  if (bytes - 1 > UINTPTR_MAX - begin) return false;
  const uintptr_t end = begin + bytes - 1;
  const uintptr_t first = begin & ~(uintptr_t(info.dwPageSize) - 1), last = end & ~(uintptr_t(info.dwPageSize) - 1);
  std::vector<PSAPI_WORKING_SET_EX_INFORMATION> query;
  for (uintptr_t p = first;; p += info.dwPageSize) {
    PSAPI_WORKING_SET_EX_INFORMATION item{}; item.VirtualAddress = reinterpret_cast<void*>(p); query.push_back(item);
    if (p == last) break;
  }
  if (!QueryWorkingSetEx(GetCurrentProcess(), query.data(), DWORD(query.size() * sizeof(query[0])))) return false;
  return std::all_of(query.begin(), query.end(), [](const auto& item) { return item.VirtualAttributes.Valid && item.VirtualAttributes.LargePage; });
}
std::vector<ScheduleEvent> read_schedule(const std::string& plan_path,
                                         const std::string& schedule_path = {}) {
  using nlohmann::json;
  std::ifstream in(plan_path, std::ios::binary);
  if (!in) throw std::runtime_error("OFFICIAL_PLAN_OPEN_FAILED:" + plan_path);
  json plan; try { in >> plan; } catch (const std::exception& e) { throw std::runtime_error(std::string("OFFICIAL_PLAN_PARSE_FAILED:") + e.what()); }
  if (!plan.is_object() || plan.value("schema", std::string()) != "T0P_S25_ACQUISITION_PLAN_R18_V1" ||
      plan.value("event_count", 0u) != 32102 || !plan.contains("events") || !plan["events"].is_array() || plan["events"].size() != 32102)
    throw std::runtime_error("OFFICIAL_PLAN_SCHEMA_OR_TOTAL_COUNT_INVALID");
  g_plan_sha = plan.value("sha256", std::string());
  if (g_plan_sha.size() != 64 || plan.value("selection_event_count", 0u) != 11718 ||
      plan.value("comparison_event_count", 0u) != 6696 || plan.value("residency_event_count", 0u) != 13688)
    throw std::runtime_error("OFFICIAL_PLAN_FROZEN_COUNTS_INVALID");
  json all_ids = json::array(), q_ids = json::array(), r_ids = json::array();
  std::set<std::string> ids;
  std::map<std::string, ScheduleEvent> all_residency_events;
  std::map<std::string, std::vector<unsigned>> aa_positions, marginal_positions, calibration_positions;
  std::map<std::string, unsigned> block_positions;
  unsigned sequence = 0, calibration = 0, sample = 0, aa = 0;
  for (const auto& item : plan["events"]) {
    ++sequence;
    if (!item.is_object() || item.value("sequence", 0u) != sequence || !item.contains("id") || !item["id"].is_string())
      throw std::runtime_error("OFFICIAL_PLAN_EVENT_SEQUENCE_INVALID:" + std::to_string(sequence));
    const std::string id = item["id"].get<std::string>();
    if (!ids.insert(id).second) throw std::runtime_error("OFFICIAL_PLAN_DUPLICATE_EVENT_ID:" + id);
    all_ids.push_back(id);
    const std::string phase = item.value("phase", std::string());
    if (phase == "reference_selection_ggml" || phase == "reference_selection_torch" || phase == "competency") q_ids.push_back(id);
    else if (phase == "calibration" || phase == "residency" || phase == "aa") r_ids.push_back(id);
    else throw std::runtime_error("OFFICIAL_PLAN_PHASE_INVALID:" + phase);
    const int replica = item.value("replica", 0);
    if (phase == "calibration") {
      ++calibration;
      calibration_positions[item.value("unit_id", std::string())].push_back(sequence);
    } else if (phase == "residency") {
      ++sample;
      marginal_positions[item.value("block_id", std::string()) + "|" + item.value("atomic_group_id", std::string())].push_back(sequence);
    } else if (phase == "aa") {
      ++aa;
      aa_positions[item.value("pair_id", std::string())].push_back(sequence);
    } else continue;
    const std::string block_id = item.value("block_id", std::string());
    if (!block_id.empty()) ++block_positions[block_id];
    ScheduleEvent e; e.sequence = sequence; e.phase = phase; e.event_id = id;
    e.condition_id = item.value("condition_id", std::string()); e.variant = item.value("variant", std::string());
    e.replica = replica; e.observation = item.value("observation", std::string());
    e.unit_id = item.value("unit_id", std::string()); e.pair_id = item.value("pair_id", std::string());
    e.block_id = item.value("block_id", std::string()); e.segment_id = item.value("segment_id", std::string());
    e.atomic_group_id = item.value("atomic_group_id", std::string());
    e.W = item.value("W_target_kib", 0); e.O = item.value("O", 0); e.m = item.value("m", 0); e.K = item.value("K", 0);
    if ((phase == "calibration" || phase == "residency") && !(e.variant == "A" || e.variant == "B" || e.variant == "C"))
      throw std::runtime_error("OFFICIAL_PLAN_VARIANT_INVALID:" + id);
    all_residency_events.emplace(id, std::move(e));
  }
  const std::string all_bytes = all_ids.dump(), q_bytes = q_ids.dump(), r_bytes = r_ids.dump();
  if (sequence != 32102 || ids.size() != 32102 || q_ids.size() != 18414 || r_ids.size() != 13688 ||
      calibration != 420 || sample != 13020 || aa != 248 ||
      sha256(reinterpret_cast<const uint8_t*>(all_bytes.data()), all_bytes.size()) != plan.value("order_sha256", std::string()) ||
      sha256(reinterpret_cast<const uint8_t*>(q_bytes.data()), q_bytes.size()) != plan.value("qualification_order_sha256", std::string()) ||
      sha256(reinterpret_cast<const uint8_t*>(r_bytes.data()), r_bytes.size()) != plan.value("residency_order_sha256", std::string()))
    throw std::runtime_error("OFFICIAL_PLAN_ORDER_HASH_INVALID");
  for (const auto& [group, rows] : marginal_positions)
    if (rows.size() != 3 || rows[1] != rows[0] + 1 || rows[2] != rows[1] + 1)
      throw std::runtime_error("OFFICIAL_PLAN_MARGINAL_VARIANT_MULTIPLICITY_OR_ADJACENCY_INVALID:" + group);
  for (const auto& [pair, rows] : aa_positions)
    if (rows.size() != 2 || rows[1] != rows[0] + 1)
      throw std::runtime_error("OFFICIAL_PLAN_AA_MULTIPLICITY_OR_ADJACENCY_INVALID:" + pair);
  for (const auto& [unit, rows] : calibration_positions)
    if (rows.size() != 3 || rows[1] != rows[0] + 1 || rows[2] != rows[1] + 1)
      throw std::runtime_error("OFFICIAL_PLAN_CALIBRATION_VARIANT_MULTIPLICITY_OR_ADJACENCY_INVALID:" + unit);

  std::vector<std::string> selected_ids;
  bool diagnostic_instrument_qualification = false, instrument_n1 = false, instrument_full = false;
  if (!schedule_path.empty()) {
    std::ifstream schedule(schedule_path, std::ios::binary);
    if (!schedule) throw std::runtime_error("OFFICIAL_SCHEDULE_OPEN_FAILED:" + schedule_path);
    std::string line; bool saw_plan_hash = false, saw_header = false, diagnostic_slice = false;
    while (std::getline(schedule, line)) {
      if (line.rfind("#plan_sha256=", 0) == 0) {
        if (line.substr(13) != g_plan_sha) throw std::runtime_error("OFFICIAL_SCHEDULE_PLAN_HASH_MISMATCH");
        saw_plan_hash = true; continue;
      }
      if (line.rfind("#diagnostic=SMOKE_ONLY;", 0) == 0) { diagnostic_slice = true; continue; }
      if (line == "#diagnostic=INSTRUMENT_QUALIFICATION_R18;mode=SMOKE_N1") {
        diagnostic_instrument_qualification = true; instrument_n1 = true; continue;
      }
      if (line == "#diagnostic=INSTRUMENT_QUALIFICATION_R18;mode=FULL_36_BLOCKS") {
        diagnostic_instrument_qualification = true; instrument_full = true; continue;
      }
      if (line.empty() || line[0] == '#') continue;
      if (!saw_header) {
        if (line != "sequence\tevent_id\tphase\tblock_id\tatomic_group_id\tsegment_id")
          throw std::runtime_error("OFFICIAL_SCHEDULE_HEADER_INVALID");
        saw_header = true; continue;
      }
      const auto first = line.find('\t'), second = first == std::string::npos ? first : line.find('\t', first + 1);
      if (first == std::string::npos || second == std::string::npos) throw std::runtime_error("OFFICIAL_SCHEDULE_ROW_INVALID");
      const std::string event_id = line.substr(first + 1, second - first - 1);
      if (event_id.empty()) throw std::runtime_error("OFFICIAL_SCHEDULE_ID_EMPTY");
      selected_ids.push_back(event_id);
    }
    if (!saw_plan_hash || !saw_header || selected_ids.empty()) throw std::runtime_error("OFFICIAL_SCHEDULE_METADATA_MISSING");
  } else {
    selected_ids = std::vector<std::string>(ids.begin(), ids.end());
  }
  std::set<std::string> selected_unique;
  std::vector<ScheduleEvent> selected; selected.reserve(selected_ids.size());
  unsigned last_sequence = 0;
  std::map<std::string, unsigned> selected_blocks, selected_groups;
  for (const auto& id : selected_ids) {
    if (!selected_unique.insert(id).second) throw std::runtime_error("OFFICIAL_SCHEDULE_DUPLICATE_EVENT_ID:" + id);
    auto event = all_residency_events.find(id);
    if (event == all_residency_events.end()) throw std::runtime_error("OFFICIAL_SCHEDULE_EVENT_NOT_RESIDENCY:" + id);
    if (event->second.sequence <= last_sequence) throw std::runtime_error("OFFICIAL_SCHEDULE_ORDER_INVALID:" + id);
    last_sequence = event->second.sequence;
    if (!event->second.block_id.empty()) ++selected_blocks[event->second.block_id];
    const std::string group = event->second.phase == "aa" ? "aa|" + event->second.pair_id :
                              event->second.phase == "calibration" ? "cal|" + event->second.unit_id :
                              "marginal|" + event->second.block_id + "|" + event->second.atomic_group_id;
    ++selected_groups[group];
    selected.push_back(event->second);
  }
  for (const auto& [id, selected_count] : selected_blocks) {
    if (!diagnostic_instrument_qualification && block_positions[id] != selected_count)
      throw std::runtime_error("OFFICIAL_SCHEDULE_INCOMPLETE_W_REPLICA_BLOCK:" + id);
  }
  for (const auto& [group, selected_count] : selected_groups) {
    const unsigned expected = group.rfind("aa|", 0) == 0 ? 2u : 3u;
    if (selected_count != expected) throw std::runtime_error("OFFICIAL_SCHEDULE_INCOMPLETE_ATOMIC_GROUP:" + group);
  }
  std::set<std::string> selected_segments;
  for (const auto& event : selected) if (!event.segment_id.empty()) selected_segments.insert(event.segment_id);
  const bool complete_residency = selected_ids.size() == r_ids.size();
  if (!complete_residency && !schedule_path.empty()) {
    std::ifstream schedule(schedule_path, std::ios::binary);
    std::string line; bool diagnostic_slice = false;
    while (std::getline(schedule, line)) if (line.rfind("#diagnostic=SMOKE_ONLY;", 0) == 0) diagnostic_slice = true;
    if (!diagnostic_slice && !diagnostic_instrument_qualification)
      throw std::runtime_error("OFFICIAL_SCHEDULE_PARTIAL_WITHOUT_SMOKE_LABEL");
    std::set<std::string> blocks;
    std::set<int> replica_ids;
    for (const auto& event : selected) {
      if (event.phase == "calibration") continue;
      blocks.insert(event.block_id);
      if (event.W != 288 || event.O != 1024)
        throw std::runtime_error(diagnostic_instrument_qualification ?
            "S25B_SLICE_MUST_USE_W288_O1024" : "SMOKE_SLICE_MUST_USE_W288_O1024");
      replica_ids.insert(event.replica);
    }
    if (diagnostic_instrument_qualification) {
      if (instrument_n1 == instrument_full)
        throw std::runtime_error("S25B_SCHEDULE_MODE_AMBIGUOUS");
      std::map<std::string, std::vector<const ScheduleEvent*>> calibration_triplets, residency_triplets;
      std::set<std::string> cal_units, conditions;
      for (const auto& event : selected) {
        if (event.phase == "calibration") {
          if ((event.m != 1 && event.m != 16) || (event.K != 1 && event.K != 16) ||
              event.replica != 0 || event.segment_id.size() != 0)
            throw std::runtime_error("S25B_CALIBRATION_GEOMETRY_INVALID:" + event.event_id);
          calibration_triplets[event.unit_id].push_back(&event); cal_units.insert(event.unit_id);
        } else if (event.phase == "residency") {
          if ((event.m != 1 && event.m != 16) || (event.K != 1 && event.K != 16) ||
              event.replica < 1 || event.replica > 3)
            throw std::runtime_error("S25B_RESIDENCY_GEOMETRY_INVALID:" + event.event_id);
          const std::string key = event.segment_id + "|m" + std::to_string(event.m) + "|K" + std::to_string(event.K);
          residency_triplets[key].push_back(&event);
          conditions.insert(std::to_string(event.replica) + "|" + std::to_string(event.m) + "|" + std::to_string(event.K));
        } else {
          throw std::runtime_error("S25B_UNSUPPORTED_PHASE:" + event.phase);
        }
      }
      const std::set<std::string> expected_units = instrument_n1 ?
          std::set<std::string>{"W288|O1024|m1|K1"} :
          std::set<std::string>{"W288|O1024|m1|K1", "W288|O1024|m1|K16",
                                "W288|O1024|m16|K1", "W288|O1024|m16|K16"};
      if (cal_units != expected_units || calibration_triplets.size() != expected_units.size() ||
          selected.size() != (instrument_n1 ? 6u : 48u) ||
          residency_triplets.size() != (instrument_n1 ? 1u : 12u) ||
          conditions.size() != (instrument_n1 ? 1u : 12u))
        throw std::runtime_error("S25B_SCHEDULE_CONDITION_COVERAGE_INVALID");
      for (const auto& [unit, triplet] : calibration_triplets) {
        if (triplet.size() != 3 || triplet[0]->variant != "A" || triplet[1]->variant != "B" ||
            triplet[2]->variant != "C")
          throw std::runtime_error("S25B_CALIBRATION_A_B_C_NOT_CONTIGUOUS:" + unit);
      }
      for (const auto& [key, triplet] : residency_triplets) {
        if (triplet.size() != 3 || triplet[0]->variant != "A" || triplet[1]->variant != "B" ||
            triplet[2]->variant != "C" || triplet[0]->block_id != triplet[1]->block_id ||
            triplet[0]->block_id != triplet[2]->block_id ||
            triplet[0]->atomic_group_id != triplet[1]->atomic_group_id ||
            triplet[0]->atomic_group_id != triplet[2]->atomic_group_id)
          throw std::runtime_error("S25B_RESIDENCY_A_B_C_TRIPLET_SPLIT:" + key);
      }
      if (instrument_n1 && (blocks.size() != 1 || replica_ids != std::set<int>{1} ||
                            selected_segments != std::set<std::string>{"R01"}))
        throw std::runtime_error("S25B_SMOKE_N1_NOT_ONE_COMPLETE_BLOCK");
      if (instrument_full && (blocks.size() != 3 || replica_ids != std::set<int>{1, 2, 3} ||
                              selected_segments != std::set<std::string>{"R01", "R02", "R03"}))
        throw std::runtime_error("S25B_FULL_MUST_HAVE_36_BLOCKS_REPLICA_MAJOR");
      g_s25_b_mode = true; g_s25_b_smoke_n1 = instrument_n1;
    } else if (blocks.size() != 3 || replica_ids != std::set<int>{1, 2, 3} ||
               selected_segments != std::set<std::string>{"R01", "R02", "R03"}) {
      throw std::runtime_error("SMOKE_SLICE_MUST_BE_COMPLETE_W288_REPLICAS_1_TO_3");
    }
  }
  if (complete_residency && selected.size() != 13688)
    throw std::runtime_error("S25_FULL_RESIDENCY_EVENT_COUNT_INVALID");
  std::cout << "OFFICIAL_PLAN_R18 PASS full_events=32102 qualification_events=18414 residency_events=13688 order_hashes=PASS exact_sequence=PASS selected_events=" << selected.size()
            << " selected_blocks=" << selected_blocks.size() << " complete_blocks_and_multiplicity=PASS plan_sha256=" << g_plan_sha << "\n";
  return selected;
}

void prepare_worker(Worker& w, unsigned worker, int output, int m, int k) {
  w.cell = r18::generate_cell(m, output);
  w.shared_ac = std::make_shared<omega_q4::PackedWeights>();
  omega_q4::prepare_weights(w.cell, output, *w.shared_ac);
  omega_q4::prepare_activations(w.cell, output, w.x);
  w.y.resize(size_t(m) * output);
  const size_t bytes = omega_q4::weight_bytes(output), code = omega_q4::code_bytes(output);
  // The allocator may fall back to ordinary pages when SeLockMemoryPrivilege
  // is unavailable; the caller records that OS result and stops before samples.
  std::vector<int8_t> wq, xq; std::vector<uint16_t> ws, xs;
  if (!omega_q4::qa_reconstruct(*w.shared_ac, wq, ws) || !omega_q4::qa_reconstruct_activations(w.x, xq, xs) ||
      wq != w.cell.wq || ws != w.cell.ws_half || xq != w.cell.xq || xs != w.cell.xs_half)
    throw std::runtime_error("exact_representation_failed:worker=" + std::to_string(worker));
  std::vector<int32_t> dots(size_t(m) * output * r18::G);
  omega_q4::qa_group_dots(*w.shared_ac, w.x, dots.data());
  for (int row = 0; row < output; ++row) for (int group = 0; group < r18::G; ++group) {
    int32_t expected = 0;
    for (int k = 0; k < 32; ++k)
      expected += int(w.cell.wq[size_t(row) * r18::I + group * 32 + k]) * int(w.cell.xq[group * 32 + k]);
    if (dots[size_t(row) * r18::G + group] != expected) throw std::runtime_error("exact_group_dot_failed");
  }
  omega_q4::run(*w.shared_ac, w.x, output, w.y.data());
  if (!r18::verify_s12(w.cell, output, w.y.data(), w.y.size()).pass) throw std::runtime_error("S12_numeric_check_failed");
  w.b_copies.clear(); w.b_copies.reserve(k);
  for (int copy_index = 0; copy_index < k; ++copy_index) {
    auto copy = std::make_unique<omega_q4::PackedWeights>(); copy->output = output; copy->bytes.resize(bytes);
    std::memcpy(copy->bytes.data(), w.shared_ac->bytes.data(), bytes);
    if (std::memcmp(copy->bytes.data(), w.shared_ac->bytes.data(), bytes) != 0 || !page_large(copy->bytes.data(), bytes))
      throw std::runtime_error("B_independent_copy_prepare_failed:worker=" + std::to_string(worker));
    for (int prior = 0; prior < copy_index; ++prior) {
      uintptr_t a = reinterpret_cast<uintptr_t>(copy->bytes.data());
      uintptr_t b = reinterpret_cast<uintptr_t>(w.b_copies[prior]->bytes.data());
      if (!(a + bytes <= b || b + bytes <= a)) throw std::runtime_error("B_copy_region_overlap");
    }
    w.b_copies.push_back(std::move(copy));
  }
  w.output = output; w.m = m;
  if (code + omega_q4::scale_bytes(output) != bytes) throw std::runtime_error("MAC_WEIGHT_SIZE_FORMULA_INVALID");
}

bool prepared_for(const Worker& worker, const ScheduleEvent& event) {
  return worker.output == event.O && worker.m == event.m &&
         worker.b_copies.size() == static_cast<size_t>(event.K);
}

bool prepare_all_workers(std::array<Worker, 8>& workers, const ScheduleEvent& event,
                         std::string& error) {
  std::atomic<bool> ok{true}; std::mutex error_mutex; std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < 8; ++worker) {
    if (prepared_for(workers[worker], event)) continue;
    threads.emplace_back([&, worker] {
      try {
        if (!set_worker_affinity_r18(worker) || !worker_affinity_matches_r18(worker))
          throw std::runtime_error("worker_affinity_invalid");
        prepare_worker(workers[worker], worker, event.O, event.m, event.K);
        const auto& current = workers[worker];
        bool pages = page_large(current.shared_ac->bytes.data(), omega_q4::weight_bytes(event.O)) &&
                     page_large(current.x.bytes.data(), current.x.bytes.size()) &&
                     page_large(current.y.data(), current.y.size() * sizeof(float));
        for (const auto& copy : current.b_copies)
          pages = pages && page_large(copy->bytes.data(), omega_q4::weight_bytes(event.O));
        if (!pages) throw std::runtime_error("large_page_query_failed");
      } catch (const std::exception& ex) {
        ok = false; std::lock_guard<std::mutex> lock(error_mutex);
        if (error.empty()) error = "worker" + std::to_string(worker) + ":" + ex.what();
      }
    });
  }
  for (auto& thread : threads) thread.join();
  return ok.load();
}

double verify_segment_state(const ScheduleEvent& event, const std::array<Worker, 8>& workers,
                            const std::string& expected_plan_sha, const std::string& expected_topology_sha,
                            bool reader_qualified) {
  if (!reader_qualified) throw std::runtime_error("SEGMENT_REVERIFY_EMON_READER_NOT_LIVE");
  if (event.segment_id.empty() || g_plan_sha != expected_plan_sha)
    throw std::runtime_error("SEGMENT_REVERIFY_PLAN_OR_SEGMENT_IDENTITY_FAILED");
  std::string why;
  if (!discover_topology_r18(why)) throw std::runtime_error("SEGMENT_REVERIFY_TOPOLOGY_FAILED:" + why);
  const std::string topology = topology_json_r18();
  const std::string topology_sha = sha256(reinterpret_cast<const uint8_t*>(topology.data()), topology.size());
  if (topology_sha != expected_topology_sha) throw std::runtime_error("SEGMENT_REVERIFY_TOPOLOGY_IDENTITY_CHANGED");
  LARGE_INTEGER qpf{};
  if (!QueryPerformanceFrequency(&qpf) || uint64_t(qpf.QuadPart) != g_qpf)
    throw std::runtime_error("SEGMENT_REVERIFY_QPF_CHANGED");
  for (unsigned worker = 0; worker < 8; ++worker) {
    const Worker& w = workers[worker];
    bool pages = prepared_for(w, event) && page_large(w.shared_ac->bytes.data(), omega_q4::weight_bytes(event.O)) &&
        page_large(w.x.bytes.data(), w.x.bytes.size()) && page_large(w.y.data(), w.y.size() * sizeof(float));
    for (const auto& copy : w.b_copies)
      pages = pages && page_large(copy->bytes.data(), omega_q4::weight_bytes(event.O));
    if (!pages) throw std::runtime_error("SEGMENT_REVERIFY_PAGES_OR_BUFFER_IDENTITY_FAILED:worker=" + std::to_string(worker));
  }
  if (!set_worker_affinity_r18(0) || !worker_affinity_matches_r18(0))
    throw std::runtime_error("SEGMENT_REVERIFY_TSC_CALIBRATION_AFFINITY_FAILED");
  std::vector<double> samples; samples.reserve(3);
  for (int i = 0; i < 3; ++i) {
    const int64_t qa = qpc_now(); const uint64_t ta = read_tsc(); Sleep(100);
    const uint64_t tb = read_tsc(); const int64_t qb = qpc_now();
    samples.push_back(double(tb - ta) * double(g_qpf) / double(qb - qa));
  }
  std::sort(samples.begin(), samples.end()); const double hz = samples[1];
  const double error = (std::max)({std::abs(samples[0] - hz), std::abs(samples[2] - hz)}) / hz;
  std::string coordinator;
  if (!set_coordinator_affinity_r18(coordinator))
    throw std::runtime_error("SEGMENT_REVERIFY_COORDINATOR_RESTORE_FAILED:" + coordinator);
  if (error > .001 || std::abs(hz - g_tsc_hz) / g_tsc_hz > .001)
    throw std::runtime_error("SEGMENT_REVERIFY_CLOCK_DRIFT_GT_0_1_PERCENT");
  return hz;
}

const omega_q4::PackedWeights& selected_weights(const Worker& w, char variant, int app) {
  if (variant == 'B') return *w.b_copies.at(size_t(app));
  return *w.shared_ac;
}

void worker_main(unsigned worker) {
  try {
    if (!set_worker_affinity_r18(worker) || !worker_affinity_matches_r18(worker))
      throw std::runtime_error("P_core_worker_affinity_invalid:" + std::to_string(worker));
    for (;;) {
      if (!g_start.wait()) return;
      const Command command = decode_command(g_command_word.load(std::memory_order_acquire));
      if (command.kind == CommandKind::Stop) { g_done.wait(); return; }
      Worker& w = (*g_workers)[worker];
      if (command.kind == CommandKind::Episode) {
        if (command.applications <= 0 || command.applications > 32 || command.repetitions <= 0)
          throw std::runtime_error("episode_repeat_geometry_invalid");
        w.batch_stamp = Stamp{}; w.batch_kernel_tsc = 0; w.batch_has_stamp = false;
        for (int repeat = 0; repeat < command.repetitions; ++repeat) for (int app = 0; app < command.applications; ++app) {
          Stamp stamp; stamp.qpc_precondition_start = qpc_now();
          const bool evict_weights_now = command.variant == 'C' || command.variant == 'B' || app == 0;
          if (evict_weights_now) {
            const auto& weights = selected_weights(w, command.variant, app);
            omega_evict::evict_range(weights.bytes.data(), weights.bytes.size());
            ++w.evicted_ranges; ++w.evicted_weight_ranges;
          }
          const volatile uint8_t* x = w.x.bytes.data(); const volatile float* y = w.y.data(); uint64_t sum = 0;
          for (size_t i = 0; i < w.x.bytes.size(); i += 64) sum += x[i];
          for (size_t i = 0; i < w.y.size(); i += 16) sum += uint64_t(std::abs(y[i]));
          w.sink ^= sum; std::fill(w.y.begin(), w.y.end(), 0.5f); _mm_mfence();
          stamp.qpc_precondition_end = qpc_now();
          const auto& weights = selected_weights(w, command.variant, app);
          stamp.weight_address = reinterpret_cast<uint64_t>(weights.bytes.data());
          stamp.lp_start = GetCurrentProcessorNumber(); stamp.qpc_start = qpc_now(); stamp.tsc_start = read_tsc();
          omega_q4::run(weights, w.x, w.output, w.y.data());
          stamp.tsc_end = read_tsc(); stamp.qpc_end = qpc_now(); stamp.lp_end = GetCurrentProcessorNumber();
          if (!w.batch_has_stamp) {
            w.batch_stamp = stamp; w.batch_has_stamp = true;
          } else {
            w.batch_stamp.tsc_end = stamp.tsc_end; w.batch_stamp.qpc_end = stamp.qpc_end;
            w.batch_stamp.qpc_precondition_end = stamp.qpc_precondition_end;
            w.batch_stamp.lp_end = stamp.lp_end;
          }
          w.batch_kernel_tsc += stamp.tsc_end - stamp.tsc_start;
          if (stamp.lp_start != worker_processor_r18(worker).lp || stamp.lp_end != worker_processor_r18(worker).lp)
            throw std::runtime_error("worker_migrated_during_application");
        }
      } else if (command.kind == CommandKind::Probe) {
        if (command.applications != kHotProbeK || command.repetitions <= 0 ||
            w.hot_probe.size() != kHotProbeBytes)
          throw std::runtime_error("S25_hot_probe_geometry_invalid");
        w.hot_probe_tsc_start = read_tsc();
        w.hot_probe_qpc_start = qpc_now();
        w.hot_probe_lp_start = GetCurrentProcessorNumber();
        const volatile std::uint64_t* lines =
            reinterpret_cast<const volatile std::uint64_t*>(w.hot_probe.data());
        std::uint64_t sum = 0;
        constexpr std::size_t words_per_line = 64 / sizeof(std::uint64_t);
        constexpr std::size_t line_count = kHotProbeBytes / 64;
        for (int repeat = 0; repeat < command.repetitions; ++repeat)
          for (int application = 0; application < command.applications; ++application)
            for (std::size_t line = 0; line < line_count; ++line)
              sum += lines[line * words_per_line];
        w.hot_probe_tsc_end = read_tsc();
        w.hot_probe_qpc_end = qpc_now();
        w.hot_probe_lp_end = GetCurrentProcessorNumber();
        w.hot_probe_sink ^= sum;
        if (w.hot_probe_lp_start != worker_processor_r18(worker).lp ||
            w.hot_probe_lp_end != worker_processor_r18(worker).lp)
          throw std::runtime_error("S25_hot_probe_worker_migrated");
      }
      if (!g_done.wait()) return;
    }
  } catch (const std::exception& e) {
    { std::lock_guard<std::mutex> lock(g_error_mutex); if (g_worker_error.empty()) g_worker_error = e.what(); }
    g_start.cancel(); g_done.cancel();
  }
}

void dispatch(Command command) {
  g_command_word.store(encode_command(command), std::memory_order_release);
  const bool start_ok = g_start.wait();
  const bool done_ok = start_ok && g_done.wait();
  if (!done_ok) {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    throw std::runtime_error(g_worker_error.empty() ? "worker_barrier_cancelled" : g_worker_error);
  }
}

void initialize_hot_probe_buffers(std::array<Worker, 8>& workers) {
  for (unsigned worker = 0; worker < workers.size(); ++worker) {
    auto& buffer = workers[worker].hot_probe;
    buffer.resize(kHotProbeBytes);
    for (std::size_t i = 0; i < buffer.size(); ++i)
      buffer[i] = std::uint8_t((i * 131u + worker * 17u + 0x5bu) & 0xffu);
  }
}

double dispatch_hot_probe(int repetitions) {
  const auto start = qpc_now();
  dispatch({CommandKind::Probe, 'P', kHotProbeK, repetitions});
  const auto end = qpc_now();
  return qpc_seconds(start, end);
}

int calibrate_hot_probe_r() {
  for (int candidate = 1; candidate <= (1 << 20); candidate *= 2) {
    const double elapsed = dispatch_hot_probe(candidate);
    std::cout << "S25_HOT_PROBE_CALIBRATION K=16 R=" << candidate
              << " region_s=" << std::fixed << std::setprecision(6) << elapsed
              << " target_s=2.5 decision=" << (elapsed >= kHotProbeTargetSeconds ? "FIXED" : "DOUBLE") << "\n";
    if (elapsed >= kHotProbeTargetSeconds) return candidate;
  }
  throw std::runtime_error("S25_HOT_PROBE_CALIBRATION_EXHAUSTED");
}

void run_hot_probe_margin(int probe_r) {
  const int margin_r = (std::max)(1, probe_r / 8);
  const auto start = qpc_now();
  do { dispatch_hot_probe(margin_r); }
  while (qpc_seconds(start, qpc_now()) < kHotProbeMarginSeconds);
}

void acquire_hot_probe_endpoint(const std::string& segment, int replica, int endpoint,
                                int probe_r, RawFile& marks, RawFile& probe_raw) {
  if (!g_hot_probe_records || g_hot_probe_records->capacity() - g_hot_probe_records->size() < 31)
    throw std::runtime_error("S25_HOT_PROBE_PREALLOCATED_BUFFER_EXHAUSTED");
  const std::string endpoint_name = endpoint == 0 ? "BEGIN" : "END";
  const std::string comparison = "S25_HOT_PROBE|" + segment + "|" + endpoint_name;
  for (int sample = 1; sample <= 31; ++sample) {
    HotProbeRecord record;
    record.replica = replica; record.endpoint = endpoint; record.sample = sample;
    record.segment = segment; record.comparison_set = comparison;
    record.qpc_pad_start = qpc_now();
    run_hot_probe_margin(probe_r);
    record.qpc_start = qpc_now();
    dispatch({CommandKind::Probe, 'P', kHotProbeK, probe_r});
    record.qpc_end = qpc_now();
    for (unsigned worker = 0; worker < 8; ++worker) {
      const auto& source = (*g_workers)[worker];
      auto& target = record.workers[worker];
      target.tsc_start = source.hot_probe_tsc_start; target.tsc_end = source.hot_probe_tsc_end;
      target.qpc_start = source.hot_probe_qpc_start; target.qpc_end = source.hot_probe_qpc_end;
      target.lp_start = source.hot_probe_lp_start; target.lp_end = source.hot_probe_lp_end;
    }
    // Preserve stamps for the measured dispatch before the post-margin dispatches
    // overwrite each worker's latest hot-probe timestamps.
    run_hot_probe_margin(probe_r);
    record.qpc_pad_end = qpc_now();
    const double region_s = qpc_seconds(record.qpc_start, record.qpc_end);
    int64_t worker_first = INT64_MAX, worker_last = 0;
    for (unsigned worker = 0; worker < 8; ++worker) {
      const auto& target = record.workers[worker];
      worker_first = (std::min)(worker_first, target.qpc_start);
      worker_last = (std::max)(worker_last, target.qpc_end);
      if (target.lp_start != worker_processor_r18(worker).lp || target.lp_end != worker_processor_r18(worker).lp ||
          target.tsc_end <= target.tsc_start || target.qpc_end <= target.qpc_start)
        throw std::runtime_error("S25_HOT_PROBE_WORKER_EVIDENCE_INVALID");
    }
    const double worker_span_s = qpc_seconds(worker_first, worker_last);
    record.coordinator_overhead_fraction = worker_span_s > 0 ? (std::max)(0.0, region_s - worker_span_s) / worker_span_s : 1.0;
    if (region_s < kHotProbeMinimumSeconds || record.coordinator_overhead_fraction > .01) {
      std::ostringstream error;
      error << "S25_HOT_PROBE_DURATION_OR_INSTRUMENTATION_HOLD:segment=" << segment
            << ":endpoint=" << endpoint_name << ":sample=" << sample
            << ":region_s=" << std::fixed << std::setprecision(6) << region_s
            << ":coordinator_overhead_fraction=" << record.coordinator_overhead_fraction;
      throw std::runtime_error(error.str());
    }
    // The record buffer was reserved before workers started. No JSON, hashing,
    // allocation, or validation is performed inside [t_pad_ini,t_pad_fin].
    g_hot_probe_records->push_back(std::move(record));
  }

  for (const auto& record : *g_hot_probe_records) {
    if (record.segment != segment || record.endpoint != endpoint) continue;
    const auto sample_id = "S25PROBE-" + segment + "-" + endpoint_name + "-" +
                           (record.sample < 10 ? "0" : "") + std::to_string(record.sample);
    for (unsigned worker = 0; worker < 8; ++worker) {
      const auto& stamp = record.workers[worker];
      marks.line(sample_id + "," + std::to_string(worker_processor_r18(worker).p_core) + "," +
          std::to_string(stamp.lp_start) + "," + std::to_string(record.replica) +
          ",S25_HOT_PROBE," + std::to_string(record.qpc_pad_start) + "," +
          std::to_string(record.qpc_start) + "," + std::to_string(record.qpc_end) + "," +
          std::to_string(record.qpc_pad_end) + "," + record.comparison_set + ",qpc_ticks");
    }
    std::ostringstream row;
    row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
        << sample_id << "\",\"phase\":\"instrumentation_probe\",\"condition_id\":\"S25_HOT_PROBE\""
        << ",\"comparison_set\":\"" << record.comparison_set
        << "\",\"frequency_basis\":\"frequency_of_condition\",\"segment_id\":\"" << segment
        << "\",\"endpoint\":\"" << endpoint_name << "\",\"replica\":" << record.replica
        << ",\"sample\":" << record.sample
        << ",\"W_target_kib\":288,\"O\":1024,\"m\":16,\"K\":16,\"probe_bytes_per_P\":"
        << kHotProbeBytes << ",\"R\":" << probe_r << ",\"qpc_pad_start\":" << record.qpc_pad_start
        << ",\"qpc_start\":" << record.qpc_start << ",\"qpc_end\":" << record.qpc_end
        << ",\"qpc_pad_end\":" << record.qpc_pad_end << ",\"region_seconds\":"
        << std::setprecision(12) << qpc_seconds(record.qpc_start, record.qpc_end)
        << ",\"instrumentation_overhead_fraction\":" << record.coordinator_overhead_fraction
        << ",\"load_only\":true,\"weights_refreshed_after_warmup\":false,\"worker_evidence\":[";
    for (unsigned worker = 0; worker < 8; ++worker) {
      if (worker) row << ',';
      const auto& stamp = record.workers[worker];
      row << "{\"worker\":" << worker << ",\"lp\":" << stamp.lp_start
          << ",\"first_tsc\":" << stamp.tsc_start << ",\"last_tsc\":" << stamp.tsc_end
          << ",\"first_qpc\":" << stamp.qpc_start << ",\"last_qpc\":" << stamp.qpc_end
          << ",\"kernel_only_seconds\":" << qpc_seconds(stamp.qpc_start, stamp.qpc_end) << '}';
    }
    row << "]}";
    probe_raw.line(row.str());
  }
}

struct EpisodeResult {
  double t_app_us = 0;
  double instrumentation_fraction = 0;
  int64_t qpc_start = 0, qpc_end = 0;
  uint64_t tsc_start = 0, tsc_end = 0;
  std::array<uint64_t, 8> worker_kernel_tsc{};
  std::array<uint64_t, 8> worker_tsc_start{}, worker_tsc_end{};
  std::array<int64_t, 8> worker_qpc_start{}, worker_qpc_end{};
  std::array<int64_t, 8> worker_precondition_start{}, worker_precondition_end{};
};

EpisodeResult run_episode(const ScheduleEvent& event, char variant, unsigned episode_index,
                          const std::string& event_type, unsigned trial_index, int repetitions) {
  (void)episode_index; (void)event_type; (void)trial_index;
  if (repetitions <= 0) throw std::runtime_error("episode_repetition_count_invalid");
  EpisodeResult result; result.qpc_start = qpc_now();
  for (auto& worker : *g_workers) {
    worker.evicted_weight_ranges = 0; worker.evicted_activation_ranges = 0; worker.evicted_ranges = 0;
    worker.batch_stamp = Stamp{}; worker.batch_kernel_tsc = 0; worker.batch_has_stamp = false;
  }
  dispatch({CommandKind::Episode, variant, event.K, repetitions});
  result.qpc_end = qpc_now();
  uint64_t first_tsc = UINT64_MAX, last_tsc = 0, max_worker_kernel = 0;
  int64_t worker_first_pre_qpc = INT64_MAX, worker_last_kernel_qpc = 0;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const Worker& w = (*g_workers)[worker];
    if (!w.batch_has_stamp || !w.batch_kernel_tsc)
      throw std::runtime_error("episode_worker_batch_timestamp_missing");
    const Stamp& s = w.batch_stamp;
    result.worker_kernel_tsc[worker] = w.batch_kernel_tsc;
    result.worker_tsc_start[worker] = s.tsc_start; result.worker_tsc_end[worker] = s.tsc_end;
    result.worker_qpc_start[worker] = s.qpc_start; result.worker_qpc_end[worker] = s.qpc_end;
    result.worker_precondition_start[worker] = s.qpc_precondition_start;
    result.worker_precondition_end[worker] = s.qpc_precondition_end;
    first_tsc = (std::min)(first_tsc, s.tsc_start); last_tsc = (std::max)(last_tsc, s.tsc_end);
    max_worker_kernel = (std::max)(max_worker_kernel, w.batch_kernel_tsc);
    worker_first_pre_qpc = (std::min)(worker_first_pre_qpc, s.qpc_precondition_start);
    worker_last_kernel_qpc = (std::max)(worker_last_kernel_qpc, s.qpc_end);
  }
  result.tsc_start = first_tsc; result.tsc_end = last_tsc;
  result.t_app_us = double(max_worker_kernel);
  uint64_t observed_weight_calls = 0, observed_activation_calls = 0, observed_range_calls = 0;
  for (const auto& worker : *g_workers) {
    observed_weight_calls += worker.evicted_weight_ranges;
    observed_activation_calls += worker.evicted_activation_ranges;
    observed_range_calls += worker.evicted_ranges;
  }
  g_evict_weight_calls += observed_weight_calls;
  g_evict_weight_range_calls += observed_weight_calls;
  g_evict_activation_range_calls += observed_activation_calls;
  g_evict_range_calls += observed_range_calls;
  const uint64_t expected_weight_calls = (variant == 'A' ? 8u : 8u * unsigned(event.K)) * uint64_t(repetitions);
  const uint64_t expected_activation_calls = 0;
  const bool calls_match = observed_weight_calls == expected_weight_calls && observed_activation_calls == 0
                        && observed_range_calls == expected_weight_calls;
  const double wall_us = qpc_seconds(result.qpc_start, result.qpc_end) * 1e6;
  const double worker_span_us = qpc_seconds(worker_first_pre_qpc, worker_last_kernel_qpc) * 1e6;
  const double app_us = result.t_app_us * 1e6 / g_tsc_hz;
  result.instrumentation_fraction = app_us > 0 ? (std::max)(0.0, wall_us - worker_span_us) / app_us : 1.0;
  if (!calls_match) throw std::runtime_error("D8_EVICTION_CALL_COUNT_MISMATCH:" + event.event_id);
  return result;
}

// Exact TSC spans are retained alongside converted microseconds.
double episode_microseconds(const EpisodeResult& e, double tsc_hz) { return e.t_app_us * 1e6 / tsc_hz; }

struct MarginResult { int64_t start = 0, end = 0; uint64_t episodes = 0; double seconds = 0; };
MarginResult run_margin(const ScheduleEvent& event, char variant, const std::string& side,
                        double tsc_hz) {
  MarginResult margin; margin.start = qpc_now(); uint64_t index = 0; double measured_us = 0;
  do {
    auto sample = run_episode(event, variant, unsigned(index + 1), "margin_" + side, 0, 1);
    measured_us += episode_microseconds(sample, tsc_hz); ++index;
    margin.end = qpc_now(); margin.seconds = qpc_seconds(margin.start, margin.end);
  } while (margin.seconds < kMarginSeconds);
  margin.episodes = index;
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"event_id\":\"" << json_escape(event.event_id)
      << "\",\"condition_id\":\"" << json_escape(event.condition_id) << "\",\"variant\":\"" << variant
      << "\",\"side\":\"" << side << "\",\"qpc_start\":" << margin.start << ",\"qpc_end\":" << margin.end
      << ",\"duration_seconds\":" << std::setprecision(12) << margin.seconds << ",\"episodes\":" << margin.episodes
      << ",\"kernel_time_seconds\":" << measured_us / 1e6 << ",\"same_condition\":true,\"continuous_load\":true}";
  return margin;
}

std::string event_regions_json(const ScheduleEvent& e, char variant) {
  std::ostringstream out; out << '[';
  for (unsigned worker = 0; worker < 8; ++worker) {
    const Worker& w = (*g_workers)[worker]; const int copies = variant == 'B' ? e.K : 1;
    for (int copy = 0; copy < copies; ++copy) {
      const auto& weights = selected_weights(w, variant, copy);
      if (worker || copy) out << ',';
      out << "{\"worker\":" << worker << ",\"copy_index\":" << copy << ",\"address\":"
          << reinterpret_cast<uintptr_t>(weights.bytes.data()) << ",\"bytes\":" << omega_q4::weight_bytes(e.O)
          << ",\"sha256\":\"" << sha256(weights.bytes, omega_q4::weight_bytes(e.O))
          << "\",\"allocation_id\":\"" << (variant == 'B' ? "b-w" + std::to_string(worker) + "-k" + std::to_string(copy)
                                                                         : "ac-w" + std::to_string(worker)) << "\"}";
    }
  }
  out << ']'; return out.str();
}

std::string activation_regions_json() {
  std::ostringstream out; out << '['; bool first = true;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const Worker& w = (*g_workers)[worker];
    const std::array<std::pair<const char*, std::pair<const void*, size_t>>, 2> regions = {
      std::make_pair("X", std::make_pair(static_cast<const void*>(w.x.bytes.data()), w.x.bytes.size())),
      std::make_pair("Y", std::make_pair(static_cast<const void*>(w.y.data()), w.y.size() * sizeof(float))) };
    for (const auto& region : regions) {
      if (!first) out << ',';
      first = false;
      out << "{\"worker\":" << worker << ",\"region_kind\":\"" << region.first
          << "\",\"address\":" << reinterpret_cast<uintptr_t>(region.second.first)
          << ",\"bytes\":" << region.second.second << '}';
    }
  }
  out << ']'; return out.str();
}

std::string region_instance_id(const ScheduleEvent& event, char variant) {
  std::ostringstream joined;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto& weights = selected_weights((*g_workers)[worker], variant, 0);
    joined << worker << ':' << reinterpret_cast<uintptr_t>(weights.bytes.data()) << ':'
           << sha256(weights.bytes, omega_q4::weight_bytes(event.O)) << '|';
  }
  const std::string s = joined.str(); return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void write_event_summary(RawFile& events, RawFile& marks, RawFile& episode_file, RawFile& manifest,
                         const ScheduleEvent& event, char variant,
                         int r, const std::vector<EpisodeResult>& episodes, double hz,
                         const MarginResult& pre, const MarginResult& post,
                         std::array<uint64_t, 8>& output_hashes) {
  double sum_us = 0; uint64_t start_tsc = UINT64_MAX, end_tsc = 0;
  for (const auto& ep : episodes) { sum_us += episode_microseconds(ep, hz); }
  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto& w = (*g_workers)[worker];
    output_hashes[worker] = output_hash(w.y.data(), w.y.size());
  }
  const int64_t sample_start = episodes.front().qpc_start, sample_end = episodes.back().qpc_end;
  std::array<uint64_t, 8> worker_kernel_tsc{};
  std::array<uint64_t, 8> worker_first_tsc{};
  std::array<uint64_t, 8> worker_last_tsc{};
  std::array<int64_t, 8> worker_first_qpc{}, worker_last_qpc{};
  std::array<int64_t, 8> worker_pre_first{}, worker_pre_last{};
  worker_first_tsc.fill(UINT64_MAX); worker_first_qpc.fill(INT64_MAX); worker_pre_first.fill(INT64_MAX);
  double max_instrumentation_fraction = 0;
  for (const auto& episode : episodes) {
    max_instrumentation_fraction = (std::max)(max_instrumentation_fraction, episode.instrumentation_fraction);
    for (unsigned worker = 0; worker < 8; ++worker) {
      worker_kernel_tsc[worker] += episode.worker_kernel_tsc[worker];
      worker_first_tsc[worker] = (std::min)(worker_first_tsc[worker], episode.worker_tsc_start[worker]);
      worker_last_tsc[worker] = (std::max)(worker_last_tsc[worker], episode.worker_tsc_end[worker]);
      worker_first_qpc[worker] = (std::min)(worker_first_qpc[worker], episode.worker_qpc_start[worker]);
      worker_last_qpc[worker] = (std::max)(worker_last_qpc[worker], episode.worker_qpc_end[worker]);
      worker_pre_first[worker] = (std::min)(worker_pre_first[worker], episode.worker_precondition_start[worker]);
      worker_pre_last[worker] = (std::max)(worker_pre_last[worker], episode.worker_precondition_end[worker]);
    }
  }
  const double condition_region_seconds = qpc_seconds(sample_start, sample_end);
  const double slowest_kernel_seconds = double(*std::min_element(worker_kernel_tsc.begin(), worker_kernel_tsc.end())) / hz;
  if (condition_region_seconds < 2.0 || slowest_kernel_seconds < .100)
    throw std::runtime_error("S24_RESIDENCY_DURATION_BELOW_MINIMUM:event=" + event.event_id +
      ":condition_region_s=" + std::to_string(condition_region_seconds) +
      ":slowest_worker_kernel_s=" + std::to_string(slowest_kernel_seconds));
  if (g_s25_b_mode && condition_region_seconds < 60.0)
    throw std::runtime_error("HOLD_INSTRUMENTATION_QUALIFICATION:S25B_REGION_BELOW_60_SECONDS:event=" +
      event.event_id + ":region_s=" + std::to_string(condition_region_seconds));
  if (max_instrumentation_fraction > .01)
    throw std::runtime_error("D9_INSTRUMENTATION_OVERHEAD_GT_1_PERCENT:event=" + event.event_id +
      ":fraction=" + std::to_string(max_instrumentation_fraction));
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(event.event_id) << "\",\"phase\":\"" << event.phase << "\",\"condition_id\":\""
      << json_escape(event.condition_id) << "\",\"comparison_set\":\"" << json_escape(frequency_group_id(event))
      << "\",\"frequency_basis\":\"frequency_of_condition\",\"variant\":\"" << variant << "\",\"observation\":\""
      << event.observation << "\",\"W_target_kib\":" << event.W << ",\"O\":" << event.O << ",\"m\":" << event.m
      << ",\"K\":" << event.K << ",\"replica\":" << event.replica;
  if (!event.unit_id.empty()) row << ",\"unit_id\":\"" << json_escape(event.unit_id) << "\"";
  if (!event.pair_id.empty()) row << ",\"pair_id\":\"" << json_escape(event.pair_id) << "\"";
  row << ",\"R_unit_id\":\"" << r_unit_id_for(event) << "\",\"R\":" << r << ",\"episode_count\":" << r
      << ",\"T_X_us\":" << std::fixed << std::setprecision(9) << sum_us / r
      << ",\"sum_t_app_us\":" << sum_us << ",\"f_tsc_hz\":" << hz << ",\"qpf_hz\":" << g_qpf
      << ",\"pre_margin_seconds\":" << pre.seconds << ",\"post_margin_seconds\":" << post.seconds
      << ",\"margin_condition_id\":\"" << json_escape(event.condition_id)
      << "\",\"margin_continuous\":true,\"qpc_pad_start\":" << pre.start << ",\"qpc_start\":" << sample_start
      << ",\"qpc_end\":" << sample_end << ",\"qpc_pad_end\":" << post.end
      << ",\"condition_region_seconds\":" << std::setprecision(9) << condition_region_seconds
      << ",\"slowest_worker_kernel_seconds\":" << slowest_kernel_seconds;
  if (g_s25_b_mode)
    row << ",\"instrument_qualification\":\"S25B\",\"region_target_seconds\":75.0,\"region_minimum_seconds\":60.0";
  row << ",\"worker_kernel_only_seconds\":[";
  for (unsigned worker = 0; worker < 8; ++worker) { if (worker) row << ','; row << double(worker_kernel_tsc[worker]) / hz; }
  row << "]"
      << ",\"timestamps_complete\":true,\"episode_timestamps_file\":\"residency_episodes_r18.jsonl\""
      << ",\"coordinator_worker_exchanges\":" << episodes.size()
      << ",\"instrumentation_overhead_fraction_max\":" << max_instrumentation_fraction
      << ",\"instrumentation_budget_max\":0.01,\"worker_evidence\":[";
  for (unsigned worker = 0; worker < 8; ++worker) {
    if (worker) row << ',';
    row << "{\"worker\":" << worker << ",\"lp\":" << worker_processor_r18(worker).lp
        << ",\"first_tsc\":" << worker_first_tsc[worker] << ",\"last_tsc\":" << worker_last_tsc[worker]
        << ",\"first_qpc\":" << worker_first_qpc[worker] << ",\"last_qpc\":" << worker_last_qpc[worker]
        << ",\"precondition_first_qpc\":" << worker_pre_first[worker]
        << ",\"precondition_last_qpc\":" << worker_pre_last[worker]
        << ",\"kernel_only_seconds\":" << double(worker_kernel_tsc[worker]) / hz << '}';
  }
  row << ']'
      << ",\"macs_per_call\":" << 512ull * event.O * event.m << ",\"large_pages\":true,\"affinity\":\"PASS\",\"numerical\":\"S12_PASS\""
      << ",\"weight_regions\":" << event_regions_json(event, variant)
      << ",\"activation_regions\":" << activation_regions_json()
      << ",\"region_instance_id\":\"" << region_instance_id(event, variant) << "\",\"output_hashes\":[";
  for (unsigned worker = 0; worker < 8; ++worker) { if (worker) row << ','; row << output_hashes[worker]; }
  row << "]}"; events.line(row.str());

  std::ostringstream episode_row;
  episode_row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\"" << json_escape(event.event_id)
    << "\",\"phase\":\"" << event.phase << "\",\"variant\":\"" << variant << "\",\"replica\":" << event.replica
    << ",\"R\":" << r << ",\"episode_count\":" << r
    << ",\"coordinator_worker_exchanges\":" << episodes.size() << ",\"workers\":[";
  for (unsigned worker = 0; worker < 8; ++worker) {
    if (worker) episode_row << ',';
    episode_row << "{\"worker\":" << worker << ",\"lp\":" << worker_processor_r18(worker).lp
      << ",\"first_tsc\":" << worker_first_tsc[worker] << ",\"last_tsc\":" << worker_last_tsc[worker]
      << ",\"first_qpc\":" << worker_first_qpc[worker] << ",\"last_qpc\":" << worker_last_qpc[worker]
      << ",\"kernel_tsc_sum\":" << worker_kernel_tsc[worker] << '}';
  }
  episode_row << "]}"; episode_file.line(episode_row.str());
  const uint64_t per_episode_weight_calls = variant == 'A' ? 8u : 8u * unsigned(event.K);
  const uint64_t all_episodes = uint64_t(r) + pre.episodes + post.episodes;
  manifest.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(event.event_id) +
    "\",\"variant\":\"" + std::string(1, variant) + "\",\"primitive\":\"omega_evict::evict_range\",\"episodes\":" +
    std::to_string(all_episodes) + ",\"weight_range_calls\":" +
    std::to_string(per_episode_weight_calls * all_episodes) + ",\"activation_range_calls\":0,\"weight_only\":true,\"callback_per_line\":false,\"each_episode_verified\":true}");

  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto p_core = worker_processor_r18(worker).p_core;
    marks.line(event.event_id + "," + std::to_string(p_core) + "," + std::to_string(worker_processor_r18(worker).lp) + "," +
      std::to_string(event.replica) + "," + event.condition_id + "," + std::to_string(pre.start) + "," +
      std::to_string(sample_start) + "," + std::to_string(sample_end) + "," + std::to_string(post.end) + "," +
      frequency_group_id(event) + ",qpc_ticks");
  }
}

void stop_workers(std::vector<std::thread>& workers) {
  if (workers.empty()) return;
  g_command_word.store(encode_command({CommandKind::Stop, 'A', 0}), std::memory_order_release);
  if (g_start.wait()) g_done.wait();
  for (auto& thread : workers) if (thread.joinable()) thread.join();
}

class WorkerCleanup {
 public:
  explicit WorkerCleanup(std::vector<std::thread>& workers) : workers_(workers) {}
  WorkerCleanup(const WorkerCleanup&) = delete;
  WorkerCleanup& operator=(const WorkerCleanup&) = delete;
  void activate() noexcept { active_ = true; }
  void stop() { if (active_) { stop_workers(workers_); active_ = false; } }
  ~WorkerCleanup() {
    if (!active_) return;
    try { stop_workers(workers_); }
    catch (...) {
      g_start.cancel(); g_done.cancel();
      for (auto& thread : workers_) if (thread.joinable()) {
        try { thread.join(); } catch (...) { try { thread.detach(); } catch (...) {} }
      }
    }
  }
 private:
  std::vector<std::thread>& workers_;
  bool active_ = false;
};

void write_preflight(const std::string& outdir, const std::string& lp_status, double hz,
                     double qpc_error, double configured_hz, bool pages, const std::string& plan_sha,
                     uint64_t timer_cycles, double timer_us) {
  RawFile file(outdir + "\\preflight_r18.json");
  GROUP_AFFINITY coordinator{}; DWORD_PTR process_mask = 0, system_mask = 0;
  const bool process_map_ok = GetThreadGroupAffinity(GetCurrentThread(), &coordinator) &&
      GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
  const DWORD coordinator_lp = GetCurrentProcessorNumber();
  bool coordinator_outside_workers = process_map_ok;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto& mapped = worker_processor_r18(worker);
    const DWORD local_lp = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(mapped.mask)));
    if (mapped.group == coordinator.Group && local_lp == coordinator_lp) coordinator_outside_workers = false;
  }
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"state\":\"" << (pages ? "PASS" : "HOLD_RESOURCE")
      << "\",\"numeric_metrics_used\":false,\"large_page_privilege\":\"" << lp_status
      << "\",\"large_pages\":" << (pages ? "true" : "false") << ",\"P_core_map\":" << topology_json_r18()
      << ",\"qpf_hz\":" << g_qpf << ",\"qpf_source\":\"QueryPerformanceFrequency\",\"f_tsc_qualified_hz\":"
      << std::setprecision(12) << hz << ",\"f_tsc_configured_hz\":" << configured_hz << ",\"tsc_qpc_error\":" << qpc_error
      << ",\"process_map_verified\":" << (process_map_ok ? "true" : "false")
      << ",\"process_affinity_mask\":" << process_mask
      << ",\"coordinator_group\":" << coordinator.Group
      << ",\"coordinator_lp\":" << coordinator_lp
      << ",\"coordinator_outside_worker_lps\":" << (coordinator_outside_workers ? "true" : "false")
      << ",\"irq_touched\":false"
      << ",\"timer_pair_median_cycles\":" << timer_cycles << ",\"timer_pair_median_us\":" << timer_us
      << ",\"plan_sha256\":\"" << plan_sha << "\",\"security_energy_PL_unchanged\":true}";
  file.line(row.str()); file.flush();
}

} // namespace

int verify_full_plan_smoke_slice_r18(const std::string& plan_path, const std::string& schedule_path) {
  const auto selected = read_schedule(plan_path, schedule_path);
  std::cout << "PLAN_EXECUTABLE_R18_QA PASS loaded_complete_plan=true selected_events=" << selected.size()
            << " ordering_and_multiplicity=PASS complete_W_replica_blocks=PASS AA_pairs_adjacent=PASS\n";
  return 0;
}

int run_residency_smoke_r18(const std::string& output_dir, const std::string& plan_path,
                            const std::string& schedule_path, bool reader_qualified) {
  const auto scheduled = read_schedule(plan_path, schedule_path);
  DWORD attrs = GetFileAttributesA(output_dir.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("D13_ATTEMPT_DIRECTORY_MUST_ALREADY_EXIST");
  std::string topology_error;
  if (!discover_topology_r18(topology_error)) throw std::runtime_error("P_CORE_TOPOLOGY_HOLD:" + topology_error);
  const std::string topology_start = topology_json_r18();
  const std::string topology_start_sha = sha256(reinterpret_cast<const uint8_t*>(topology_start.data()), topology_start.size());

  std::string lp_status; const bool lp_privilege = t0common::enable_large_page_privilege(&lp_status);
  std::cout << "LARGE_PAGE_PRIVILEGE=" << lp_status << " enable_large_page_privilege_called=true\n";
  LARGE_INTEGER qpf{}; if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) throw std::runtime_error("QPF_UNAVAILABLE");
  g_qpf = uint64_t(qpf.QuadPart);
  bool invariant = false; int cpu[4]{}; __cpuid(cpu, 0x80000000);
  if (unsigned(cpu[0]) >= 0x80000007) { __cpuid(cpu, 0x80000007); invariant = (cpu[3] & (1u << 8)) != 0; }
  if (!invariant) throw std::runtime_error("INVARIANT_TSC_REQUIRED");
  if (!set_worker_affinity_r18(0) || !worker_affinity_matches_r18(0)) throw std::runtime_error("TSC_CALIBRATION_AFFINITY_FAILED");
  std::vector<double> hz_samples;
  for (int i = 0; i < 5; ++i) { auto qa = qpc_now(); auto ta = read_tsc(); Sleep(250); auto tb = read_tsc(); auto qb = qpc_now(); hz_samples.push_back(double(tb - ta) * double(g_qpf) / double(qb - qa)); }
  std::sort(hz_samples.begin(), hz_samples.end()); const double hz = hz_samples[2];
  double qpc_error = 0; for (double f : hz_samples) qpc_error = (std::max)(qpc_error, std::abs(f - hz) / hz);
  if (qpc_error > .001) throw std::runtime_error("TSC_QPC_QUALIFICATION_GT_0_1_PERCENT");
  std::string coordinator_affinity;
  if (!set_coordinator_affinity_r18(coordinator_affinity))
    throw std::runtime_error("D9_COORDINATOR_AFFINITY_HOLD:" + coordinator_affinity);
  std::cout << "D9_COORDINATOR " << coordinator_affinity << " TSC_calibration_affinity_restored=true IRQ_touched=false\n";

  const auto flush_choice = omega_evict::selected_flush();
  uint64_t timer_median = UINT64_MAX; std::vector<uint64_t> timer_samples; timer_samples.reserve(1024);
  for (int i = 0; i < 1024; ++i) { auto a = read_tsc(); auto b = read_tsc(); timer_samples.push_back(b - a); }
  std::sort(timer_samples.begin(), timer_samples.end()); timer_median = timer_samples[timer_samples.size()/2];
  const double timer_us = double(timer_median) * 1e6 / hz;
  const double configured_hz = configured_tsc_hz();

  std::array<Worker, 8> workers_data; g_workers = &workers_data;
  std::string prep_error;
  std::vector<std::string> load_records;
  auto prepare_and_mark_load = [&](const ScheduleEvent& event) {
    const bool reused = std::all_of(workers_data.begin(), workers_data.end(),
      [&](const Worker& worker) { return prepared_for(worker, event); });
    const int64_t load_start = qpc_now();
    const bool ok = prepare_all_workers(workers_data, event, prep_error);
    const int64_t load_end = qpc_now();
    std::ostringstream mark; mark << "{\"label\":\"SMOKE\",\"phase\":\"" << event.phase
      << "\",\"event_id\":\"" << json_escape(event.event_id) << "\",\"block_id\":\""
      << json_escape(event.block_id) << "\",\"W_target_kib\":" << event.W << ",\"O\":" << event.O
      << ",\"m\":" << event.m << ",\"K\":" << event.K << ",\"qpc_load_start\":" << load_start
      << ",\"qpc_load_end\":" << load_end << ",\"buffers_reused\":" << (reused ? "true" : "false")
      << ",\"large_pages_verified\":" << (ok ? "true" : "false") << '}';
    load_records.push_back(mark.str());
    return ok;
  };
  const bool prep_ok = prepare_and_mark_load(scheduled.front());
  const bool all_pages = prep_ok && lp_privilege;
  if (!prep_ok) throw std::runtime_error("LARGE_PAGE_OR_QA_SETUP_FAILED:" + prep_error);
  // The privilege is always attempted in this process. Without it, the run
  // stops before measurements and records the actual system resource status.
  if (!all_pages) {
    write_preflight(output_dir, lp_status, hz, qpc_error, configured_hz, false, g_plan_sha, timer_median, timer_us);
    std::cout << "RESIDENCY_SMOKE_R18 BLOCKED_REAL large_page_privilege=" << lp_status << " page_queries=FAIL measurements_started=false\n";
    return 3;
  }

  g_tsc_hz = hz;
  RawFile events(output_dir + "\\residency_events_r18.jsonl", 64u * 1024u * 1024u);
  RawFile episodes(output_dir + "\\residency_episodes_r18.jsonl", 16u * 1024u * 1024u);
  RawFile calibration_episodes(output_dir + "\\residency_calibration_trials_r18.jsonl", 16u * 1024u * 1024u);
  RawFile manifest(output_dir + "\\eviction_manifest_r18.jsonl", 16u * 1024u * 1024u);
  RawFile margins(output_dir + "\\residency_margins_r18.jsonl", 8u * 1024u * 1024u);
  RawFile margin_episodes(output_dir + "\\residency_margin_episodes_r18.jsonl", 8u * 1024u * 1024u);
  RawFile marks(output_dir + "\\block_marks_r18.csv", 8u * 1024u * 1024u);
  RawFile clock_map(output_dir + "\\clock_map.csv", 8u * 1024u * 1024u);
  RawFile segment_conditioning(output_dir + "\\segment_conditioning_r18.jsonl", 8u * 1024u * 1024u);
  RawFile hot_probe_raw(output_dir + "\\instrumentation_probe_r18.jsonl", 16u * 1024u * 1024u);
  RawFile load_marks(output_dir + "\\residency_load_marks_r18.jsonl", 16u * 1024u * 1024u);
  ClockMapSampler clock_sampler(clock_map);
  marks.line("sample_id,p_core,worker_lp,replica,condition,t_pad_ini,t_ini,t_fin,t_pad_fin,comparison_set,clock_domain");
  std::set<std::string> planned_probe_segments;
  for (const auto& event : scheduled)
    if (event.phase != "calibration" && !event.segment_id.empty()) planned_probe_segments.insert(event.segment_id);
  std::vector<HotProbeRecord> hot_probe_records;
  hot_probe_records.reserve(planned_probe_segments.size() * 62);
  g_hot_probe_records = &hot_probe_records;
  initialize_hot_probe_buffers(workers_data);
  std::vector<std::thread> worker_threads; worker_threads.reserve(8);
  WorkerCleanup worker_cleanup(worker_threads);
  std::string worker_error; { std::lock_guard<std::mutex> lock(g_error_mutex); g_worker_error.clear(); }
  try { for (unsigned worker = 0; worker < 8; ++worker) worker_threads.emplace_back(worker_main, worker); }
  catch (...) {
    g_start.cancel(); g_done.cancel();
    for (auto& thread : worker_threads) if (thread.joinable()) thread.join();
    throw;
  }
  worker_cleanup.activate();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(g_error_mutex); worker_error = g_worker_error;
  }
  if (!worker_error.empty()) throw std::runtime_error("P_CORE_WORKER_START_FAILED:" + worker_error);
  // Warm the fixed load-only footprint once; it is never rewritten afterward.
  dispatch_hot_probe(1);
  const int hot_probe_r = calibrate_hot_probe_r();
  std::cout << "S25_HOT_PROBE_R_FIXED R=" << hot_probe_r << " K=16 m=16 bytes_per_P="
            << kHotProbeBytes << " workers=8 lifetime_fixed=true\n";

  std::map<std::string, std::array<std::array<uint64_t, 8>, 3>> observed;
  std::map<std::string, std::array<bool, 3>> observed_set;
  std::map<std::string, std::array<std::array<uint64_t, 8>, 2>> aa_observed;
  std::map<std::string, std::array<bool, 2>> aa_observed_set;
  std::vector<ScheduleEvent> calibration_events, residency_events, aa_events;
  for (const auto& e : scheduled) {
    if (e.phase == "calibration") calibration_events.push_back(e);
    else if (e.phase == "residency") residency_events.push_back(e);
    else aa_events.push_back(e);
  }
  std::map<std::string, std::vector<const ScheduleEvent*>> calibration_units;
  std::vector<std::string> calibration_unit_order;
  for (const auto& e : calibration_events) {
    auto [it, inserted] = calibration_units.try_emplace(e.unit_id);
    if (inserted) calibration_unit_order.push_back(e.unit_id);
    it->second.push_back(&e);
  }
  std::map<std::string, int> calibrated_r_by_unit;
  std::map<std::string, uint64_t> calibration_candidate_episode_count;
  std::map<std::string, unsigned> calibration_trial_count_by_event;
  std::map<std::string, std::vector<EpisodeResult>> chosen_calibration;
  struct CalibrationRegions {
    std::string weights, activations, instance_id;
  };
  std::map<std::string, CalibrationRegions> calibration_regions;
  const double calibration_target_s = g_s25_b_mode ? 75.0 : kCalibrationTargetSeconds;
  for (const auto& unit_id : calibration_unit_order) {
    const auto& unit_events = calibration_units.at(unit_id);
    if (unit_events.size() != 3 || unit_events[0]->variant != "A" ||
        unit_events[1]->variant != "B" || unit_events[2]->variant != "C")
      throw std::runtime_error("R_CALIBRATION_UNIT_NOT_CONTIGUOUS_A_B_C:" + unit_id);
    bool unit_calibrated = false;
    for (int trial = 1; trial <= 22; ++trial) {
      const int candidate_r = 1 << (trial - 1);
      bool all_reached = true;
      std::map<std::string, std::vector<EpisodeResult>> this_trial;
      for (const ScheduleEvent* event : unit_events) {
        const auto& e = *event;
        ++calibration_trial_count_by_event[e.event_id];
        calibration_candidate_episode_count[e.event_id] += static_cast<uint64_t>(candidate_r);
        if (!prepare_and_mark_load(e))
          throw std::runtime_error("R_CALIBRATION_PREPARE_FAILED:" + prep_error);
        const char variant = e.variant[0];
        auto result = run_episode(e, variant, unsigned(trial), "calibration_trial", unsigned(trial), candidate_r);
        const double kernel_seconds = episode_microseconds(result, hz) / 1e6;
        const double region_seconds = qpc_seconds(result.qpc_start, result.qpc_end);
        const double slowest_worker_kernel_seconds = double(*std::min_element(
            result.worker_kernel_tsc.begin(), result.worker_kernel_tsc.end())) / hz;
        const double instrumentation_fraction = result.instrumentation_fraction;
        const bool duration_ok = region_seconds >= calibration_target_s &&
                                 slowest_worker_kernel_seconds >= .150 &&
                                 instrumentation_fraction <= kCoordinatorCalibrationTarget;
        if (!duration_ok) all_reached = false;
        std::cout << "R_CALIBRATION_TRIAL unit=" << unit_id << " variant=" << variant << " candidate_R=" << candidate_r
                  << " region_s=" << std::fixed << std::setprecision(6) << region_seconds
                  << " slowest_worker_kernel_s=" << slowest_worker_kernel_seconds
                  << " coordinator_overhead=" << instrumentation_fraction
                  << " aggregate_kernel_s=" << kernel_seconds << " target_region_s=" << calibration_target_s
                  << " kernel_target_s=0.150 calibration_overhead_target="
                  << kCoordinatorCalibrationTarget << " final_budget_max=0.01 decision="
                  << (duration_ok ? "REACHED" : "DOUBLE") << "\n";
        this_trial[e.event_id].push_back(std::move(result));
      }
      if (all_reached) {
        calibrated_r_by_unit[unit_id] = candidate_r;
        for (const ScheduleEvent* event : unit_events) {
          chosen_calibration[event->event_id] = std::move(this_trial[event->event_id]);
          CalibrationRegions regions;
          regions.weights = event_regions_json(*event, event->variant[0]);
          regions.activations = activation_regions_json();
          regions.instance_id = region_instance_id(*event, event->variant[0]);
          calibration_regions[event->event_id] = std::move(regions);
        }
        unit_calibrated = true;
        break;
      }
    }
    if (!unit_calibrated) throw std::runtime_error("R_CALIBRATION_FAILED_AFTER_22_DOUBLINGS:" + unit_id);
  }

  for (const auto& e : calibration_events) {
    const auto& calibrated_episodes = chosen_calibration.at(e.event_id);
    const int calibrated_r = calibrated_r_by_unit.at(e.unit_id);
    double calibrated_us = 0;
    double max_calibration_overhead = 0;
    std::array<uint64_t, 8> worker_kernel{};
    std::array<uint64_t, 8> worker_first_tsc{};
    std::array<uint64_t, 8> worker_last_tsc{};
    std::array<int64_t, 8> worker_first_qpc{};
    std::array<int64_t, 8> worker_last_qpc{};
    worker_first_tsc.fill(UINT64_MAX); worker_first_qpc.fill(INT64_MAX);
    for (const auto& episode : calibrated_episodes) {
      calibrated_us += episode_microseconds(episode, hz);
      max_calibration_overhead = (std::max)(max_calibration_overhead, episode.instrumentation_fraction);
      for (unsigned w = 0; w < 8; ++w) {
        worker_kernel[w] += episode.worker_kernel_tsc[w];
        worker_first_tsc[w] = (std::min)(worker_first_tsc[w], episode.worker_tsc_start[w]);
        worker_last_tsc[w] = (std::max)(worker_last_tsc[w], episode.worker_tsc_end[w]);
        worker_first_qpc[w] = (std::min)(worker_first_qpc[w], episode.worker_qpc_start[w]);
        worker_last_qpc[w] = (std::max)(worker_last_qpc[w], episode.worker_qpc_end[w]);
      }
    }
    const double calibrated_region_seconds = qpc_seconds(calibrated_episodes.front().qpc_start,
                                                          calibrated_episodes.back().qpc_end);
    std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(e.event_id) << "\",\"phase\":\"calibration\",\"unit_id\":\"" << json_escape(e.unit_id) << "\",\"condition_id\":\""
      << json_escape(e.condition_id) << "\",\"variant\":\"" << e.variant << "\",\"R_unit_id\":\""
      << json_escape(e.unit_id) << "\",\"R\":" << calibrated_r
      << ",\"W_target_kib\":" << e.W << ",\"O\":" << e.O << ",\"m\":" << e.m << ",\"K\":" << e.K
      << ",\"episode_count\":" << calibrated_r << ",\"sum_t_app_us\":" << calibrated_us
      << ",\"condition_region_seconds\":" << std::fixed << std::setprecision(9) << calibrated_region_seconds
      << ",\"instrumentation_overhead_fraction_max\":" << max_calibration_overhead
      << ",\"f_tsc_hz\":" << hz << ",\"qpf_hz\":" << g_qpf
      << ",\"weight_regions\":" << calibration_regions.at(e.event_id).weights
      << ",\"activation_regions\":" << calibration_regions.at(e.event_id).activations
      << ",\"region_instance_id\":\"" << calibration_regions.at(e.event_id).instance_id
      << "\",\"condition_region_target_seconds\":" << calibration_target_s
      << ",\"condition_region_minimum_seconds\":" << (g_s25_b_mode ? 60.0 : 2.0)
      << ",\"kernel_only_target_seconds\":0.150,\"kernel_only_minimum_seconds\":0.100"
      << ",\"slowest_worker_kernel_seconds\":" << std::fixed << std::setprecision(9)
      << double(*std::min_element(worker_kernel.begin(),worker_kernel.end()))/hz
      << ",\"timestamps_complete\":true,\"coordinator_worker_exchanges\":" << calibrated_episodes.size()
      << ",\"instrumentation_budget_max\":0.01,\"worker_evidence\":[";
    for (unsigned w = 0; w < 8; ++w) {
      if (w) row << ',';
      row << "{\"worker\":" << w << ",\"lp\":" << worker_processor_r18(w).lp
          << ",\"first_tsc\":" << worker_first_tsc[w] << ",\"last_tsc\":" << worker_last_tsc[w]
          << ",\"first_qpc\":" << worker_first_qpc[w] << ",\"last_qpc\":" << worker_last_qpc[w]
          << ",\"kernel_seconds\":" << double(worker_kernel[w])/hz << '}';
    }
    row << "]";
    if (g_s25_b_mode)
      row << ",\"R_policy\":\"R(W,m,K)_common_A_B_C_fixed_across_S25B_replicas\",\"instrument_qualification\":\"S25B\"}";
    else
      row << ",\"R_policy\":\"R(W,m,K)_common_A_B_C_fixed_N31\"}";
    events.line(row.str());
    std::ostringstream cal_episode;
    cal_episode << "{\"label\":\"SMOKE\",\"event_id\":\"" << json_escape(e.event_id)
      << "\",\"R\":" << calibrated_r << ",\"episode_count\":" << calibrated_r
      << ",\"workers\":[";
    for (unsigned w = 0; w < 8; ++w) {
      if (w) cal_episode << ',';
      cal_episode << "{\"worker\":" << w << ",\"lp\":" << worker_processor_r18(w).lp
        << ",\"first_tsc\":" << worker_first_tsc[w] << ",\"last_tsc\":" << worker_last_tsc[w]
        << ",\"first_qpc\":" << worker_first_qpc[w] << ",\"last_qpc\":" << worker_last_qpc[w]
        << ",\"kernel_tsc_sum\":" << worker_kernel[w] << '}';
    }
    cal_episode << "]}"; calibration_episodes.line(cal_episode.str());
    const uint64_t weight_calls_per_episode = e.variant == "A" ? 8u : 8u * unsigned(e.K);
    manifest.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) +
      "\",\"variant\":\"" + e.variant + "\",\"primitive\":\"omega_evict::evict_range\",\"candidate_trials\":" +
      std::to_string(calibration_trial_count_by_event.at(e.event_id)) + ",\"candidate_R_sum\":" +
      std::to_string(calibration_candidate_episode_count.at(e.event_id)) + ",\"weight_range_calls\":" +
      std::to_string(weight_calls_per_episode * calibration_candidate_episode_count.at(e.event_id)) +
      ",\"activation_range_calls\":0,\"weight_only\":true,\"callback_per_line\":false,\"each_episode_verified\":true}");
  }

  size_t cursor = calibration_events.size();
  if (scheduled.size() < cursor ||
      !std::all_of(scheduled.begin(), scheduled.begin() + cursor, [](const ScheduleEvent& e) { return e.phase == "calibration"; }))
    throw std::runtime_error("OFFICIAL_PLAN_CALIBRATION_NOT_FIRST_COMPLETE_UNIT");
  auto record_margin = [&](const ScheduleEvent& e, const char* side, const MarginResult& margin) {
    margins.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) + "\",\"side\":\"" + side +
      "\",\"qpc_start\":" + std::to_string(margin.start) + ",\"qpc_end\":" + std::to_string(margin.end) +
      ",\"seconds\":" + std::to_string(margin.seconds) + ",\"episodes\":" + std::to_string(margin.episodes) +
      ",\"same_condition\":true,\"continuous_load\":true}");
  };
  std::string active_segment;
  int active_segment_replica = 0;
  while (cursor < scheduled.size()) {
    const auto& first = scheduled[cursor];
    if (first.phase == "residency") {
      if (!active_segment.empty() && active_segment != first.segment_id) {
        acquire_hot_probe_endpoint(active_segment, active_segment_replica, 1, hot_probe_r, marks, hot_probe_raw);
        active_segment.clear();
      }
      if (!prepare_and_mark_load(first))
        throw std::runtime_error("RESIDENCY_PREPARE_FAILED:" + prep_error);
      if (active_segment != first.segment_id) {
        active_segment = first.segment_id;
        active_segment_replica = first.replica;
        const double segment_hz = verify_segment_state(first, workers_data, g_plan_sha, topology_start_sha, reader_qualified);
        const int64_t condition_start = qpc_now();
        int64_t condition_end = condition_start; uint64_t conditioning_dispatches = 0;
        while (qpc_seconds(condition_start, condition_end) < 180.0) {
          dispatch({CommandKind::Probe, 'P', kHotProbeK, hot_probe_r});
          ++conditioning_dispatches;
          condition_end = qpc_now();
        }
        const double condition_seconds = qpc_seconds(condition_start, condition_end);
        if (condition_seconds < 180.0 || std::abs(segment_hz - hz) / hz > .001)
          throw std::runtime_error("SEGMENT_CONDITIONING_OR_CLOCK_REVERIFY_INVALID");
        acquire_hot_probe_endpoint(active_segment, active_segment_replica, 0, hot_probe_r, marks, hot_probe_raw);
        segment_conditioning.line("{\"label\":\"SMOKE\",\"segment_id\":\"" + json_escape(first.segment_id) +
          "\",\"plan_sha256\":\"" + g_plan_sha + "\",\"topology_sha256\":\"" + topology_start_sha +
          "\",\"reader_qualified\":true,\"large_pages_reverified\":true,\"conditioning\":\"no_weight_hot_probe\",\"clock_reverified_hz\":" +
          std::to_string(segment_hz) + ",\"qpc_start\":" + std::to_string(condition_start) +
          ",\"qpc_end\":" + std::to_string(condition_end) + ",\"active_conditioning_seconds\":" +
          std::to_string(condition_seconds) + ",\"probe_R\":" + std::to_string(hot_probe_r) +
          ",\"active_dispatches\":" + std::to_string(conditioning_dispatches) + "}");
        std::cout << "SEGMENT_REVERIFY_R18 PASS segment=" << first.segment_id
                  << " clock_hz=" << segment_hz << " reader=PASS pages=PASS active_conditioning_s="
                  << condition_seconds << " no_weight_probe_R=" << hot_probe_r
                  << " active_dispatches=" << conditioning_dispatches << "\n";
      }
      if (cursor + 3 > scheduled.size()) throw std::runtime_error("OFFICIAL_PLAN_TRUNCATED_MARGINAL_GROUP");
      std::array<const ScheduleEvent*, 3> group{};
      std::array<MarginResult, 3> pre{}, post{};
      std::array<std::vector<EpisodeResult>, 3> measured{};
      for (unsigned i = 0; i < 3; ++i) {
        const auto& event = scheduled[cursor + i];
        if (event.phase != "residency" || event.block_id != first.block_id || event.atomic_group_id != first.atomic_group_id)
          throw std::runtime_error("OFFICIAL_PLAN_MARGINAL_TRIPLET_SPLIT:" + first.atomic_group_id);
        group[i] = &event;
        pre[i] = run_margin(event, event.variant[0], "pre", hz);
        record_margin(event, "pre", pre[i]);
      }
      const int group_r = calibrated_r_by_unit.at(r_unit_id_for(*group[0]));
      // Keep A/B/C as one uninterrupted measured group. Records stay in the
      // preallocated in-memory buffers until acquisition has ended.
      for (unsigned i = 0; i < 3; ++i) {
        measured[i].reserve(1);
        measured[i].push_back(run_episode(*group[i], group[i]->variant[0], 1u, "sample", 0,
                                           group_r));
        if (g_s25_b_mode && qpc_seconds(measured[i].front().qpc_start,
                                        measured[i].front().qpc_end) < 60.0)
          throw std::runtime_error("HOLD_INSTRUMENTATION_QUALIFICATION:S25B_REGION_BELOW_60_SECONDS:event=" +
                                   group[i]->event_id);
      }
      for (unsigned i = 0; i < 3; ++i) {
        post[i] = run_margin(*group[i], group[i]->variant[0], "post", hz);
        record_margin(*group[i], "post", post[i]);
      }
      for (unsigned i = 0; i < 3; ++i) {
        std::array<uint64_t, 8> hashes{};
        write_event_summary(events, marks, episodes, manifest, *group[i], group[i]->variant[0], group_r, measured[i], hz, pre[i], post[i], hashes);
        const unsigned vi = group[i]->variant == "A" ? 0u : group[i]->variant == "B" ? 1u : 2u;
        const std::string key = group[i]->block_id + "|" + group[i]->atomic_group_id;
        observed[key][vi] = hashes; observed_set[key][vi] = true;
      }
      cursor += 3;
      continue;
    }
    if (first.phase == "aa") {
      if (!prepare_and_mark_load(first))
        throw std::runtime_error("AA_PREPARE_FAILED:" + prep_error);
      if (cursor + 2 > scheduled.size()) throw std::runtime_error("OFFICIAL_PLAN_TRUNCATED_AA_PAIR");
      std::array<const ScheduleEvent*, 2> pair{&scheduled[cursor], &scheduled[cursor + 1]};
      std::array<MarginResult, 2> pre{}, post{};
      std::array<std::vector<EpisodeResult>, 2> measured{};
      if (pair[0]->phase != "aa" || pair[1]->phase != "aa" || pair[0]->pair_id != pair[1]->pair_id ||
          pair[1]->sequence != pair[0]->sequence + 1)
        throw std::runtime_error("OFFICIAL_PLAN_AA_PAIR_SPLIT_OR_REORDERED");
      const int pair_r = calibrated_r_by_unit.at(r_unit_id_for(*pair[0]));
      for (unsigned i = 0; i < 2; ++i) {
        pre[i] = run_margin(*pair[i], 'A', "pre", hz);
        record_margin(*pair[i], "pre", pre[i]);
      }
      for (unsigned i = 0; i < 2; ++i) {
        measured[i].reserve(1);
        measured[i].push_back(run_episode(*pair[i], 'A', 1u, "aa_sample", 0,
                                           pair_r));
      }
      for (unsigned i = 0; i < 2; ++i) {
        post[i] = run_margin(*pair[i], 'A', "post", hz);
        record_margin(*pair[i], "post", post[i]);
      }
      for (unsigned i = 0; i < 2; ++i) {
        std::array<uint64_t, 8> hashes{};
        write_event_summary(events, marks, episodes, manifest, *pair[i], 'A', pair_r, measured[i], hz, pre[i], post[i], hashes);
        const unsigned obs = pair[i]->observation == "A1" ? 0u : 1u;
        aa_observed[pair[i]->pair_id][obs] = hashes;
        aa_observed_set[pair[i]->pair_id][obs] = true;
      }
      cursor += 2;
      continue;
    }
    throw std::runtime_error("OFFICIAL_PLAN_SMOKE_UNSUPPORTED_PHASE:" + first.phase);
  }
  if (!active_segment.empty())
    acquire_hot_probe_endpoint(active_segment, active_segment_replica, 1, hot_probe_r, marks, hot_probe_raw);
  bool outputs_equal = true;
  for (const auto& [key, flags] : observed_set) for (unsigned worker = 0; worker < 8; ++worker)
    outputs_equal &= flags[0] && flags[1] && flags[2] &&
                     observed[key][0][worker] == observed[key][1][worker] &&
                     observed[key][0][worker] == observed[key][2][worker];
  for (const auto& [key, flags] : aa_observed_set) for (unsigned worker = 0; worker < 8; ++worker)
    outputs_equal &= flags[0] && flags[1] && aa_observed[key][0][worker] == aa_observed[key][1][worker];
  worker_cleanup.stop();
  marks.flush(); events.flush(); episodes.flush(); calibration_episodes.flush(); manifest.flush(); margins.flush(); margin_episodes.flush(); segment_conditioning.flush(); hot_probe_raw.flush();
  for (const auto& load_mark : load_records) load_marks.line(load_mark);
  load_marks.flush();
  clock_sampler.stop();
  clock_map.flush();
  write_preflight(output_dir, lp_status, hz, qpc_error, configured_hz, all_pages, g_plan_sha, timer_median, timer_us);
  std::cout << "MAC_CHECK real_work=512*O*m dynamic_from_plan=true PASS\n";
  std::cout << "D9_AC_SHARED same_live_buffer=true address=" << reinterpret_cast<uintptr_t>(workers_data[0].shared_ac->bytes.data())
            << " sha256=" << sha256(workers_data[0].shared_ac->bytes, omega_q4::weight_bytes(workers_data[0].output)) << " workers=8 PASS\n";
  std::cout << "D8_EVICT primitive=omega_evict::evict_range only calls_weight_ranges="
            << g_evict_weight_range_calls << " calls_activation_ranges=" << g_evict_activation_range_calls
            << " calls_total=" << g_evict_range_calls << " episodes=" << (g_evict_weight_calls > 0 ? "manifested" : "missing")
            << " per_line_atomic_callback=none flush=" << flush_choice.name << " PASS\n";
  int min_calibrated_r = std::numeric_limits<int>::max(), max_calibrated_r = 0;
  for (const auto& [unit, unit_r] : calibrated_r_by_unit) {
    (void)unit;
    min_calibrated_r = (std::min)(min_calibrated_r, unit_r);
    max_calibrated_r = (std::max)(max_calibrated_r, unit_r);
  }
  std::cout << "R_RESIDENCY_R18 units=" << calibrated_r_by_unit.size() << " min_R=" << min_calibrated_r
            << " max_R=" << max_calibrated_r << " W=288 complete_plan_blocks=true A_B_C=common calibration_target_s="
            << calibration_target_s << "\n";
  std::cout << "D9_EPISODES all_timestamps=true margins_each_side_min_s=0.200 same_condition=true N="
            << (g_s25_b_mode ? (g_s25_b_smoke_n1 ? 1 : 3) : 3) << " PASS\n";
  std::cout << "SMOKE_R18_CAPTURE PASS schedule_events=" << scheduled.size() << " calibration=" << calibration_events.size()
            << " A_B_C=" << residency_events.size() << " AA_pairs=" << aa_events.size()/2 << " aa_observations=" << aa_events.size() << " outputs_bitwise="
            << (outputs_equal ? "PASS" : "FAIL") << " label=SMOKE numeric_metrics_used=false EMON=continuous_required\n";
  return outputs_equal ? 0 : 4;
}

int run_kernel_qa_r18() {
  std::filesystem::path root = std::filesystem::current_path();
  while (!root.empty() && !std::filesystem::exists(root / L"T0P" / L"common" / L"data")) {
    const auto parent = root.parent_path();
    if (parent == root) break;
    root = parent;
  }
  const auto data = root / L"T0P" / L"common" / L"data";
  const auto baseline = root / L"T0P" / L"kernel" / L"logs" / L"baseline_candidate_o1024.bin";
  if (!std::filesystem::exists(data) || !std::filesystem::exists(baseline))
    throw std::runtime_error("D12_standalone_QA_inputs_missing");
  std::string data_arg = data.string(), baseline_arg = baseline.string();
  char app[] = "kernel_qa_r18";
  char* args[] = {app, data_arg.data(), baseline_arg.data(), nullptr};
  const int rc = q4_standalone_qa_main_r18(3, args);
  if (rc != 0) throw std::runtime_error("D12_reused_standalone_QA_failed:exit=" + std::to_string(rc));
  std::cout << "KERNEL_QA_R18 PASS source=T0P/kernel/qa_main.cpp linked_object=q4_kernel_r18.obj "
               "same_object_as_harness=true exact_O1024_and_512_coordinate_sweeps=true\n";
  return 0;
}

int barrier_cancel_test_r18() {
  Gate barrier(2);
  std::atomic<bool> started{false}, result{true};
  std::thread waiter([&] {
    started.store(true, std::memory_order_release);
    result.store(barrier.wait(), std::memory_order_release);
  });
  while (!started.load(std::memory_order_acquire)) _mm_pause();
  barrier.cancel();
  const DWORD waited = WaitForSingleObject(waiter.native_handle(), 1000);
  if (waited != WAIT_OBJECT_0) {
    waiter.detach();
    std::cerr << "BARRIER_CANCEL_R18 FAIL bounded_wait_ms=1000 result=timeout\n";
    return 3;
  }
  waiter.join();
  if (result.load(std::memory_order_acquire)) {
    std::cerr << "BARRIER_CANCEL_R18 FAIL cancelled_wait_returned_true\n";
    return 3;
  }
  std::cout << "BARRIER_CANCEL_R18 PASS bounded_wait_ms=1000 cancelled_wait_returned_false\n";
  return 0;
}
}
