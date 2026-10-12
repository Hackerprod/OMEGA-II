#define NOMINMAX
#include "harness_r15.hpp"
#include "common.hpp"
#include <windows.h>
#include <process.h>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
int run_collector_entry(const wchar_t* mode, const wchar_t* session_id = nullptr) {
  std::string privilege_status;
  if (!t0common::enable_large_page_privilege(&privilege_status)) {
    std::cerr << "HARNESS_R15_ENTRY HOLD_REAL large_page_privilege=" << privilege_status
              << " measurements_started=false\n";
    return 20;
  }
  std::cout << "LARGE_PAGE_PRIVILEGE=" << privilege_status
            << " enable_large_page_privilege_called=true\n";
  std::wstring module(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (length == 0 || length >= module.size()) {
    std::cerr << "HARNESS_R15_ENTRY REJECT executable_path_unavailable measurements_started=false\n";
    return 20;
  }
  module.resize(length);
  const std::filesystem::path executable(module);
  const auto root = executable.parent_path().parent_path().parent_path().parent_path().parent_path();
  const auto script = root / L"T0P" / L"collector" / L"official_run_r15.py";
  if (!std::filesystem::is_regular_file(script)) {
    std::wcerr << L"HARNESS_R15_ENTRY REJECT orchestrator_missing=" << script.c_str()
               << L" measurements_started=false\n";
    return 20;
  }
  const std::filesystem::path python = L"C:\\Program Files\\Python313\\python.exe";
  if (!std::filesystem::is_regular_file(python)) {
    std::cerr << "HARNESS_R15_ENTRY REJECT python_runtime_missing measurements_started=false\n";
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
  if (_wputenv_s(L"OMEGA_R15_INTEGRATED_LAUNCH", L"1") != 0) {
    std::cerr << "HARNESS_R15_ENTRY REJECT integrated_launch_token_failed measurements_started=false\n";
    return 20;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(python.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                      0, nullptr, root.c_str(), &startup, &process)) {
    std::cerr << "HARNESS_R15_ENTRY REJECT python_launch_failed measurements_started=false error="
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
}

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--residency-smoke-r15")
      return harness::run_residency_smoke_r15(argv[2], argv[3]);
    if (argc == 2 && std::string(argv[1]) == "--qa-r15")
      return harness::run_kernel_qa_r15();
    if (argc == 4 && std::string(argv[1]) == "--official" &&
        std::string(argv[2]) == "--session-id")
      return run_collector_entry(L"--official", std::wstring(argv[3], argv[3] + std::strlen(argv[3])).c_str());
    if (argc == 2 && std::string(argv[1]) == "--synthetic-self-test")
      return run_collector_entry(L"--self-test");
    std::cerr << "HARNESS_R15 usage=--official --session-id <id> | --synthetic-self-test | --qa-r15 | "
                 "--residency-smoke-r15 <new-attempt-dir> <r15-smoke-schedule>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "HARNESS_R15_ERROR " << e.what()
              << " measurements_label=SMOKE numeric_metrics_used=false\n";
    return 1;
  }
}
