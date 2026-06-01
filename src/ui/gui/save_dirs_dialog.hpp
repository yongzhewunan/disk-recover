#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace disk_recover {

struct SaveDirEntry {
    std::wstring path;
    uint64_t free_bytes = 0;
};

class SaveDirsDialog {
public:
    static bool Show(HWND parent_hwnd,
                     const std::vector<wchar_t>& excluded_letters,
                     std::vector<SaveDirEntry>& out_dirs);
};

} // namespace disk_recover
