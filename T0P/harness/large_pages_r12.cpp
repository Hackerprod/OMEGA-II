#define NOMINMAX
#include "large_pages_r12.hpp"
#include "common.hpp"
#include <windows.h>
#include <vector>

namespace harness {
bool enable_pages_only_if_pre_enabled_r12(std::string& status) {
  HANDLE token=nullptr;
  if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
    status="HOLD OpenProcessToken error="+std::to_string(GetLastError()); return false;
  }
  LUID luid{};
  if(!LookupPrivilegeValueW(nullptr,L"SeLockMemoryPrivilege",&luid)) {
    status="HOLD LookupPrivilegeValue error="+std::to_string(GetLastError()); CloseHandle(token); return false;
  }
  DWORD needed=0; GetTokenInformation(token,TokenPrivileges,nullptr,0,&needed);
  if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||!needed) {
    status="HOLD TokenPrivileges size error="+std::to_string(GetLastError()); CloseHandle(token); return false;
  }
  std::vector<unsigned char> data(needed);
  if(!GetTokenInformation(token,TokenPrivileges,data.data(),needed,&needed)) {
    status="HOLD TokenPrivileges error="+std::to_string(GetLastError()); CloseHandle(token); return false;
  }
  CloseHandle(token);
  auto* privileges=reinterpret_cast<TOKEN_PRIVILEGES*>(data.data());
  bool already_enabled=false;
  for(DWORD i=0;i<privileges->PrivilegeCount;++i) {
    const auto& p=privileges->Privileges[i];
    if(p.Luid.LowPart==luid.LowPart&&p.Luid.HighPart==luid.HighPart)
      already_enabled=(p.Attributes&SE_PRIVILEGE_ENABLED)!=0;
  }
  if(!already_enabled) { status="HOLD SeLockMemoryPrivilege not already enabled; security unchanged"; return false; }
  // The common allocator needs its process-local flag. Calling its initializer
  // after this check is idempotent: the effective token privilege is already on.
  const bool ok=t0common::enable_large_page_privilege(&status);
  if(!ok) status="HOLD already-enabled privilege could not initialize allocator: "+status;
  return ok;
}
}
