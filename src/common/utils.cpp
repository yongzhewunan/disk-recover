#include "utils.hpp"
#include <windows.h>
#include <shellapi.h>

namespace disk_recover::utils {

bool IsAdminPrivilege() {
    BOOL is_admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
    PSID admin_group = nullptr;
    if (AllocateAndInitializeSid(&nt_auth, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(nullptr, admin_group, &is_admin);
        FreeSid(admin_group);
    }
    return is_admin;
}

bool EnsureAdminPrivilege() {
    if (IsAdminPrivilege()) return true;
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei);
}

std::wstring FormatFileSize(uint64_t bytes) {
    if (bytes >= 1ULL << 40) return std::to_wstring(bytes >> 40) + L" TB";
    if (bytes >= 1ULL << 30) return std::to_wstring(bytes >> 30) + L" GB";
    if (bytes >= 1ULL << 20) return std::to_wstring(bytes >> 20) + L" MB";
    if (bytes >= 1ULL << 10) return std::to_wstring(bytes >> 10) + L" KB";
    return std::to_wstring(bytes) + L" B";
}

std::wstring FormatSectorRange(uint64_t start, uint64_t count) {
    return std::to_wstring(start) + L"-" + std::to_wstring(start + count - 1);
}

uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t AlignDown(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

} // namespace disk_recover::utils