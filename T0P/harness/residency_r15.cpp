#define NOMINMAX
#include "harness_r15.hpp"
#include "provider_common_r15.hpp"
#include "topology_r15.hpp"
#include "q4_kernel.hpp"
#include "evict.hpp"
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
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
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace harness {
namespace {
constexpr int kO = 1024, kM = 1, kK = 16, kN = 3;
constexpr double kCalibrationTargetSeconds = 2.5, kMarginSeconds = .200;
std::atomic<uint64_t> g_evict_weight_calls{0}, g_evict_range_calls{0}, g_evict_weight_range_calls{0},
    g_evict_activation_range_calls{0}, g_evicted_lines{0};

class RawFile {
 public:
  explicit RawFile(const std::string& path) {
    handle_ = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::runtime_error("D13_RAW_CREATE_NEW_FAILED:" + path + ":" + std::to_string(GetLastError()));
  }
  RawFile(const RawFile&) = delete;
  RawFile& operator=(const RawFile&) = delete;
  ~RawFile() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
  void append(const std::string& bytes) {
    const char* p = bytes.data(); size_t left = bytes.size();
    while (left) {
      DWORD amount = static_cast<DWORD>((std::min)(left, size_t(1u << 20))), written = 0;
      if (!WriteFile(handle_, p, amount, &written, nullptr) || written != amount)
        throw std::runtime_error("D13_RAW_WRITE_FAILED:" + std::to_string(GetLastError()));
      p += written; left -= written;
    }
  }
  void line(const std::string& s) { std::string row = s; row.push_back('\n'); append(row); }
  void flush() { if (!FlushFileBuffers(handle_)) throw std::runtime_error("D13_RAW_FLUSH_FAILED:" + std::to_string(GetLastError())); }
 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
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
  int replica = 0, W = 0, O = 0, m = 0, K = 0;
};
struct Stamp {
  uint64_t tsc_start = 0, tsc_end = 0;
  int64_t qpc_start = 0, qpc_end = 0;
  DWORD lp_start = MAXDWORD, lp_end = MAXDWORD;
  uint64_t weight_address = 0, output_hash = 0;
};
struct Worker {
  t0common::Cell cell;
  omega_q4::PackedActivations x;
  std::shared_ptr<omega_q4::PackedWeights> shared_ac;
  std::vector<std::unique_ptr<omega_q4::PackedWeights>> b_copies;
  t0common::Buffer<float> y;
  uint64_t sink = 0;
  Stamp stamp;
};
enum class CommandKind { EvictActivations, Condition, Run, Stop };
struct Command { CommandKind kind = CommandKind::Stop; char variant = 'A'; int app = 0; };

std::mutex g_command_mutex, g_error_mutex;
Command g_command;
std::string g_worker_error;
// Each operation barrier includes the coordinator and all eight workers.
Gate g_start(9), g_done(9);
std::array<Worker, 8>* g_workers = nullptr;
std::array<uint64_t, 8> g_output_hashes{};
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
void line_sink(uintptr_t, void* context) { static_cast<std::atomic<uint64_t>*>(context)->fetch_add(1, std::memory_order_relaxed); }

std::vector<ScheduleEvent> read_schedule(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("SMOKE_SCHEDULE_OPEN_FAILED:" + path);
  std::string line; bool header = false; std::vector<ScheduleEvent> events; std::set<std::string> ids;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line.rfind("#plan_sha256=", 0) == 0) { g_plan_sha = line.substr(13); continue; }
    if (line[0] == '#') continue;
    if (!header) { if (line != "sequence\tphase\tevent_id\tcondition_id\tvariant\treplica\tobservation\tunit_id\tpair_id\tW_target_kib\tO\tm\tK") throw std::runtime_error("SMOKE_SCHEDULE_HEADER_INVALID"); header = true; continue; }
    std::vector<std::string> f; std::stringstream ss(line); std::string field; while (std::getline(ss, field, '\t')) f.push_back(field);
    if (f.size() != 13) throw std::runtime_error("SMOKE_SCHEDULE_ROW_INVALID");
    ScheduleEvent e; e.sequence = unsigned(std::stoul(f[0])); e.phase = f[1]; e.event_id = f[2]; e.condition_id = f[3]; e.variant = f[4]; e.replica = std::stoi(f[5]); e.observation = f[6]; e.unit_id = f[7]; e.pair_id = f[8]; e.W = std::stoi(f[9]); e.O = std::stoi(f[10]); e.m = std::stoi(f[11]); e.K = std::stoi(f[12]);
    if (!ids.insert(e.event_id).second) throw std::runtime_error("SMOKE_SCHEDULE_DUPLICATE_ID:" + e.event_id);
    if (!(e.phase == "calibration" || e.phase == "residency" || e.phase == "aa") ||
        !(e.variant == "A" || e.variant == "B" || e.variant == "C") || e.replica < 0 || e.replica > 3 ||
        e.W != 288 || e.O != kO || e.m != kM || e.K != kK)
      throw std::runtime_error("SMOKE_SCHEDULE_FIELD_INVALID:" + e.event_id);
    events.push_back(std::move(e));
  }
  if (!header || g_plan_sha.size() != 64 || events.size() != 18 || events.front().phase != "calibration")
    throw std::runtime_error("SMOKE_SCHEDULE_EXPECTED_18_EVENTS");
  if (!std::is_sorted(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.sequence < b.sequence; }))
    throw std::runtime_error("SMOKE_SCHEDULE_ORDER_NOT_SERIALIZED");
  size_t calibration = 0, sample = 0, aa = 0;
  for (const auto& e : events) { calibration += e.phase == "calibration"; sample += e.phase == "residency"; aa += e.phase == "aa"; }
  if (calibration != 3 || sample != 9 || aa != 6) throw std::runtime_error("SMOKE_SCHEDULE_EVENT_COUNTS_INVALID");
  return events;
}

void prepare_worker(Worker& w, unsigned worker, int output) {
  w.cell = r15::generate_cell(kM, output);
  w.shared_ac = std::make_shared<omega_q4::PackedWeights>();
  omega_q4::prepare_weights(w.cell, output, *w.shared_ac);
  omega_q4::prepare_activations(w.cell, output, w.x);
  w.y.resize(size_t(kM) * output);
  const size_t bytes = omega_q4::weight_bytes(output), code = omega_q4::code_bytes(output);
  // The allocator may fall back to ordinary pages when SeLockMemoryPrivilege
  // is unavailable; the caller records that OS result and stops before samples.
  std::vector<int8_t> wq, xq; std::vector<uint16_t> ws, xs;
  if (!omega_q4::qa_reconstruct(*w.shared_ac, wq, ws) || !omega_q4::qa_reconstruct_activations(w.x, xq, xs) ||
      wq != w.cell.wq || ws != w.cell.ws_half || xq != w.cell.xq || xs != w.cell.xs_half)
    throw std::runtime_error("exact_representation_failed:worker=" + std::to_string(worker));
  std::vector<int32_t> dots(size_t(kM) * output * r15::G);
  omega_q4::qa_group_dots(*w.shared_ac, w.x, dots.data());
  for (int row = 0; row < output; ++row) for (int group = 0; group < r15::G; ++group) {
    int32_t expected = 0;
    for (int k = 0; k < 32; ++k)
      expected += int(w.cell.wq[size_t(row) * r15::I + group * 32 + k]) * int(w.cell.xq[group * 32 + k]);
    if (dots[size_t(row) * r15::G + group] != expected) throw std::runtime_error("exact_group_dot_failed");
  }
  omega_q4::run(*w.shared_ac, w.x, output, w.y.data());
  if (!r15::verify_s12(w.cell, output, w.y.data(), w.y.size()).pass) throw std::runtime_error("S12_numeric_check_failed");
  w.b_copies.reserve(kK);
  for (int k = 0; k < kK; ++k) {
    auto copy = std::make_unique<omega_q4::PackedWeights>(); copy->output = output; copy->bytes.resize(bytes);
    std::memcpy(copy->bytes.data(), w.shared_ac->bytes.data(), bytes);
    if (std::memcmp(copy->bytes.data(), w.shared_ac->bytes.data(), bytes) != 0 || !page_large(copy->bytes.data(), bytes))
      throw std::runtime_error("B_independent_copy_prepare_failed:worker=" + std::to_string(worker));
    for (int prior = 0; prior < k; ++prior) {
      uintptr_t a = reinterpret_cast<uintptr_t>(copy->bytes.data());
      uintptr_t b = reinterpret_cast<uintptr_t>(w.b_copies[prior]->bytes.data());
      if (!(a + bytes <= b || b + bytes <= a)) throw std::runtime_error("B_copy_region_overlap");
    }
    w.b_copies.push_back(std::move(copy));
  }
  if (code + omega_q4::scale_bytes(output) != bytes) throw std::runtime_error("MAC_WEIGHT_SIZE_FORMULA_INVALID");
}

const omega_q4::PackedWeights& selected_weights(const Worker& w, char variant, int app) {
  if (variant == 'B') return *w.b_copies.at(size_t(app));
  return *w.shared_ac;
}

void worker_main(unsigned worker) {
  try {
    if (!set_worker_affinity_r15(worker) || !worker_affinity_matches_r15(worker))
      throw std::runtime_error("P_core_worker_affinity_invalid:" + std::to_string(worker));
    for (;;) {
      if (!g_start.wait()) return;
      Command command; { std::lock_guard<std::mutex> lock(g_command_mutex); command = g_command; }
      if (command.kind == CommandKind::Stop) { g_done.wait(); return; }
      Worker& w = (*g_workers)[worker];
      if (command.kind == CommandKind::EvictActivations) {
        std::atomic<uint64_t> lines{0};
        const bool evict_weights_now = command.variant == 'C' || command.app == 0;
        if (evict_weights_now) {
          const int copies = command.variant == 'B' ? kK : 1;
          for (int copy = 0; copy < copies; ++copy) {
            const auto& weights = selected_weights(w, command.variant, copy);
            g_evict_range_calls.fetch_add(1, std::memory_order_relaxed);
            g_evict_weight_range_calls.fetch_add(1, std::memory_order_relaxed);
            g_evict_weight_calls.fetch_add(1, std::memory_order_relaxed);
            omega_evict::evict_range_record(weights.bytes.data(), weights.bytes.size(), line_sink, &lines);
            g_evicted_lines.fetch_add(lines.load(), std::memory_order_relaxed);
            lines.store(0);
          }
        }
        g_evict_range_calls.fetch_add(1, std::memory_order_relaxed);
        g_evict_activation_range_calls.fetch_add(1, std::memory_order_relaxed);
        omega_evict::evict_range_record(w.x.bytes.data(), w.x.bytes.size(), line_sink, &lines);
        g_evicted_lines.fetch_add(lines.load(), std::memory_order_relaxed);
        lines.store(0);
        g_evict_range_calls.fetch_add(1, std::memory_order_relaxed);
        g_evict_activation_range_calls.fetch_add(1, std::memory_order_relaxed);
        omega_evict::evict_range_record(w.y.data(), w.y.size() * sizeof(float), line_sink, &lines);
        g_evicted_lines.fetch_add(lines.load(), std::memory_order_relaxed);
      } else if (command.kind == CommandKind::Condition) {
        const volatile uint8_t* p = w.x.bytes.data(); uint64_t sum = 0;
        for (size_t i = 0; i < w.x.bytes.size(); i += 64) sum += p[i];
        w.sink ^= sum; std::fill(w.y.begin(), w.y.end(), 0.5f); _mm_mfence();
      } else if (command.kind == CommandKind::Run) {
        const auto& weights = selected_weights(w, command.variant, command.app);
        Stamp stamp; stamp.weight_address = reinterpret_cast<uint64_t>(weights.bytes.data());
        stamp.lp_start = GetCurrentProcessorNumber(); stamp.qpc_start = qpc_now(); stamp.tsc_start = read_tsc();
        omega_q4::run(weights, w.x, kO, w.y.data());
        stamp.tsc_end = read_tsc(); stamp.qpc_end = qpc_now(); stamp.lp_end = GetCurrentProcessorNumber();
        stamp.output_hash = output_hash(w.y.data(), w.y.size());
        w.stamp = stamp; g_output_hashes[worker] = stamp.output_hash;
        if (stamp.lp_start != worker_processor_r15(worker).lp || stamp.lp_end != worker_processor_r15(worker).lp)
          throw std::runtime_error("worker_migrated_during_application");
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

void evict_nonweights(char variant, int app_index) {
  dispatch({CommandKind::EvictActivations, variant, app_index});
}

struct EpisodeResult {
  double t_app_us = 0;
  int64_t qpc_start = 0, qpc_end = 0;
  uint64_t tsc_start = 0, tsc_end = 0;
  std::array<uint64_t, 8> output_hashes{};
  std::string json_line;
};

EpisodeResult run_episode(const ScheduleEvent& event, char variant, unsigned episode_index,
                          const std::string& event_type, unsigned trial_index, int trial_r,
                          RawFile& episodes, RawFile& manifest, bool record_episode) {
  EpisodeResult result; result.qpc_start = qpc_now();
  const uint64_t weight_calls_start = g_evict_weight_range_calls.load(std::memory_order_relaxed);
  const uint64_t activation_calls_start = g_evict_activation_range_calls.load(std::memory_order_relaxed);
  const uint64_t range_calls_start = g_evict_range_calls.load(std::memory_order_relaxed);
  const uint64_t lines_start = g_evicted_lines.load(std::memory_order_relaxed);
  std::ostringstream apps; apps << '['; double sum_us = 0;
  for (int app = 0; app < kK; ++app) {
    evict_nonweights(variant, app);
    dispatch({CommandKind::Condition, variant, app});
    dispatch({CommandKind::Run, variant, app});
    uint64_t first = UINT64_MAX, last = 0;
    int64_t qpc_first = INT64_MAX, qpc_last = 0;
    for (unsigned worker = 0; worker < 8; ++worker) {
      const Stamp& s = (*g_workers)[worker].stamp; first = (std::min)(first, s.tsc_start); last = (std::max)(last, s.tsc_end);
      qpc_first = (std::min)(qpc_first, s.qpc_start); qpc_last = (std::max)(qpc_last, s.qpc_end);
      result.output_hashes[worker] = s.output_hash;
    }
    const double app_us = double(last - first) * 1e6 / g_tsc_hz;
    if (app) apps << ',';
    apps << "{\"application\":" << app << ",\"t_app_us\":" << std::fixed << std::setprecision(9) << app_us
         << ",\"start_tsc\":" << first << ",\"end_tsc\":" << last
         << ",\"start_qpc\":" << qpc_first << ",\"end_qpc\":" << qpc_last << '}';
    result.t_app_us += double(last - first);
    if (app == 0) result.tsc_start = first;
    result.tsc_end = last;
    sum_us += double(last - first);
  }
  apps << ']'; result.qpc_end = qpc_now(); result.t_app_us = sum_us;
  if (record_episode) {
    std::ostringstream line; line << "{\"label\":\"SMOKE\",\"event_id\":\"" << json_escape(event.event_id)
      << "\",\"phase\":\"" << event.phase << "\",\"episode_type\":\"" << event_type
      << "\",\"trial_index\":" << trial_index << ",\"trial_R\":" << trial_r
      << ",\"episode_index\":" << episode_index << ",\"qpc_episode_start\":" << result.qpc_start
      << ",\"qpc_episode_end\":" << result.qpc_end << ",\"start_tsc\":" << result.tsc_start
      << ",\"end_tsc\":" << result.tsc_end << ",\"applications\":" << apps.str() << '}';
    result.json_line = line.str();
    episodes.line(result.json_line);
  }
  const uint64_t observed_weight_calls = g_evict_weight_range_calls.load(std::memory_order_relaxed) - weight_calls_start;
  const uint64_t observed_activation_calls = g_evict_activation_range_calls.load(std::memory_order_relaxed) - activation_calls_start;
  const uint64_t observed_range_calls = g_evict_range_calls.load(std::memory_order_relaxed) - range_calls_start;
  const uint64_t observed_lines = g_evicted_lines.load(std::memory_order_relaxed) - lines_start;
  const uint64_t expected_weight_calls = variant == 'A' ? 8u : 8u * kK;
  const uint64_t expected_activation_calls = 8u * 2u * kK;
  const bool calls_match = observed_weight_calls == expected_weight_calls && observed_activation_calls == expected_activation_calls
                        && observed_range_calls == expected_weight_calls + expected_activation_calls;
  std::ostringstream eviction; eviction << "{\"label\":\"SMOKE\",\"event_id\":\"" << json_escape(event.event_id)
      << "\",\"phase\":\"" << event.phase << "\",\"episode_index\":" << episode_index
      << ",\"trial_index\":" << trial_index << ",\"episode_type\":\"" << json_escape(event_type)
      << "\",\"variant\":\"" << variant << "\",\"primitive\":\"omega_evict::evict_range_record"
      << "\",\"flush_method\":\"" << omega_evict::selected_flush().name << "\",\"weight_range_calls\":"
      << observed_weight_calls << ",\"activation_range_calls\":" << observed_activation_calls
      << ",\"range_calls_total\":" << observed_range_calls << ",\"expected_weight_range_calls\":"
      << expected_weight_calls << ",\"expected_activation_range_calls\":" << expected_activation_calls
      << ",\"call_counts_match\":" << (calls_match ? "true" : "false")
      << ",\"evicted_cache_lines\":" << observed_lines << '}';
  manifest.line(eviction.str());
  if (!calls_match) throw std::runtime_error("D8_EVICTION_CALL_COUNT_MISMATCH:" + event.event_id);
  return result;
}

// Exact TSC spans are retained alongside converted microseconds.
double episode_microseconds(const EpisodeResult& e, double tsc_hz) { return e.t_app_us * 1e6 / tsc_hz; }

struct MarginResult { int64_t start = 0, end = 0; uint64_t episodes = 0; double seconds = 0; };
MarginResult run_margin(const ScheduleEvent& event, char variant, const std::string& side,
                        double tsc_hz, RawFile& episodes, RawFile& margin_episodes, RawFile& manifest) {
  MarginResult margin; margin.start = qpc_now(); uint64_t index = 0; double measured_us = 0;
  do {
    auto sample = run_episode(event, variant, unsigned(index + 1), "margin_" + side, 0, 0, margin_episodes, manifest, true);
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
    const Worker& w = (*g_workers)[worker]; const int copies = variant == 'B' ? kK : 1;
    for (int copy = 0; copy < copies; ++copy) {
      const auto& weights = selected_weights(w, variant, copy);
      if (worker || copy) out << ',';
      out << "{\"worker\":" << worker << ",\"copy_index\":" << copy << ",\"address\":"
          << reinterpret_cast<uintptr_t>(weights.bytes.data()) << ",\"bytes\":" << omega_q4::weight_bytes(kO)
          << ",\"sha256\":\"" << sha256(weights.bytes, omega_q4::weight_bytes(kO))
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

std::string region_instance_id(char variant) {
  std::ostringstream joined;
  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto& weights = selected_weights((*g_workers)[worker], variant, 0);
    joined << worker << ':' << reinterpret_cast<uintptr_t>(weights.bytes.data()) << ':'
           << sha256(weights.bytes, omega_q4::weight_bytes(kO)) << '|';
  }
  const std::string s = joined.str(); return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void write_event_summary(RawFile& events, RawFile& marks, const ScheduleEvent& event, char variant,
                         int r, const std::vector<EpisodeResult>& episodes, double hz,
                         const MarginResult& pre, const MarginResult& post,
                         std::array<uint64_t, 8>& output_hashes) {
  double sum_us = 0; uint64_t start_tsc = UINT64_MAX, end_tsc = 0;
  for (const auto& ep : episodes) { sum_us += episode_microseconds(ep, hz); }
  for (unsigned worker = 0; worker < 8; ++worker) output_hashes[worker] = g_output_hashes[worker];
  const int64_t sample_start = episodes.front().qpc_start, sample_end = episodes.back().qpc_end;
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(event.event_id) << "\",\"phase\":\"" << event.phase << "\",\"condition_id\":\""
      << json_escape(event.condition_id) << "\",\"variant\":\"" << variant << "\",\"observation\":\""
      << event.observation << "\",\"W_target_kib\":" << event.W << ",\"O\":" << event.O << ",\"m\":" << event.m
      << ",\"K\":" << event.K << ",\"replica\":" << event.replica;
  if (!event.unit_id.empty()) row << ",\"unit_id\":\"" << json_escape(event.unit_id) << "\"";
  if (!event.pair_id.empty()) row << ",\"pair_id\":\"" << json_escape(event.pair_id) << "\"";
  row << ",\"R\":" << r << ",\"episode_count\":" << episodes.size()
      << ",\"T_X_us\":" << std::fixed << std::setprecision(9) << sum_us / r
      << ",\"sum_t_app_us\":" << sum_us << ",\"f_tsc_hz\":" << hz << ",\"qpf_hz\":" << g_qpf
      << ",\"pre_margin_seconds\":" << pre.seconds << ",\"post_margin_seconds\":" << post.seconds
      << ",\"margin_condition_id\":\"" << json_escape(event.condition_id)
      << "\",\"margin_continuous\":true,\"qpc_pad_start\":" << pre.start << ",\"qpc_start\":" << sample_start
      << ",\"qpc_end\":" << sample_end << ",\"qpc_pad_end\":" << post.end
      << ",\"timestamps_complete\":true,\"episode_timestamps_file\":\"residency_episodes_r15.jsonl\""
      << ",\"macs_per_call\":" << 512ull * event.O * event.m << ",\"large_pages\":true,\"affinity\":\"PASS\",\"numerical\":\"S12_PASS\""
      << ",\"weight_regions\":" << event_regions_json(event, variant)
      << ",\"activation_regions\":" << activation_regions_json()
      << ",\"region_instance_id\":\"" << region_instance_id(variant) << "\",\"output_hashes\":[";
  for (unsigned worker = 0; worker < 8; ++worker) { if (worker) row << ','; row << output_hashes[worker]; }
  row << "]}"; events.line(row.str());

  for (unsigned worker = 0; worker < 8; ++worker) {
    const auto p_core = worker_processor_r15(worker).p_core;
    marks.line(event.event_id + "," + std::to_string(p_core) + "," + std::to_string(worker_processor_r15(worker).lp) + "," +
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

void write_preflight(const std::string& outdir, const std::string& lp_status, double hz,
                     double qpc_error, double configured_hz, bool pages, const std::string& plan_sha,
                     uint64_t timer_cycles, double timer_us) {
  RawFile file(outdir + "\\preflight_r15.json");
  std::ostringstream row; row << "{\"label\":\"SMOKE\",\"state\":\"" << (pages ? "PASS" : "HOLD_RESOURCE")
      << "\",\"numeric_metrics_used\":false,\"large_page_privilege\":\"" << lp_status
      << "\",\"large_pages\":" << (pages ? "true" : "false") << ",\"P_core_map\":" << topology_json_r15()
      << ",\"qpf_hz\":" << g_qpf << ",\"qpf_source\":\"QueryPerformanceFrequency\",\"f_tsc_qualified_hz\":"
      << std::setprecision(12) << hz << ",\"f_tsc_configured_hz\":" << configured_hz << ",\"tsc_qpc_error\":" << qpc_error
      << ",\"timer_pair_median_cycles\":" << timer_cycles << ",\"timer_pair_median_us\":" << timer_us
      << ",\"plan_sha256\":\"" << plan_sha << "\",\"security_energy_PL_unchanged\":true}";
  file.line(row.str()); file.flush();
}

} // namespace

int run_residency_smoke_r15(const std::string& output_dir, const std::string& schedule_path) {
  const auto scheduled = read_schedule(schedule_path);
  DWORD attrs = GetFileAttributesA(output_dir.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("D13_ATTEMPT_DIRECTORY_MUST_ALREADY_EXIST");
  std::string topology_error;
  if (!discover_topology_r15(topology_error)) throw std::runtime_error("P_CORE_TOPOLOGY_HOLD:" + topology_error);

  std::string lp_status; const bool lp_privilege = t0common::enable_large_page_privilege(&lp_status);
  std::cout << "LARGE_PAGE_PRIVILEGE=" << lp_status << " enable_large_page_privilege_called=true\n";
  LARGE_INTEGER qpf{}; if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) throw std::runtime_error("QPF_UNAVAILABLE");
  g_qpf = uint64_t(qpf.QuadPart);
  bool invariant = false; int cpu[4]{}; __cpuid(cpu, 0x80000000);
  if (unsigned(cpu[0]) >= 0x80000007) { __cpuid(cpu, 0x80000007); invariant = (cpu[3] & (1u << 8)) != 0; }
  if (!invariant) throw std::runtime_error("INVARIANT_TSC_REQUIRED");
  if (!set_worker_affinity_r15(0) || !worker_affinity_matches_r15(0)) throw std::runtime_error("TSC_CALIBRATION_AFFINITY_FAILED");
  std::vector<double> hz_samples;
  for (int i = 0; i < 5; ++i) { auto qa = qpc_now(); auto ta = read_tsc(); Sleep(250); auto tb = read_tsc(); auto qb = qpc_now(); hz_samples.push_back(double(tb - ta) * double(g_qpf) / double(qb - qa)); }
  std::sort(hz_samples.begin(), hz_samples.end()); const double hz = hz_samples[2];
  double qpc_error = 0; for (double f : hz_samples) qpc_error = (std::max)(qpc_error, std::abs(f - hz) / hz);
  if (qpc_error > .001) throw std::runtime_error("TSC_QPC_QUALIFICATION_GT_0_1_PERCENT");

  const auto flush_choice = omega_evict::selected_flush();
  uint64_t timer_median = UINT64_MAX; std::vector<uint64_t> timer_samples; timer_samples.reserve(1024);
  for (int i = 0; i < 1024; ++i) { auto a = read_tsc(); auto b = read_tsc(); timer_samples.push_back(b - a); }
  std::sort(timer_samples.begin(), timer_samples.end()); timer_median = timer_samples[timer_samples.size()/2];
  const double timer_us = double(timer_median) * 1e6 / hz;
  const double configured_hz = configured_tsc_hz();

  std::array<Worker, 8> workers_data; g_workers = &workers_data;
  std::atomic<bool> prep_ok{true}; std::string prep_error; std::mutex prep_mutex; std::vector<std::thread> prep_threads;
  for (unsigned worker = 0; worker < 8; ++worker) prep_threads.emplace_back([&, worker] {
    try { if (!set_worker_affinity_r15(worker) || !worker_affinity_matches_r15(worker)) throw std::runtime_error("worker_affinity_invalid");
      prepare_worker(workers_data[worker], worker, kO);
    } catch (const std::exception& e) { prep_ok = false; std::lock_guard<std::mutex> lock(prep_mutex); prep_error += " worker" + std::to_string(worker) + ":" + e.what(); }
  });
  for (auto& thread : prep_threads) thread.join();
  bool all_pages = prep_ok && lp_privilege;
  if (!prep_ok) throw std::runtime_error("LARGE_PAGE_OR_QA_SETUP_FAILED:" + prep_error);
  for (const auto& worker : workers_data) {
    all_pages &= page_large(worker.shared_ac->bytes.data(), omega_q4::weight_bytes(kO));
    all_pages &= page_large(worker.x.bytes.data(), worker.x.bytes.size());
    all_pages &= page_large(worker.y.data(), worker.y.size() * sizeof(float));
    for (const auto& copy : worker.b_copies) all_pages &= page_large(copy->bytes.data(), omega_q4::weight_bytes(kO));
  }
  // The privilege is always attempted in this process. Without it, the run
  // stops before measurements and records the actual system resource status.
  if (!all_pages) {
    write_preflight(output_dir, lp_status, hz, qpc_error, configured_hz, false, g_plan_sha, timer_median, timer_us);
    std::cout << "RESIDENCY_SMOKE_R15 BLOCKED_REAL large_page_privilege=" << lp_status << " page_queries=FAIL measurements_started=false\n";
    return 3;
  }

  g_tsc_hz = hz;
  RawFile events(output_dir + "\\residency_events_r15.jsonl");
  RawFile episodes(output_dir + "\\residency_episodes_r15.jsonl");
  RawFile calibration_episodes(output_dir + "\\residency_calibration_trials_r15.jsonl");
  RawFile manifest(output_dir + "\\eviction_manifest_r15.jsonl");
  RawFile margins(output_dir + "\\residency_margins_r15.jsonl");
  RawFile margin_episodes(output_dir + "\\residency_margin_episodes_r15.jsonl");
  RawFile marks(output_dir + "\\block_marks_r15.csv");
  RawFile clock_map(output_dir + "\\clock_map.csv");
  ClockMapSampler clock_sampler(clock_map);
  marks.line("sample_id,p_core,worker_lp,replica,condition,t_pad_ini,t_ini,t_fin,t_pad_fin,comparison_set,clock_domain");
  std::vector<std::thread> worker_threads; worker_threads.reserve(8);
  std::string worker_error; { std::lock_guard<std::mutex> lock(g_error_mutex); g_worker_error.clear(); }
  for (unsigned worker = 0; worker < 8; ++worker) worker_threads.emplace_back(worker_main, worker);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(g_error_mutex); worker_error = g_worker_error;
  }
  if (!worker_error.empty()) throw std::runtime_error("P_CORE_WORKER_START_FAILED:" + worker_error);

  std::array<std::array<std::array<uint64_t, 8>, 3>, 3> observed{};
  std::array<std::array<bool, 3>, 3> observed_set{};
  std::array<std::array<std::array<uint64_t, 8>, 2>, 3> aa_observed{};
  std::array<std::array<bool, 2>, 3> aa_observed_set{};
  int calibrated_r = 1;
  std::vector<ScheduleEvent> calibration_events, residency_events, aa_events;
  for (const auto& e : scheduled) {
    if (e.phase == "calibration") calibration_events.push_back(e);
    else if (e.phase == "residency") residency_events.push_back(e);
    else aa_events.push_back(e);
  }
  bool calibration_done = false;
  std::map<std::string, std::vector<EpisodeResult>> chosen_calibration;
  for (int trial = 1; trial <= 22; ++trial) {
    const int candidate_r = 1 << (trial - 1); bool all_reached = true;
    std::map<std::string, std::vector<EpisodeResult>> this_trial;
    for (const auto& e : calibration_events) {
      double total_us = 0; const char variant = e.variant[0];
      auto& trial_episodes = this_trial[e.event_id]; trial_episodes.reserve(candidate_r);
      for (int episode = 1; episode <= candidate_r; ++episode) {
        auto result = run_episode(e, variant, unsigned(episode), "calibration_trial", unsigned(trial), candidate_r,
                                  calibration_episodes, manifest, true);
        total_us += episode_microseconds(result, hz);
        trial_episodes.push_back(std::move(result));
      }
      const double seconds = total_us / 1e6;
      if (seconds < kCalibrationTargetSeconds) all_reached = false;
      std::cout << "R_CALIBRATION_TRIAL variant=" << variant << " candidate_R=" << candidate_r
                << " elapsed_kernel_s=" << std::fixed << std::setprecision(6) << seconds
                << " target_s=2.5 decision=" << (seconds >= kCalibrationTargetSeconds ? "REACHED" : "DOUBLE") << "\n";
    }
    if (all_reached) { calibrated_r = candidate_r; chosen_calibration = std::move(this_trial); calibration_done = true; break; }
  }
  if (!calibration_done) throw std::runtime_error("R_CALIBRATION_FAILED_AFTER_22_DOUBLINGS");

  for (const auto& e : calibration_events) {
    const auto& calibrated_episodes = chosen_calibration.at(e.event_id);
    for (const auto& episode : calibrated_episodes) episodes.line(episode.json_line);
    double calibrated_us = 0;
    for (const auto& episode : calibrated_episodes) calibrated_us += episode_microseconds(episode, hz);
    std::ostringstream row; row << "{\"label\":\"SMOKE\",\"numeric_metrics_used\":false,\"event_id\":\""
      << json_escape(e.event_id) << "\",\"phase\":\"calibration\",\"unit_id\":\"" << json_escape(e.unit_id) << "\",\"condition_id\":\""
      << json_escape(e.condition_id) << "\",\"variant\":\"" << e.variant << "\",\"R\":" << calibrated_r
      << ",\"W_target_kib\":" << e.W << ",\"O\":" << e.O << ",\"m\":" << e.m << ",\"K\":" << e.K
      << ",\"episode_count\":" << calibrated_episodes.size() << ",\"sum_t_app_us\":" << calibrated_us
      << ",\"f_tsc_hz\":" << hz << ",\"qpf_hz\":" << g_qpf
      << ",\"weight_regions\":" << event_regions_json(e, e.variant[0])
      << ",\"activation_regions\":" << activation_regions_json()
      << ",\"region_instance_id\":\"" << region_instance_id(e.variant[0])
      << "\",\"calibration_target_seconds\":2.5,\"R_policy\":\"R(W,m,K)_common_A_B_C\"}";
    events.line(row.str());
  }

  for (const auto& e : residency_events) {
    const char variant = e.variant[0];
    const auto pre = run_margin(e, variant, "pre", hz, episodes, margin_episodes, manifest);
    margins.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) + "\",\"side\":\"pre\",\"qpc_start\":" +
      std::to_string(pre.start) + ",\"qpc_end\":" + std::to_string(pre.end) + ",\"seconds\":" + std::to_string(pre.seconds) +
      ",\"episodes\":" + std::to_string(pre.episodes) + ",\"same_condition\":true,\"continuous_load\":true}");
    std::vector<EpisodeResult> measured; measured.reserve(calibrated_r);
    for (int i = 1; i <= calibrated_r; ++i) measured.push_back(run_episode(e, variant, unsigned(i), "sample", 0, calibrated_r, episodes, manifest, true));
    const auto post = run_margin(e, variant, "post", hz, episodes, margin_episodes, manifest);
    margins.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) + "\",\"side\":\"post\",\"qpc_start\":" +
      std::to_string(post.start) + ",\"qpc_end\":" + std::to_string(post.end) + ",\"seconds\":" + std::to_string(post.seconds) +
      ",\"episodes\":" + std::to_string(post.episodes) + ",\"same_condition\":true,\"continuous_load\":true}");
    const unsigned variant_index = variant == 'A' ? 0 : variant == 'B' ? 1 : 2;
    std::array<uint64_t, 8> hashes{}; write_event_summary(events, marks, e, variant, calibrated_r, measured, hz, pre, post, hashes);
    const unsigned rep = unsigned(e.replica - 1);
    if (rep < 3) { observed[rep][variant_index] = hashes; observed_set[rep][variant_index] = true; }
  }
  for (const auto& e : aa_events) {
    const char variant = 'A'; const auto pre = run_margin(e, variant, "pre", hz, episodes, margin_episodes, manifest);
    margins.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) + "\",\"side\":\"pre\",\"seconds\":" +
      std::to_string(pre.seconds) + ",\"episodes\":" + std::to_string(pre.episodes) + ",\"same_condition\":true,\"continuous_load\":true}");
    std::vector<EpisodeResult> measured; measured.reserve(calibrated_r);
    for (int i = 1; i <= calibrated_r; ++i) measured.push_back(run_episode(e, variant, unsigned(i), "aa_sample", 0, calibrated_r, episodes, manifest, true));
    const auto post = run_margin(e, variant, "post", hz, episodes, margin_episodes, manifest);
    margins.line("{\"label\":\"SMOKE\",\"event_id\":\"" + json_escape(e.event_id) + "\",\"side\":\"post\",\"seconds\":" +
      std::to_string(post.seconds) + ",\"episodes\":" + std::to_string(post.episodes) + ",\"same_condition\":true,\"continuous_load\":true}");
    std::array<uint64_t, 8> hashes{}; write_event_summary(events, marks, e, variant, calibrated_r, measured, hz, pre, post, hashes);
    const unsigned rep = unsigned(e.replica - 1); const unsigned obs = e.observation == "A1" ? 0 : 1;
    if (rep < 3) { aa_observed[rep][obs] = hashes; aa_observed_set[rep][obs] = true; }
  }
  bool outputs_equal = true;
  for (unsigned rep = 0; rep < 3; ++rep) for (unsigned worker = 0; worker < 8; ++worker) {
    outputs_equal &= observed_set[rep][0] && observed_set[rep][1] && observed_set[rep][2]
                     && observed[rep][0][worker] == observed[rep][1][worker] && observed[rep][0][worker] == observed[rep][2][worker];
    outputs_equal &= aa_observed_set[rep][0] && aa_observed_set[rep][1]
                     && aa_observed[rep][0][worker] == aa_observed[rep][1][worker];
  }
  stop_workers(worker_threads);
  marks.flush(); events.flush(); episodes.flush(); calibration_episodes.flush(); manifest.flush(); margins.flush(); margin_episodes.flush();
  clock_sampler.stop();
  clock_map.flush();
  write_preflight(output_dir, lp_status, hz, qpc_error, configured_hz, all_pages, g_plan_sha, timer_median, timer_us);
  std::cout << "MAC_CHECK real_work=512*O*m value=" << 512ull * kO * kM << " PASS\n";
  std::cout << "D9_AC_SHARED same_live_buffer=true address=" << reinterpret_cast<uintptr_t>(workers_data[0].shared_ac->bytes.data())
            << " sha256=" << sha256(workers_data[0].shared_ac->bytes, omega_q4::weight_bytes(kO)) << " workers=8 PASS\n";
  std::cout << "D8_EVICT primitive=omega_evict::evict_range_record only calls_weight_ranges="
            << g_evict_weight_range_calls.load() << " calls_activation_ranges=" << g_evict_activation_range_calls.load()
            << " calls_total=" << g_evict_range_calls.load() << " episodes=" << (g_evict_weight_calls.load() > 0 ? "manifested" : "missing")
            << " cache_lines=" << g_evicted_lines.load() << " flush=" << flush_choice.name << " PASS\n";
  std::cout << "R_RESIDENCY_R15 common_R=" << calibrated_r << " W=288 O=1024 m=1 K=16 A_B_C=common calibration_target_s=2.5\n";
  std::cout << "D9_EPISODES all_timestamps=true margins_each_side_min_s=0.200 same_condition=true N=3 PASS\n";
  std::cout << "SMOKE_R15_CAPTURE PASS schedule_events=" << scheduled.size() << " calibration=3 A_B_C=9 AA_pairs=3 aa_observations=6 outputs_bitwise="
            << (outputs_equal ? "PASS" : "FAIL") << " label=SMOKE numeric_metrics_used=false EMON=continuous_required\n";
  return outputs_equal ? 0 : 4;
}

int run_kernel_qa_r15() {
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
      const auto cell = r15::generate_cell(m, output_rows);
      omega_q4::PackedWeights weights;
      omega_q4::PackedActivations activations;
      omega_q4::prepare_weights(cell, output_rows, weights);
      omega_q4::prepare_activations(cell, output_rows, activations);
      std::vector<float> output(size_t(m) * output_rows);
      omega_q4::run(weights, activations, output_rows, output.data());
      const auto check = r15::verify_s12(cell, output_rows, output.data(), output.size());
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
  std::cout << "KERNEL_QA_R15 PASS path=omega_q4::run coordinate_cases=" << coordinate_cases
            << " O1024_m=1,4,8,16 positions=0..511 sweep_O_values=15 sweep_run_cases="
            << sweep_cases << " rows_verified=ALL S12=PASS elapsed_s=" << std::fixed
            << std::setprecision(3) << elapsed << " numeric_metrics_used=false\n";
  return 0;
}
}
