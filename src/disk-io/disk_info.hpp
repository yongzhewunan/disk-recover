#pragma once
#include "disk_handle.hpp"
#include "types.hpp"
#include <vector>

namespace disk_recover {

class DiskInfoQuery {
public:
    static std::vector<DiskInfo> EnumeratePhysicalDisks();
    static bool QueryDiskGeometry(DiskHandle& handle, DiskGeometry& geometry);
    static bool QueryPartitionTable(DiskHandle& handle, uint32_t sector_size, std::vector<PartitionInfo>& partitions);
    static std::wstring QueryDiskModel(DiskHandle& handle);
    static std::wstring DetectFilesystemFromBootSector(DiskHandle& handle, uint64_t start_sector);
    static std::vector<wchar_t> GetDriveLettersForPhysicalDrive(const std::wstring& physical_drive_path);
};

} // namespace disk_recover