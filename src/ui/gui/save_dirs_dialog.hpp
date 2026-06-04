#pragma once
#include "business/recovery_manager.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace disk_recover {

class SaveDirsDialog {
public:
    static bool Show(HWND parent_hwnd,
                     const std::vector<wchar_t>& excluded_letters,
                     std::vector<SaveDirEntry>& out_dirs);
};

// Helper: get the physical drive number for a given drive letter
// Returns -1 if unable to determine
int GetPhysicalDriveNumberForLetter(wchar_t letter);

} // namespace disk_recover