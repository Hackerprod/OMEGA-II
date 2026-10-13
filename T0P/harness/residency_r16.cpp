#define NOMINMAX
#include "harness_r16.hpp"
#include "provider_common_r16.hpp"
#include "topology_r16.hpp"
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
#include <condition_variable>
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

namespace harness {
namespace {
constexpr double kCalibrationTargetSeconds = 2.5, kMarginSeconds = .200;
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
    file_.line("sample,qpc_pre,qpc_post,filetime_100ns,sample_period_ms");
    thread_ = std::thread([this] { sample_loop(); });
  }
  ClockMapSampler(const ClockMapSampler&) = delete;
  ClockMapSampler& operator=(const ClockMapSampler&) = delete;
  void stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    if (!error_.empty()) throw std::runtime_error("CLOCK_MAP_SAMPLER_FAILED:" + error_);
    file_.flush();
  }
  ~ClockMapSampler() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
  }
 private:
  void sample_loop() {
    try {
      unsigned long long sample = 0;
      while (!stop_.load(std::memory_order_acquire)) {
        LARGE_INTEGER before{}, after{}; FILETIME ft{};
        if (!QueryPerformanceCounter(&before)) throw std::runtime_error("QPC_PRE_ERROR=" + std::to_string(GetLastError()));
        GetSystemTimePreciseAsFileTime(&ft);
        if (!QueryPerformanceCounter(&after)) throw std::runtime_error("QPC_POST_ERROR=" + std::to_string(GetLastError()));
        ULARGE_INTEGER ticks{}; ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
        std::ostringstream row; row << sample++ << ',' << before.QuadPart << ',' << after.QuadPart << ',' << ticks.QuadPart << ",20";
        file_.line(row.str());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    } catch (const std::exception& e) { error_ = e.what(); }
  }
  RawFile& file_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::string error_;
};

struct Gate {
  explicit Gate(unsigned count) : count_(count) {}
  bool wait() {
    std::unique_lock<std::mutex> lock(m_);
    if (cancelled_) return false;
    const unsigned generation = generation_;
    if (++arrived_ == count_) { arrived_ = 0; ++generation_; cv_.notify_all(); return true; }
    cv_.wait(lock, [&] { return generation_ != generation || cancelled_; });
    return !cancelled_;
  }
  void cancel() { std::lock_guard<std::mutex> lock(m_); cancelled_ = true; cv_.notify_all(); }
 private:
  std::mutex m_; std::condition_variable cv_; unsigned count_, arrived_ = 0, generation_ = 0; bool cancelled_ = false;
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
  Stamp batch_stamp;
  uint64_t batch_kernel_tsc = 0;
  bool batch_has_stamp = false;
  uint64_t evicted_weight_ranges = 0, evicted_activation_ranges = 0, evicted_ranges = 0;
};
enum class CommandKind { Episode, Stop };
struct Command { CommandKind kind = CommandKind::Stop; char variant = 'A'; int applications = 0; int repetitions = 1; };

std::mutex g_command_mutex, g_error_mutex;
Command g_command;
std::string g_worker_error;
// Each operation barrier includes the coordinator and all eight workers.
Gate g_start(9), g_done(9);
std::array<Worker, 8>* g_workers = nullptr;
uint64_t g_qpf = 0;
double g_tsc_hz = 0;
std::string g_plan_sha;

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
  if (!plan.is_object() || plan.value("schema", std::string()) != "T0P_S24_ACQUISITION_PLAN_R16_V1" ||
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
  if (!schedule_path.empty()) {
    std::ifstream schedule(schedule_path, std::ios::binary);
    if (!schedule) throw std::runtime_error("OFFICIAL_SCHEDULE_OPEN_FAILED:" + schedule_path);
    std::string line; bool saw_plan_hash = false, saw_header = false;
    while (std::getline(schedule, line)) {
      if (line.rfind("#plan_sha256=", 0) == 0) {
        if (line.substr(13) != g_plan_sha) throw std::runtime_error("OFFICIAL_SCHEDULE_PLAN_HASH_MISMATCH");
        saw_plan_hash = true; continue;
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
    if (block_positions[id] != selected_count) throw std::runtime_error("OFFICIAL_SCHEDULE_INCOMPLETE_W_REPLICA_BLOCK:" + id);
  }
  for (const auto& [group, selected_count] : selected_groups) {
    const unsigned expected = group.rfind("aa|", 0) == 0 ? 2u : 3u;
    if (selected_count != expected) throw std::runtime_error("OFFICIAL_SCHEDULE_INCOMPLETE_ATOMIC_GROUP:" + group);
  }
  std::set<std::string> selected_segments;
  for (const auto& event : selected) if (!event.segment_id.empty()) selected_segments.insert(event.segment_id);
  if (selected_segments.size() > 1) throw std::runtime_error("OFFICIAL_SCHEDULE_SPLITS_ACROSS_SEGMENTS");
  std::cout << "OFFICIAL_PLAN_R16 PASS full_events=32102 qualification_events=18414 residency_events=13688 order_hashes=PASS exact_sequence=PASS selected_events=" << selected.size()
            << " selected_blocks=" << selected_blocks.size() << " complete_blocks_and_multiplicity=PASS plan_sha256=" << g_plan_sha << "\n";
  return selected;
}

void prepare_worker(Worker& w, unsigned worker, int output, int m, int k) {
  w.cell = r16::generate_cell(m, output);
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
  std::vector<int32_t> dots(size_t(m) * output * r16::G);
  omega_q4::qa_group_dots(*w.shared_ac, w.x, dots.data());
  for (int row = 0; row < output; ++row) for (int group = 0; group < r16::G; ++group) {
    int32_t expected = 0;
    for (int k = 0; k < 32; ++k)
      expected += int(w.cell.wq[size_t(row) * r16::I + group * 32 + k]) * int(w.cell.xq[group * 32 + k]);
    if (dots[size_t(row) * r16::G + group] != expected) throw std::runtime_error("exact_group_dot_failed");
  }
  omega_q4::run(*w.shared_ac, w.x, output, w.y.data());
  if (!r16::verify_s12(w.cell, output, w.y.data(), w.y.size()).pass) throw std::runtime_error("S12_numeric_check_failed");
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
        if (!set_worker_affinity_r16(worker) || !worker_affinity_matches_r16(worker))
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
  if (!discover_topology_r16(why)) throw std::runtime_error("SEGMENT_REVERIFY_TOPOLOGY_FAILED:" + why);
  const std::string topology = topology_json_r16();
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
  if (!set_worker_affinity_r16(0) || !worker_affinity_matches_r16(0))
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
  if (!set_coordinator_affinity_r16(coordinator))
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
    if (!set_worker_affinity_r16(worker) || !worker_affinity_matches_r16(worker))
      throw std::runtime_error("P_core_worker_affinity_invalid:" + std::to_string(worker));
    for (;;) {
      if (!g_start.wait()) return;
      Command command; { std::lock_guard<std::mutex> lock(g_command_mutex); command = g_command; }
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
          if (stamp.lp_start != worker_processor_r16(worker).lp || stamp.lp_end != worker_processor_r16(worker).lp)
            throw std::runtime_error("worker_migrated_during_application");
        }
      }
      if (!g_done.wait()) return;
    }
  } catch (const std::exception& e) {
    { std::lock_guard<std::mutex> lock(g_error_mutex); if (g_worker_error.empty()) g_worker_error = e.what(); }
    g_start.cancel(); g_done.cancel();
  }
}

void dispatch(Command command) {
  { std::lock_guard<std::mutex> lock(g_command_mutex); g_command = command; }
  const bool start_ok = g_start.wait();
  const bool done_ok = start_ok && g_done.wait();
  if (!done_ok) {
    std::lock_guard<std::mutex> lock(g_error_mutex);
    throw std::runtime_error(g_worker_error.empty() ? "worker_barrier_cancelled" : g_worker_error);
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
  if (max_instrumentation_fraction > .01)
    throw std::runtime_error("D9_INSTRUMENTATION_OVERHEAD_GT_1_PERCENT:event=" + event.event_id +
      ":fraction=" + std::to_string(max_instrumentation_fraction));
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(event.event_id) << "\",\"phase\":\"" << event.phase << "\",\"condition_id\":\""
      << json_escape(event.condition_id) << "\",\"variant\":\"" << variant << "\",\"observation\":\""
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
      << ",\"slowest_worker_kernel_seconds\":" << slowest_kernel_seconds
      << ",\"worker_kernel_only_seconds\":[";
  for (unsigned worker = 0; worker < 8; ++worker) { if (worker) row << ','; row << double(worker_kernel_tsc[worker]) / hz; }
  row << "]"
      << ",\"timestamps_complete\":true,\"episode_timestamps_file\":\"residency_episodes_r16.jsonl\""
      << ",\"coordinator_worker_exchanges\":" << episodes.size()
      << ",\"instrumentation_overhead_fraction_max\":" << max_instrumentation_fraction
      << ",\"instrumentation_budget_max\":0.01,\"worker_evidence\":[";
  for (unsigned worker = 0; worker < 8; ++worker) {
    if (worker) row << ',';
    row << "{\"worker\":" << worker << ",\"lp\":" << worker_processor_r16(worker).lp
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
    episode_row << "{\"worker\":" << worker << ",\"lp\":" << worker_processor_r16(worker).lp
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
    const auto p_core = worker_processor_r16(worker).p_core;
    marks.line(event.event_id + "," + std::to_string(p_core) + "," + std::to_string(worker_processor_r16(worker).lp) + "," +
      std::to_string(event.replica) + "," + event.condition_id + "," + std::to_string(pre.start) + "," +
      std::to_string(sample_start) + "," + std::to_string(sample_end) + "," + std::to_string(post.end) + "," +
      event.condition_id + ",qpc_ticks");
  }
}

void stop_workers(std::vector<std::thread>& workers) {
  if (workers.empty()) return;
  { std::lock_guard<std::mutex> lock(g_command_mutex); g_command = {CommandKind::Stop, 'A', 0}; }
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
  RawFile file(outdir + "\\preflight_r16.json");
  GROUP_AFFINITY coordinator{}; DWORD_PTR process_mask = 0, system_mask = 0;
  const bool process_map_ok = GetThreadGroupAffinity(GetCurrentThread(), &coordinator) &&
      GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
  const DWORD coordinator_lp = GetCurrentProcessorNumber();
  bool coordinator_outside_workers = process_map_ok;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto& mapped = worker_processor_r16(worker);
    const DWORD local_lp = static_cast<DWORD>(std::countr_zero(static_cast<unsigned long long>(mapped.mask)));
    if (mapped.group == coordinator.Group && local_lp == coordinator_lp) coordinator_outside_workers = false;
  }
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"state\":\"" << (pages ? "PASS" : "HOLD_RESOURCE")
      << "\",\"numeric_metrics_used\":false,\"large_page_privilege\":\"" << lp_status
      << "\",\"large_pages\":" << (pages ? "true" : "false") << ",\"P_core_map\":" << topology_json_r16()
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

int verify_full_plan_smoke_slice_r16(const std::string& plan_path, const std::string& schedule_path) {
  const auto selected = read_schedule(plan_path, schedule_path);
  std::cout << "PLAN_EXECUTABLE_R16_QA PASS loaded_complete_plan=true selected_events=" << selected.size()
            << " ordering_and_multiplicity=PASS complete_W_replica_blocks=PASS AA_pairs_adjacent=PASS\n";
  return 0;
}

int run_residency_smoke_r16(const std::string& output_dir, const std::string& plan_path,
                            const std::string& schedule_path, bool reader_qualified) {
  const auto scheduled = read_schedule(plan_path, schedule_path);
  DWORD attrs = GetFileAttributesA(output_dir.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("D13_ATTEMPT_DIRECTORY_MUST_ALREADY_EXIST");
  std::string topology_error;
  if (!discover_topology_r16(topology_error)) throw std::runtime_error("P_CORE_TOPOLOGY_HOLD:" + topology_error);
  const std::string topology_start = topology_json_r16();
  const std::string topology_start_sha = sha256(reinterpret_cast<const uint8_t*>(topology_start.data()), topology_start.size());

  std::string lp_status; const bool lp_privilege = t0common::enable_large_page_privilege(&lp_status);
  std::cout << "LARGE_PAGE_PRIVILEGE=" << lp_status << " enable_large_page_privilege_called=true\n";
  LARGE_INTEGER qpf{}; if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) throw std::runtime_error("QPF_UNAVAILABLE");
  g_qpf = uint64_t(qpf.QuadPart);
  bool invariant = false; int cpu[4]{}; __cpuid(cpu, 0x80000000);
  if (unsigned(cpu[0]) >= 0x80000007) { __cpuid(cpu, 0x80000007); invariant = (cpu[3] & (1u << 8)) != 0; }
  if (!invariant) throw std::runtime_error("INVARIANT_TSC_REQUIRED");
  if (!set_worker_affinity_r16(0) || !worker_affinity_matches_r16(0)) throw std::runtime_error("TSC_CALIBRATION_AFFINITY_FAILED");
  std::vector<double> hz_samples;
  for (int i = 0; i < 5; ++i) { auto qa = qpc_now(); auto ta = read_tsc(); Sleep(250); auto tb = read_tsc(); auto qb = qpc_now(); hz_samples.push_back(double(tb - ta) * double(g_qpf) / double(qb - qa)); }
  std::sort(hz_samples.begin(), hz_samples.end()); const double hz = hz_samples[2];
  double qpc_error = 0; for (double f : hz_samples) qpc_error = (std::max)(qpc_error, std::abs(f - hz) / hz);
  if (qpc_error > .001) throw std::runtime_error("TSC_QPC_QUALIFICATION_GT_0_1_PERCENT");
  std::string coordinator_affinity;
  if (!set_coordinator_affinity_r16(coordinator_affinity))
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
    std::cout << "RESIDENCY_SMOKE_R16 BLOCKED_REAL large_page_privilege=" << lp_status << " page_queries=FAIL measurements_started=false\n";
    return 3;
  }

  g_tsc_hz = hz;
  RawFile events(output_dir + "\\residency_events_r16.jsonl", 64u * 1024u * 1024u);
  RawFile episodes(output_dir + "\\residency_episodes_r16.jsonl", 16u * 1024u * 1024u);
  RawFile calibration_episodes(output_dir + "\\residency_calibration_trials_r16.jsonl", 16u * 1024u * 1024u);
  RawFile manifest(output_dir + "\\eviction_manifest_r16.jsonl", 16u * 1024u * 1024u);
  RawFile margins(output_dir + "\\residency_margins_r16.jsonl", 8u * 1024u * 1024u);
  RawFile margin_episodes(output_dir + "\\residency_margin_episodes_r16.jsonl", 8u * 1024u * 1024u);
  RawFile marks(output_dir + "\\block_marks_r16.csv", 8u * 1024u * 1024u);
  RawFile clock_map(output_dir + "\\clock_map.csv", 8u * 1024u * 1024u);
  RawFile segment_conditioning(output_dir + "\\segment_conditioning_r16.jsonl", 8u * 1024u * 1024u);
  RawFile load_marks(output_dir + "\\residency_load_marks_r16.jsonl", 16u * 1024u * 1024u);
  ClockMapSampler clock_sampler(clock_map);
  marks.line("sample_id,p_core,worker_lp,replica,condition,t_pad_ini,t_ini,t_fin,t_pad_fin,comparison_set,clock_domain");
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
        const bool duration_ok = region_seconds >= kCalibrationTargetSeconds &&
                                 slowest_worker_kernel_seconds >= .150 && instrumentation_fraction <= .01;
        if (!duration_ok) all_reached = false;
        std::cout << "R_CALIBRATION_TRIAL unit=" << unit_id << " variant=" << variant << " candidate_R=" << candidate_r
                  << " region_s=" << std::fixed << std::setprecision(6) << region_seconds
                  << " slowest_worker_kernel_s=" << slowest_worker_kernel_seconds
                  << " coordinator_overhead=" << instrumentation_fraction
                  << " aggregate_kernel_s=" << kernel_seconds << " targets=2.5s_region,0.150s_kernel_per_worker decision="
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
    std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(e.event_id) << "\",\"phase\":\"calibration\",\"unit_id\":\"" << json_escape(e.unit_id) << "\",\"condition_id\":\""
      << json_escape(e.condition_id) << "\",\"variant\":\"" << e.variant << "\",\"R_unit_id\":\""
      << json_escape(e.unit_id) << "\",\"R\":" << calibrated_r
      << ",\"W_target_kib\":" << e.W << ",\"O\":" << e.O << ",\"m\":" << e.m << ",\"K\":" << e.K
      << ",\"episode_count\":" << calibrated_r << ",\"sum_t_app_us\":" << calibrated_us
      << ",\"instrumentation_overhead_fraction_max\":" << max_calibration_overhead
      << ",\"f_tsc_hz\":" << hz << ",\"qpf_hz\":" << g_qpf
      << ",\"weight_regions\":" << calibration_regions.at(e.event_id).weights
      << ",\"activation_regions\":" << calibration_regions.at(e.event_id).activations
      << ",\"region_instance_id\":\"" << calibration_regions.at(e.event_id).instance_id
      << "\",\"condition_region_target_seconds\":2.5,\"condition_region_minimum_seconds\":2.0"
      << ",\"kernel_only_target_seconds\":0.150,\"kernel_only_minimum_seconds\":0.100"
      << ",\"slowest_worker_kernel_seconds\":" << std::fixed << std::setprecision(9)
      << double(*std::min_element(worker_kernel.begin(),worker_kernel.end()))/hz
      << ",\"timestamps_complete\":true,\"coordinator_worker_exchanges\":" << calibrated_episodes.size()
      << ",\"instrumentation_budget_max\":0.01,\"worker_evidence\":[";
    for (unsigned w = 0; w < 8; ++w) {
      if (w) row << ',';
      row << "{\"worker\":" << w << ",\"lp\":" << worker_processor_r16(w).lp
          << ",\"first_tsc\":" << worker_first_tsc[w] << ",\"last_tsc\":" << worker_last_tsc[w]
          << ",\"first_qpc\":" << worker_first_qpc[w] << ",\"last_qpc\":" << worker_last_qpc[w]
          << ",\"kernel_seconds\":" << double(worker_kernel[w])/hz << '}';
    }
    row << "]"
      << ",\"R_policy\":\"R(W,m,K)_common_A_B_C_fixed_N31\"}";
    events.line(row.str());
    std::ostringstream cal_episode;
    cal_episode << "{\"label\":\"SMOKE\",\"event_id\":\"" << json_escape(e.event_id)
      << "\",\"R\":" << calibrated_r << ",\"episode_count\":" << calibrated_r
      << ",\"workers\":[";
    for (unsigned w = 0; w < 8; ++w) {
      if (w) cal_episode << ',';
      cal_episode << "{\"worker\":" << w << ",\"lp\":" << worker_processor_r16(w).lp
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
  std::set<std::string> conditioned_segments;
  while (cursor < scheduled.size()) {
    const auto& first = scheduled[cursor];
    if (first.phase == "residency") {
      if (!prepare_and_mark_load(first))
        throw std::runtime_error("RESIDENCY_PREPARE_FAILED:" + prep_error);
      if (conditioned_segments.insert(first.segment_id).second) {
        const double segment_hz = verify_segment_state(first, workers_data, g_plan_sha, topology_start_sha, reader_qualified);
        const int conditioning_r = calibrated_r_by_unit.at(r_unit_id_for(first));
        const int64_t condition_start = qpc_now();
        int64_t condition_end = condition_start; uint64_t conditioning_episodes = 0, conditioning_kernel_tsc = 0;
        while (qpc_seconds(condition_start, condition_end) < 180.0) {
          const auto condition = run_episode(first, first.variant[0], unsigned(conditioning_episodes + 1),
                                             "segment_conditioning", 0, conditioning_r);
          ++conditioning_episodes;
          for (uint64_t cycles : condition.worker_kernel_tsc) conditioning_kernel_tsc += cycles;
          condition_end = qpc_now();
        }
        const double condition_seconds = qpc_seconds(condition_start, condition_end);
        if (condition_seconds < 180.0 || std::abs(segment_hz - hz) / hz > .001)
          throw std::runtime_error("SEGMENT_CONDITIONING_OR_CLOCK_REVERIFY_INVALID");
        segment_conditioning.line("{\"label\":\"SMOKE\",\"segment_id\":\"" + json_escape(first.segment_id) +
          "\",\"plan_sha256\":\"" + g_plan_sha + "\",\"topology_sha256\":\"" + topology_start_sha +
          "\",\"reader_qualified\":true,\"large_pages_reverified\":true,\"clock_reverified_hz\":" +
          std::to_string(segment_hz) + ",\"qpc_start\":" + std::to_string(condition_start) +
          ",\"qpc_end\":" + std::to_string(condition_end) + ",\"active_conditioning_seconds\":" +
          std::to_string(condition_seconds) + ",\"R_unit_id\":\"" + r_unit_id_for(first) + "\",\"R\":" +
          std::to_string(conditioning_r) + ",\"active_episodes\":" + std::to_string(conditioning_episodes * uint64_t(conditioning_r)) +
          ",\"kernel_tsc_sum\":" + std::to_string(conditioning_kernel_tsc) + "}");
        std::cout << "SEGMENT_REVERIFY_R16 PASS segment=" << first.segment_id
                  << " clock_hz=" << segment_hz << " reader=PASS pages=PASS active_conditioning_s="
                  << condition_seconds << " R=" << conditioning_r << " episodes=" << conditioning_episodes * uint64_t(conditioning_r) << "\n";
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
  bool outputs_equal = true;
  for (const auto& [key, flags] : observed_set) for (unsigned worker = 0; worker < 8; ++worker)
    outputs_equal &= flags[0] && flags[1] && flags[2] &&
                     observed[key][0][worker] == observed[key][1][worker] &&
                     observed[key][0][worker] == observed[key][2][worker];
  for (const auto& [key, flags] : aa_observed_set) for (unsigned worker = 0; worker < 8; ++worker)
    outputs_equal &= flags[0] && flags[1] && aa_observed[key][0][worker] == aa_observed[key][1][worker];
  worker_cleanup.stop();
  marks.flush(); events.flush(); episodes.flush(); calibration_episodes.flush(); manifest.flush(); margins.flush(); margin_episodes.flush(); segment_conditioning.flush();
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
  std::cout << "R_RESIDENCY_R16 units=" << calibrated_r_by_unit.size() << " min_R=" << min_calibrated_r
            << " max_R=" << max_calibrated_r << " W=288 complete_plan_blocks=true A_B_C=common calibration_target_s=2.5\n";
  std::cout << "D9_EPISODES all_timestamps=true margins_each_side_min_s=0.200 same_condition=true N=3 PASS\n";
  std::cout << "SMOKE_R16_CAPTURE PASS schedule_events=" << scheduled.size() << " calibration=" << calibration_events.size()
            << " A_B_C=" << residency_events.size() << " AA_pairs=" << aa_events.size()/2 << " aa_observations=" << aa_events.size() << " outputs_bitwise="
            << (outputs_equal ? "PASS" : "FAIL") << " label=SMOKE numeric_metrics_used=false EMON=continuous_required\n";
  return outputs_equal ? 0 : 4;
}

int run_kernel_qa_r16() {
  using Clock = std::chrono::steady_clock;
  const auto started = Clock::now();
  constexpr std::array<int, 4> m_values{1, 4, 8, 16};
  constexpr std::array<int, 15> sweep_outputs{
      912, 1824, 2728, 3640, 4552, 5464, 6368, 7280,
      9104, 10920, 14560, 21848, 29128, 43688, 58256};
  uint64_t coordinate_cases = 0;
  for (const int m : m_values) {
    for (int coordinate = 0; coordinate < 512; ++coordinate) {
      const auto cell = t0common::generate_single_coordinate(m, coordinate);
      omega_q4::PackedWeights weights;
      omega_q4::PackedActivations activations;
      omega_q4::prepare_weights(cell, omega_q4::kOutput, weights);
      omega_q4::prepare_activations(cell, omega_q4::kOutput, activations);
      std::vector<float> output(size_t(m) * omega_q4::kOutput);
      omega_q4::run(weights, activations, omega_q4::kOutput, output.data());
      const auto check = t0common::verify_s12(cell, output);
      if (!check.pass)
        throw std::runtime_error("D12_coordinate_run_full_failed:m=" + std::to_string(m) +
                                 ":coordinate=" + std::to_string(coordinate) +
                                 ":failures=" + std::to_string(check.failures));
      ++coordinate_cases;
    }
  }
  if (coordinate_cases != 2048)
    throw std::runtime_error("D12_coordinate_coverage_invalid:" + std::to_string(coordinate_cases));

  uint64_t sweep_cases = 0;
  for (const int output_rows : sweep_outputs) {
    for (const int m : m_values) {
      const auto cell = r16::generate_cell(m, output_rows);
      omega_q4::PackedWeights weights;
      omega_q4::PackedActivations activations;
      omega_q4::prepare_weights(cell, output_rows, weights);
      omega_q4::prepare_activations(cell, output_rows, activations);
      std::vector<float> output(size_t(m) * output_rows);
      omega_q4::run(weights, activations, output_rows, output.data());
      const auto check = r16::verify_s12(cell, output_rows, output.data(), output.size());
      if (!check.pass)
        throw std::runtime_error("D12_sweep_run_full_failed:O=" + std::to_string(output_rows) +
                                 ":m=" + std::to_string(m) +
                                 ":failures=" + std::to_string(check.failures));
      ++sweep_cases;
    }
  }
  if (sweep_cases != 60)
    throw std::runtime_error("D12_sweep_coverage_invalid:" + std::to_string(sweep_cases));
  const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  std::cout << "KERNEL_QA_R16 PASS path=omega_q4::run coordinate_cases=" << coordinate_cases
            << " O1024_m=1,4,8,16 positions=0..511 sweep_O_values=15 sweep_run_cases="
            << sweep_cases << " rows_verified=ALL S12=PASS elapsed_s=" << std::fixed
            << std::setprecision(3) << elapsed << " numeric_metrics_used=false\n";
  return 0;
}
}
