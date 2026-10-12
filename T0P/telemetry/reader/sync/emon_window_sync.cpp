#include "emon_window_sync.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <limits>

namespace emon_sync {

BlockAnalysis analyze_padded_block(const PaddedBlock& b,
    const std::vector<WindowInterval>& windows, double u, double required_margin) {
    BlockAnalysis a{};
    a.margin_ini_ms = b.region_start_ms - b.pad_start_ms;
    a.margin_fin_ms = b.pad_end_ms - b.region_end_ms;
    a.duration_pass = b.region_end_ms - b.region_start_ms >= 2000.0;
    a.uncertainty_pass = u >= 0.0 && u <= 1.0;
    a.margins_pass = u >= 0 && required_margin >= 0 &&
        a.margin_ini_ms >= required_margin && a.margin_fin_ms >= required_margin;
    a.order_pass = b.pad_start_ms <= b.region_start_ms &&
        b.region_start_ms < b.region_end_ms && b.region_end_ms <= b.pad_end_ms;
    double previous_start = -std::numeric_limits<double>::infinity();
    for (const auto& w : windows) {
        if (!std::isfinite(w.start_ms) || !std::isfinite(w.end_ms) ||
            w.end_ms <= w.start_ms || w.start_ms <= previous_start) {
            a.order_pass = false;
            a.reason = (w.start_ms == previous_start) ? "duplicate window" : "invalid window order/interval";
            return a;
        }
        previous_start = w.start_ms;
    }
    if (!a.order_pass) { if (a.reason.empty()) a.reason = "invalid block order"; return a; }

    std::vector<std::pair<double,double>> clipped;
    for (const auto& w : windows) {
        if (w.end_ms <= b.region_start_ms || w.start_ms >= b.region_end_ms) continue;
        if (w.start_ms < b.pad_start_ms || w.end_ms > b.pad_end_ms) {
            ++a.windows_excluded_crossing_pad;
            continue;
        }
        clipped.emplace_back(std::max(w.start_ms, b.region_start_ms),
                             std::min(w.end_ms, b.region_end_ms));
        a.max_window_ms = std::max(a.max_window_ms, w.end_ms - w.start_ms);
    }
    a.windows_used = clipped.size();
    const double span = b.region_end_ms - b.region_start_ms;
    std::vector<std::pair<double,double>> united;
    for (const auto& x : clipped) {
        if (united.empty() || x.first > united.back().second)
            united.push_back(x);
        else
            united.back().second = std::max(united.back().second, x.second);
    }
    double covered = 0.0, cursor = b.region_start_ms, max_gap = 0.0;
    for (const auto& x : united) {
        const double gap = x.first - cursor;
        max_gap = std::max(max_gap, gap + (cursor == b.region_start_ms ? u : 2*u));
        covered += std::max(0.0, x.second - x.first - 2*u);
        cursor = x.second;
    }
    max_gap = std::max(max_gap, b.region_end_ms - cursor + u);
    a.coverage_lower = span > 0 ? std::min(1.0, covered / span) : 0.0;
    a.max_gap_upper_ms = max_gap;
    a.coverage_pass = a.coverage_lower >= .95;
    a.gap_pass = a.max_gap_upper_ms <= 20.0;
    a.count_pass = a.windows_used >= 20;
    a.window_pass = a.max_window_ms <= 100.0;
    a.qualified = a.margins_pass && a.duration_pass && a.uncertainty_pass && a.order_pass && a.coverage_pass &&
        a.gap_pass && a.count_pass && a.window_pass;
    if (!a.margins_pass) a.reason = "same-condition margin below required minimum";
    else if (!a.duration_pass) a.reason = "measured region below 2 seconds";
    else if (!a.uncertainty_pass) a.reason = "uncertainty above 1 ms";
    else if (a.windows_excluded_crossing_pad) a.reason = "windows crossing pad boundary excluded";
    else if (!a.coverage_pass) a.reason = "coverage below 95 percent";
    else if (!a.gap_pass) a.reason = "gap above 20 ms";
    else if (!a.count_pass) a.reason = "fewer than 20 complete windows";
    else if (!a.window_pass) a.reason = "window above 100 ms";
    else a.reason = "PASS";
    return a;
}

uint64_t qpc_now() { LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return static_cast<uint64_t>(v.QuadPart); }
uint64_t filetime_utc_now() { FILETIME v{}; GetSystemTimePreciseAsFileTime(&v); return (uint64_t(v.dwHighDateTime) << 32) | v.dwLowDateTime; }

static bool is_timestamp(const std::string& s) {
    return s.size() >= 23 && s[2] == '/' && s[5] == '/' && s[10] == ' ' &&
           s[13] == ':' && s[16] == ':' && s[19] == '.' &&
           std::isdigit((unsigned char)s[0]) && std::isdigit((unsigned char)s[1]);
}

bool WindowSync::parse_civil_ms(const std::string& s, uint64_t& out, std::string* error) {
    if (!is_timestamp(s)) return false;
    int mo=0, day=0, year=0, hh=0, mm=0, ss=0, ms=0;
    if (std::sscanf(s.c_str(), "%d/%d/%d %d:%d:%d.%d", &mo,&day,&year,&hh,&mm,&ss,&ms) != 7 ||
        mo<1 || mo>12 || day<1 || day>31 || hh>23 || mm>59 || ss>60 || ms>999) {
        if (error) *error = "invalid civil timestamp";
        return false;
    }
    std::tm t{}; t.tm_mon=mo-1; t.tm_mday=day; t.tm_year=year-1900; t.tm_hour=hh; t.tm_min=mm; t.tm_sec=ss; t.tm_isdst=-1;
    const std::time_t local = std::mktime(&t);
    if (local == static_cast<std::time_t>(-1)) { if (error) *error="civil timestamp conversion failed"; return false; }
    constexpr uint64_t epoch_delta = 116444736000000000ull;
    out = epoch_delta + (uint64_t(local) * 1000ull + uint64_t(ms)) * 10000ull;
    return true;
}

WindowSync::WindowSync(Config c) : config_(c) {
    if (!config_.qpc_frequency) { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); config_.qpc_frequency=f.QuadPart; }
    if (!config_.qpc_frequency || config_.alignment_uncertainty_ms < 0 || config_.max_detection_delay_ms < 0)
        throw std::invalid_argument("invalid sync timing configuration");
}

bool WindowSync::feed_line(const std::string& raw, uint64_t qpc, uint64_t ft, std::string* error) {
    std::string s=raw; while (!s.empty() && (s.back()=='\r'||s.back()=='\n'||s.back()==' '||s.back()=='\t')) s.pop_back();
    if (!is_timestamp(s)) return false;
    uint64_t marked=0; std::string parse_error;
    if (!parse_civil_ms(s, marked, &parse_error)) { if(error)*error=parse_error; return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!last_timestamp_.empty() && s <= last_timestamp_) {
        failure_ = (s == last_timestamp_) ? "duplicate civil timestamp" : "out-of-order civil timestamp";
        if(error)*error=failure_; cv_.notify_all(); return false;
    }
    last_timestamp_=s;
    const double delay = (double(int64_t(ft)-int64_t(marked)) / 10000.0);
    Boundary b{s,qpc,ft,delay}; all_.push_back(b); pending_.push_back(b); cv_.notify_all();
    return true;
}

void WindowSync::process_stopped() { std::lock_guard<std::mutex> lock(mutex_); stopped_=true; cv_.notify_all(); }

Boundary WindowSync::wait_boundary(uint32_t timeout_ms) {
    const uint64_t call_qpc=qpc_now();
    std::unique_lock<std::mutex> lock(mutex_);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    for(;;){
        if(!failure_.empty())throw std::runtime_error(failure_);
        while(!pending_.empty() && pending_.front().qpc_detect<=call_qpc)pending_.pop_front();
        if(!pending_.empty()){Boundary b=pending_.front();pending_.pop_front();return b;}
        if(stopped_)throw std::runtime_error("EMON process stopped before next window boundary");
        if(cv_.wait_until(lock,deadline)==std::cv_status::timeout)throw std::runtime_error("next EMON window boundary not observed before timeout");
    }
}
Boundary WindowSync::sync_begin_block(uint32_t t) { return wait_boundary(t); }
Boundary WindowSync::sync_end_block(uint32_t t) { return wait_boundary(t); }
std::vector<Boundary> WindowSync::boundaries() const { std::lock_guard<std::mutex> lock(mutex_); return all_; }
std::string WindowSync::failure() const { std::lock_guard<std::mutex> lock(mutex_); return failure_; }
bool WindowSync::timing_budget_pass() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !all_.empty() && (config_.alignment_uncertainty_ms + config_.max_detection_delay_ms <= 20.0) &&
        std::all_of(all_.begin(),all_.end(),[&](const Boundary& b){return b.detection_delay_ms <= config_.max_detection_delay_ms;});
}
double WindowSync::endpoint_gap_upper_ms() const { std::lock_guard<std::mutex> lock(mutex_); double m=0; for(const auto& b:all_)m=std::max(m,b.detection_delay_ms); return m+config_.alignment_uncertainty_ms; }

EmonPipeReader::EmonPipeReader(WindowSync& s):sync_(s){}
EmonPipeReader::~EmonPipeReader(){try{wait();}catch(...){}if(read_pipe_)CloseHandle(read_pipe_);if(process_)CloseHandle(process_);if(raw_)CloseHandle(raw_);}
void EmonPipeReader::start(const std::wstring& exe,const std::wstring& edp,const std::wstring& wd,const std::wstring& rawpath){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd=nullptr,wr=nullptr;
    if(!CreatePipe(&rd,&wr,&sa,0))throw std::runtime_error("CreatePipe failed");SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
    raw_=CreateFileW(rawpath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(raw_==INVALID_HANDLE_VALUE){raw_=nullptr;CloseHandle(rd);CloseHandle(wr);throw std::runtime_error("EMON raw stdout exists or cannot be created");}
    HANDLE err=CreateFileW((rawpath+L".stderr").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(err==INVALID_HANDLE_VALUE){CloseHandle(rd);CloseHandle(wr);throw std::runtime_error("EMON stderr file exists or cannot be created");}
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=wr;si.hStdError=err;
    PROCESS_INFORMATION pi{};std::wstring cmd=L"\""+exe+L"\" -collect-edp \"edp_file="+edp+L"\"";
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,wd.c_str(),&si,&pi)){DWORD e=GetLastError();CloseHandle(rd);CloseHandle(wr);CloseHandle(err);throw std::runtime_error("CreateProcess EMON failed Win32="+std::to_string(e));}
    CloseHandle(wr);CloseHandle(err);CloseHandle(pi.hThread);read_pipe_=rd;process_=pi.hProcess;reader_=std::thread(&EmonPipeReader::read_loop,this);
}
void EmonPipeReader::read_loop(){
    std::string line;char ch=0;DWORD got=0;
    while(ReadFile(read_pipe_,&ch,1,&got,nullptr)&&got){DWORD written=0;WriteFile(raw_,&ch,1,&written,nullptr);if(ch=='\n'){std::string e;sync_.feed_line(line,qpc_now(),filetime_utc_now(),&e);line.clear();}else if(ch!='\r')line.push_back(ch);}
    if(!line.empty()){std::string e;sync_.feed_line(line,qpc_now(),filetime_utc_now(),&e);}
    sync_.process_stopped();
}
void EmonPipeReader::wait(){if(reader_.joinable())reader_.join();if(process_){WaitForSingleObject(process_,INFINITE);GetExitCodeProcess(process_,&exit_code_);}}

} // namespace emon_sync
