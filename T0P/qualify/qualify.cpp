#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

static std::wstring quote(const fs::path& p) {
    std::wstring s = p.wstring(), out = L"\"";
    for (wchar_t c : s) { if (c == L'"') out += L'\\'; out += c; }
    return out + L"\"";
}

static std::string sha256(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD obj_size = 0, got = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_size), sizeof(obj_size), &got, 0);
    std::vector<UCHAR> obj(obj_size), bytes(1 << 16), digest(32);
    if (BCryptCreateHash(alg, &hash, obj.data(), obj_size, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(alg, 0); return {}; }
    std::ifstream in(path, std::ios::binary);
    while (in) { in.read(reinterpret_cast<char*>(bytes.data()), bytes.size()); auto n = in.gcount(); if (n && BCryptHashData(hash, bytes.data(), DWORD(n), 0) < 0) break; }
    const bool ok = in.eof() && BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) >= 0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    static const char* h = "0123456789abcdef"; std::string out; out.reserve(64);
    for (UCHAR b : digest) { out += h[b >> 4]; out += h[b & 15]; }
    return out;
}

static DWORD run_child(const fs::path& exe, const std::wstring& args, const fs::path& cwd,
                       const fs::path& log, DWORD_PTR affinity) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 9001;
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = file; si.hStdError = file; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = quote(exe) + L" " + args;
    std::vector<wchar_t> writable(cmd.begin(), cmd.end()); writable.push_back(0);
    BOOL made = CreateProcessW(exe.c_str(), writable.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, cwd.c_str(), &si, &pi);
    CloseHandle(file);
    if (!made) return GetLastError();
    if (!SetProcessAffinityMask(pi.hProcess, affinity)) { TerminateProcess(pi.hProcess, 9002); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 9002; }
    const DWORD_PTR child_previous = SetThreadAffinityMask(pi.hThread, affinity);
    GROUP_AFFINITY child_group{}; DWORD_PTR child_mask = 0, child_system_mask = 0;
    if (!child_previous || !GetThreadGroupAffinity(pi.hThread, &child_group) || child_group.Mask != affinity ||
        !GetProcessAffinityMask(pi.hProcess, &child_mask, &child_system_mask) || child_mask != affinity) {
        TerminateProcess(pi.hProcess, 9004); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 9004;
    }
    ResumeThread(pi.hThread); WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 9003; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return code;
}

static void scrub_metric_fields(const fs::path& log) {
    std::ifstream in(log, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), {});
    const std::regex numeric_metric(R"(((?:max_error_over_B|error_over_B|G_ggml|G_Torch|D_prime_hot|D'_hot|P_instr|GFLOPS|time_ns|duration_ns)=)[^ \r\n]+)");
    s = std::regex_replace(s, numeric_metric, "$1null");
    std::ofstream out(log, std::ios::binary | std::ios::trunc);
    out.write(s.data(), std::streamsize(s.size()));
    if (!out) throw std::runtime_error("no se pudo sanear el log dry-run");
}

static std::vector<std::string> dry_records(const fs::path& log, bool omega) {
    std::ifstream in(log); std::vector<std::string> rows; std::string line;
    const std::regex ref_row(R"(DRY_ROUTE candidate=([A-Za-z0-9_]+) m=([0-9]+) status=(PASS|FAIL))");
    const std::regex omega_row(R"(CELL fixture=([A-Za-z0-9_]+) m=([0-9]+) route=omega_q4 .*S12_pass=([01]))");
    std::smatch m;
    while (std::getline(in, line)) {
        if (!omega && std::regex_search(line, m, ref_row)) {
            rows.push_back("{\"candidate\":\"" + m[1].str() + "\",\"fixture\":\"uniform\",\"m\":" + m[2].str() +
                ",\"core\":null,\"frontier\":\"dry-run-smoke\",\"N\":0,\"samples\":[],\"median\":null,\"IQR\":null,\"MAD\":null,\"status\":\"" + m[3].str() + "\",\"timing_fields\":null}");
        } else if (omega && std::regex_search(line, m, omega_row)) {
            rows.push_back("{\"candidate\":\"omega_q4\",\"fixture\":\"" + m[1].str() + "\",\"m\":" + m[2].str() +
                ",\"core\":null,\"frontier\":\"dry-run-smoke\",\"N\":0,\"samples\":[],\"median\":null,\"IQR\":null,\"MAD\":null,\"status\":\"" +
                (m[3].str() == "1" ? "PASS" : "FAIL") + "\",\"timing_fields\":null}");
        }
    }
    return rows;
}

static bool verify_omega_page_bytes(const fs::path& log) {
    std::ifstream in(log); std::string line; std::size_t rows = 0;
    std::set<std::string> keys;
    const std::regex page_row(R"(PAGE fixture=([A-Za-z0-9_]+) m=([0-9]+) candidate=omega_q4 buffer=([A-Za-z0-9_]+) function=([A-Za-z0-9_]+) alias=([A-Za-z0-9_]+) address=0x[0-9a-fA-F]+ bytes=([0-9]+))");
    std::smatch m;
    while (std::getline(in, line)) if (std::regex_search(line, m, page_row)) {
        const std::string fixture = m[1].str(), buffer = m[3].str();
        const int slots = std::stoi(m[2].str()); const std::size_t bytes = std::stoull(m[6].str());
        std::size_t expected = 0;
        if (buffer == "weights") expected = 294912;
        else if (buffer == "activations") expected = std::size_t(672) * slots;
        else if (buffer == "Y") expected = std::size_t(4096) * slots;
        else if (buffer == "dots") expected = std::size_t(65536) * slots;
        if (expected == 0 || slots != 1 && slots != 4 && slots != 8 && slots != 16 || bytes != expected) return false;
        const std::string key = fixture + "/" + m[2].str() + "/" + buffer + "/" + m[4].str() + "/" + m[5].str();
        if (!keys.insert(key).second) return false;
        ++rows;
    }
    return rows == 212;
}

// Pure collector/statistics model. It is exercised only with explicitly synthetic data.
namespace synthetic {
constexpr int kN = 31;
constexpr double kGgml = .90, kTorch = 1.20, kDPrime = 2.00;
struct Stats { double median, iqr, mad, minimum, maximum; };
static double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    const double pos = q * double(v.size() - 1);
    const auto lo = std::size_t(pos); const auto hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - double(lo));
}
static Stats stats(const std::vector<double>& v) {
    if (v.size() != kN) throw std::runtime_error("N distinto de 31");
    for (double x : v) if (!std::isfinite(x)) throw std::runtime_error("muestra no finita");
    const double med = quantile(v, .5); std::vector<double> dev; dev.reserve(v.size());
    for (double x : v) dev.push_back(std::abs(x - med));
    auto mm = std::minmax_element(v.begin(), v.end());
    return {med, quantile(v,.75)-quantile(v,.25), quantile(dev,.5), *mm.first, *mm.second};
}
struct Cell { std::string route, frontier, core; int m; double time, refTime, macs; bool numerical, page; };
// In synthetic cells, time is the vara's normalized throughput and refTime is OMEGA's;
// their quotient is P_OMEGA/P_ref. These are fixtures only, never acquired timings.
static double ratio(const Cell& c) { return (c.time > 0 && c.refTime > 0) ? c.time / c.refTime : NAN; }
static bool qualified(const Cell& c, double gate) { return c.numerical && c.page && std::isfinite(ratio(c)) && ratio(c) >= gate; }
static const Cell& best_qualified(const std::vector<Cell>& v, double gate) {
    const Cell* best = nullptr;
    for (const auto& c : v) if (qualified(c, gate) && (!best || c.time > best->time)) best = &c;
    if (!best) throw std::runtime_error("sin vara cualificada");
    return *best;
}
struct Auth { std::string executable_hash, protocol_hash; };
static bool auth_matches(const Auth& a, const std::string& exe, const std::string& protocol) {
    return !a.executable_hash.empty() && !a.protocol_hash.empty() && a.executable_hash == exe && a.protocol_hash == protocol;
}
static bool page_complete(std::size_t expected, std::size_t actual) { return expected != 0 && expected == actual; }
static bool exact_coverage(const std::set<std::string>& expected,const std::set<std::string>& actual) {
    return !expected.empty() && expected==actual;
}
static bool seed_present(const std::string& seed) { return !seed.empty(); }
static bool s12_pass(bool finite, bool representation, bool exact_dots, bool tolerance) {
    return finite && representation && exact_dots && tolerance;
}
static double d_prime(double p_hot, double bw) { return .5625 * p_hot / bw; }
static bool plateau(double p12, double p14) {
    return p12 > 0 && p14 > 0 && std::abs(p12-p14) / ((p12+p14)*.5) <= .03;
}
static double rho(double ta1,double ta16,double tb1,double tb16) {
    const double ca=(ta16-ta1)/15., cb=(tb16-tb1)/15.;
    if (!(ca>0 && cb>0)) return NAN;
    return ca/cb;
}
static void expect(bool value, const char* name) { if (!value) throw std::runtime_error(std::string("self-test failed: ")+name); }
static std::vector<double> shuffled_values(double x, std::uint32_t seed) {
    std::vector<double> v(kN); for (int i=0;i<kN;++i) v[i]=x + (i-15)*.0001;
    std::mt19937 rng(seed); std::shuffle(v.begin(),v.end(),rng); return v;
}
static void run(const fs::path& logs) {
    const std::uint32_t seed=0x51A17E2u;
    const auto a=shuffled_values(1.25,seed), b=shuffled_values(1.25,seed);
    expect(a==b,"orden reproducible por semilla");
    auto st=stats(a); expect(st.median==1.25 && st.iqr>0 && st.mad>0,"mediana/IQR/MAD y orden aleatorio");
    Cell pass{"flat","kernel_only","P0",1,.90,1.,524288.,true,true};
    Cell fail{"flat","kernel_only","P0",1,.89999,1.,524288.,true,true};
    expect(qualified(pass,kGgml),"umbral ggml 0.90 inclusivo");
    expect(!qualified(fail,kGgml),"umbral ggml falla debajo");
    pass.time=1.20; pass.refTime=1.; expect(qualified(pass,kTorch),"umbral Torch 1.20 inclusivo");
    pass.time=1.19999; expect(!qualified(pass,kTorch),"umbral Torch falla debajo");
    expect(d_prime(400.,112.5)==2.0,"umbral D-prime 2.00 inclusivo");
    expect(d_prime(399.999,112.5)<2.0,"D-prime falla debajo");
    expect(plateau(100.,103.04568) && !plateau(100.,103.046),"plateau 12 vs 14 al 3 por ciento");
    expect(std::isfinite(rho(10,40,10,70)) && rho(10,40,10,70)==.5,"rho marginal aplicable");
    expect(rho(10,40,10,70)<=.50 && ! (rho(10,40,10,69)<.50),"rho gate marginal 0.50 frontera");
    expect(rho(10,25,10,70)==.25 && rho(10,25,10,70)<=.25,"rho señal fuerte 0.25");
    expect(std::isnan(rho(10,10,10,70)),"rho marginal no positivo inválido");
    const std::vector<Cell> choices{{"flat","kernel_only","P0",1,1.0,.8,1,true,true},
      {"repack","kernel_only","P0",1,1.3,.8,1,true,true},
      {"torch","kernel_only","P0",1,1.4,.8,1,false,true}};
    expect(best_qualified(choices,kGgml).route=="repack","selección mejor vara cualificada");
    expect(page_complete(3,3) && !page_complete(3,2),"cobertura PAGE íntegra/incompleta");
    std::set<std::string> required, observed;
    for (const char* core : {"P0","P1","P2","P3","P4","P5","P6","P7","8P"})
      for (const char* frontier : {"kernel_only","dynamic_plus_kernel"})
       for (int m : {1,4,8,16}) required.insert(std::string(core)+"/"+frontier+"/"+std::to_string(m));
    observed=required; expect(exact_coverage(required,observed),"cobertura celda completa");
    observed.erase("P7/dynamic_plus_kernel/16");
    expect(!exact_coverage(required,observed),"negativa frontera/celda/core ausente");
    expect(!s12_pass(true,true,true,false),"control S-12 fallido");
    expect(!auth_matches({"exe","protocol"},"other","protocol"),"hash de ejecutable AUTHORIZED distinto");
    expect(!seed_present(""),"semilla ausente");
    try { stats(std::vector<double>(30,1.)); throw std::runtime_error("N negativo aceptado"); }
    catch (const std::runtime_error& e) { expect(std::string(e.what())=="N distinto de 31","N distinto de 31 rechazado"); }
    try { (void)best_qualified({Cell{"flat","both","P0",1,100,90,1,true,true}},kGgml); }
    catch (...) { throw; }
    try { (void)best_qualified({},kGgml); throw std::runtime_error("celda/core/frontera ausente aceptada"); }
    catch (const std::runtime_error& e) { expect(std::string(e.what())=="sin vara cualificada","celda/core/frontera ausente rechazada"); }
    std::ofstream j(logs/L"self_test_synthetic.json",std::ios::binary|std::ios::trunc);
    j << "{\n  \"label\":\"SYNTHETIC\",\n  \"seed\":" << seed
      << ",\n  \"N\":31,\n  \"statistics\":{\"median\":" << st.median << ",\"IQR\":" << st.iqr
      << ",\"MAD\":" << st.mad << "},\n  \"g_ggml_boundary\":0.90,\n  \"g_torch_boundary\":1.20,\n  \"D_prime_boundary\":2.00,\n  \"plateau_12_14_max_relative_difference\":0.03,\n  \"rho_marginal_boundary\":0.50,\n  \"negative_cases\":[\"missing_cell_core_frontier\",\"N_not_31\",\"PAGE_incomplete\",\"S12_fail\",\"AUTHORIZED_exe_hash_mismatch\",\"seed_missing\"],\n  \"status\":\"PASS\"\n}\n";
    if (!j) throw std::runtime_error("no se pudo escribir reporte SYNTHETIC");
    std::cout << "SELF_TEST label=SYNTHETIC seed=" << seed << " N=31 stats=PASS order=PASS best_qualified=PASS"
              << " G_ggml_0.90=PASS G_Torch_1.20=PASS D_prime_2.00=PASS plateau_12_14_3pct=PASS rho_0.50_0.25=PASS"
              << " negative_missing_frontier_cell_core=PASS negative_N=PASS negative_PAGE=PASS negative_S12=PASS"
              << " negative_AUTH_hash=PASS negative_seed=PASS report=logs/self_test_synthetic.json\n";
}
}

static void timer_smoke(const fs::path& logs) {
    LARGE_INTEGER freq{}, begin{}, end{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0 || !QueryPerformanceCounter(&begin))
        throw std::runtime_error("QPC no disponible");
    DWORD_PTR pm=0, sm=0; if (!GetProcessAffinityMask(GetCurrentProcess(),&pm,&sm)||!pm)
        throw std::runtime_error("máscara para smoke DUMMY no disponible");
    const DWORD_PTR one=pm & (~pm+1); if (!SetThreadAffinityMask(GetCurrentThread(),one))
        throw std::runtime_error("pinning DUMMY falló");
    GROUP_AFFINITY ga{}; if (!GetThreadGroupAffinity(GetCurrentThread(),&ga)||ga.Mask!=one)
        throw std::runtime_error("pinning DUMMY no verificable");
    constexpr LONGLONG min_ticks=0; // TIMED_REGION_MIN se calibra con objetivo dummy de 100 ms.
    (void)min_ticks;
    std::uint64_t iterations=1024, sink=0; double elapsed=0.;
    for (int attempt=0;attempt<32;++attempt) {
        QueryPerformanceCounter(&begin);
        for (std::uint64_t i=0;i<iterations;++i) sink = sink*1664525u + i + 1013904223u;
        QueryPerformanceCounter(&end);
        elapsed=double(end.QuadPart-begin.QuadPart)/double(freq.QuadPart);
        if (elapsed>=.100) break;
        iterations*=2;
    }
    if (elapsed<.100 || end.QuadPart<=begin.QuadPart || !sink) throw std::runtime_error("calibración TIMED_REGION_MIN DUMMY falló");
    const double ticks_to_seconds=double(end.QuadPart-begin.QuadPart)/double(freq.QuadPart);
    if (!(ticks_to_seconds>=.100 && ticks_to_seconds<60.)) throw std::runtime_error("conversión QPC DUMMY inválida");
    std::ofstream j(logs/L"timer_smoke_dummy.json",std::ios::binary|std::ios::trunc);
    j << "{\n  \"label\":\"DUMMY\",\n  \"payload\":\"integer_loop_only\",\n  \"qpc_frequency_hz\":" << freq.QuadPart
      << ",\n  \"qpc_ticks\":" << (end.QuadPart-begin.QuadPart) << ",\n  \"duration_seconds_dummy\":" << elapsed
      << ",\n  \"iterations\":" << iterations << ",\n  \"affinity_mask\":" << one
      << ",\n  \"affinity_verified\":true,\n  \"TIMED_REGION_MIN_seconds\":0.100,\n  \"status\":\"PASS\"\n}\n";
    if (!j) throw std::runtime_error("no se pudo escribir reporte DUMMY");
    std::cout << "TIMER_SMOKE label=DUMMY qpc=PASS calibration_TIMED_REGION_MIN=PASS affinity=PASS"
              << " duration_seconds_DUMMY=" << elapsed << " iterations=" << iterations << " report=logs/timer_smoke_dummy.json\n";
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2 || (std::wstring(argv[1]) != L"--dry-run" && std::wstring(argv[1]) != L"--official" &&
                          std::wstring(argv[1]) != L"--self-test" && std::wstring(argv[1]) != L"--timer-smoke")) {
            std::wcerr << L"Uso: qualify.exe --dry-run | --self-test | --timer-smoke | --official\n"; return 2;
        }
        const fs::path root = fs::absolute(fs::path(argv[0])).parent_path();
        const fs::path workspace = root.parent_path().parent_path();
        const fs::path obj = root / L"obj", logs = root / L"logs";
        fs::create_directories(logs);
        const std::wstring mode=argv[1];
        if (mode==L"--self-test") { synthetic::run(logs); return 0; }
        if (mode==L"--timer-smoke") { timer_smoke(logs); return 0; }
        const DWORD_PTR process_mask = [] { DWORD_PTR p = 0, s = 0; return GetProcessAffinityMask(GetCurrentProcess(), &p, &s) ? p : DWORD_PTR(0); }();
        if (!process_mask) throw std::runtime_error("no se pudo consultar la máscara de afinidad");
        const DWORD_PTR one_core = process_mask & (~process_mask + 1);
        if (!SetThreadAffinityMask(GetCurrentThread(), one_core)) throw std::runtime_error("SetThreadAffinityMask falló");
        const DWORD_PTR previous_affinity = SetThreadAffinityMask(GetCurrentThread(), one_core);
        GROUP_AFFINITY current_group{};
        if (!previous_affinity || !GetThreadGroupAffinity(GetCurrentThread(), &current_group) || current_group.Mask != one_core)
            throw std::runtime_error("afinidad de dry-run no verificable");

        if (std::wstring(argv[1]) == L"--official") {
            const fs::path auth = root / L"AUTHORIZED.json";
            if (!fs::exists(auth)) { std::wcerr << L"OFFICIAL_BLOCKED: falta AUTHORIZED.json (no se ejecutó ninguna rutina).\n"; return 4; }
            std::ifstream in(auth); std::string s((std::istreambuf_iterator<char>(in)), {});
            const std::string digest = sha256(fs::absolute(argv[0]));
            const std::string protocol_hash=sha256(root/L"QUALIFY_PROTOCOL.md");
            const std::regex exe_field(R"rx("executable_sha256"\s*:\s*"([0-9a-f]{64})")rx");
            const std::regex protocol_field(R"rx("protocol_sha256"\s*:\s*"([0-9a-f]{64})")rx");
            std::smatch em, pm;
            if (!std::regex_search(s,em,exe_field) || em[1].str()!=digest) {
                std::wcerr << L"OFFICIAL_BLOCKED: hash de ejecutable AUTHORIZED.json ausente/no coincide.\n"; return 5;
            }
            if (!std::regex_search(s,pm,protocol_field) || pm[1].str()!=protocol_hash) {
                std::wcerr << L"OFFICIAL_BLOCKED: hash de protocolo AUTHORIZED.json ausente/no coincide.\n"; return 5;
            }
            std::wcerr << L"OFFICIAL_BLOCKED: el recolector oficial completo no está habilitado en este build; no se ejecutó ninguna medición.\n";
            return 6;
        }

        const fs::path ggml = root / L"qualify_ggml_qa.exe";
        const fs::path torch = root / L"qualify_torch_qa.exe";
        const fs::path omega = root / L"qualify_kernel_qa.exe";
        const fs::path omega_controls = root / L"qualify_kernel_controls.exe";
        const fs::path data = workspace / L"T0P" / L"common" / L"data";
        wchar_t old_path[32768]{}; DWORD old_n = GetEnvironmentVariableW(L"PATH", old_path, DWORD(std::size(old_path)));
        std::wstring path = old_n ? std::wstring(old_path, old_n) : L"";
        path = (workspace / L"T0P" / L"ref_ggml" / L"build_llama" / L"bin").wstring() + L";" +
               (workspace.parent_path() / L".venv_cuda" / L"Lib" / L"site-packages" / L"torch" / L"lib").wstring() + L";" + path;
        if (!SetEnvironmentVariableW(L"PATH", path.c_str())) throw std::runtime_error("no se pudo preparar PATH de DLL congeladas");
        SetEnvironmentVariableW(L"OMP_NUM_THREADS", L"1");
        SetEnvironmentVariableW(L"OMP_DYNAMIC", L"FALSE");
        SetEnvironmentVariableW(L"MKL_NUM_THREADS", L"1");
        SetEnvironmentVariableW(L"MKL_DYNAMIC", L"FALSE");
        const DWORD_PTR mask = one_core;
        const DWORD ggml_rc = run_child(ggml, quote(data), root, logs / L"dryrun_ggml.log", mask);
        const DWORD torch_rc = run_child(torch, L"", workspace, logs / L"dryrun_torch.log", mask);
        const DWORD omega_rc = run_child(omega, quote(data), workspace, logs / L"dryrun_omega.log", mask);
        const DWORD omega_controls_rc = run_child(omega_controls, L"", workspace, logs / L"dryrun_kernel_controls.log", mask);
        scrub_metric_fields(logs / L"dryrun_ggml.log");
        scrub_metric_fields(logs / L"dryrun_torch.log");
        scrub_metric_fields(logs / L"dryrun_omega.log");
        scrub_metric_fields(logs / L"dryrun_kernel_controls.log");
        const bool page_sizes_ok = verify_omega_page_bytes(logs / L"dryrun_omega.log");
        const bool pass = ggml_rc == 0 && torch_rc == 0 && omega_rc == 0 && omega_controls_rc == 0 && page_sizes_ok;
        auto records = dry_records(logs / L"dryrun_ggml.log", false);
        auto torch_records = dry_records(logs / L"dryrun_torch.log", false);
        auto omega_records = dry_records(logs / L"dryrun_omega.log", true);
        records.insert(records.end(), torch_records.begin(), torch_records.end());
        records.insert(records.end(), omega_records.begin(), omega_records.end());
        std::ofstream json(logs / L"dryrun_summary.json", std::ios::binary | std::ios::trunc);
        json << "{\n  \"mode\": \"dry-run\",\n  \"timed\": false,\n  \"timing_fields\": null,\n"
             "  \"g_ggml\": null,\n  \"g_torch\": null,\n  \"d_prime_hot\": null,\n  \"p_instr\": null,\n"
             "  \"affinity_mask\": " << mask << ",\n  \"affinity_verified\": true,\n  \"routine_exit_codes\": {\"ggml\": " << ggml_rc
          << ", \"torch\": " << torch_rc << ", \"omega\": " << omega_rc
          << ", \"omega_controls_Q1_Q2\": " << omega_controls_rc << "},\n"
             "  \"page_size_contract\": \"" << (page_sizes_ok ? "PASS" : "FAIL") << "\",\n"
             "  \"routine_records\": [\n";
        for (size_t i = 0; i < records.size(); ++i) json << "    " << records[i] << (i + 1 == records.size() ? "\n" : ",\n");
        json << "  ],\n  \"status\": \"" << (pass && records.size() == 61 ? "PASS" : "FAIL") << "\"\n}\n";
        json.close();
        std::cout << "DRY_RUN timed=false affinity=PASS ggml=" << ggml_rc << " torch=" << torch_rc
                  << " omega=" << omega_rc << " summary=logs/dryrun_summary.json status=" << (pass ? "PASS" : "FAIL") << "\n";
        return pass && records.size() == 61 ? 0 : 10;
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; return 1; }
}
