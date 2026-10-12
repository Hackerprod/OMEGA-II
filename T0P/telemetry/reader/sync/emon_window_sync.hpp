#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

namespace emon_sync {

// Offline S-20 analysis for a measured region guarded by same-condition work.
struct WindowInterval { double start_ms = 0, end_ms = 0; };
struct PaddedBlock {
    double pad_start_ms = 0, region_start_ms = 0;
    double region_end_ms = 0, pad_end_ms = 0;
};
struct BlockAnalysis {
    double margin_ini_ms = 0, margin_fin_ms = 0;
    double coverage_lower = 0, max_gap_upper_ms = 0, max_window_ms = 0;
    size_t windows_used = 0, windows_excluded_crossing_pad = 0;
    bool margins_pass = false, duration_pass = false, uncertainty_pass = false;
    bool order_pass = false, coverage_pass = false;
    bool gap_pass = false, count_pass = false, window_pass = false;
    bool qualified = false;
    std::string reason;
};
BlockAnalysis analyze_padded_block(const PaddedBlock& block,
    const std::vector<WindowInterval>& windows, double uncertainty_ms,
    double required_margin_ms = 200.0);

struct Boundary {
    std::string civil_ms;
    uint64_t qpc_detect = 0;
    uint64_t filetime_detect = 0;
    double detection_delay_ms = 0.0;
};

struct Config {
    uint64_t qpc_frequency = 0;
    double alignment_uncertainty_ms = 1.0;
    double max_detection_delay_ms = 0.0;
};

class WindowSync {
public:
    explicit WindowSync(Config config);
    // Feed one complete EMON stdout line. The timestamp line at the head of
    // each group is parsed; all other lines are ignored.
    bool feed_line(const std::string& line, uint64_t qpc_now,
                   uint64_t filetime_utc_now, std::string* error = nullptr);
    void process_stopped();
    // Blocks for a newly observed group boundary. Returns the QPC immediately
    // after detection, along with the civil window marker and measured delay.
    Boundary sync_begin_block(uint32_t timeout_ms = 10000);
    Boundary sync_end_block(uint32_t timeout_ms = 10000);
    std::vector<Boundary> boundaries() const;
    std::string failure() const;
    bool timing_budget_pass() const;
    double endpoint_gap_upper_ms() const;
    static bool parse_civil_ms(const std::string&, uint64_t& filetime_utc,
                               std::string* error = nullptr);

private:
    Boundary wait_boundary(uint32_t timeout_ms);
    Config config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Boundary> pending_;
    std::vector<Boundary> all_;
    std::string last_timestamp_;
    std::string failure_;
    bool stopped_ = false;
};

class EmonPipeReader {
public:
    explicit EmonPipeReader(WindowSync& sync);
    ~EmonPipeReader();
    void start(const std::wstring& emon_exe, const std::wstring& edp_file,
               const std::wstring& working_directory, const std::wstring& raw_stdout);
    void wait();
    DWORD exit_code() const { return exit_code_; }
private:
    void read_loop();
    WindowSync& sync_;
    HANDLE read_pipe_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE raw_ = nullptr;
    std::thread reader_;
    DWORD exit_code_ = STILL_ACTIVE;
};

uint64_t qpc_now();
uint64_t filetime_utc_now();

} // namespace emon_sync
