#pragma once
#include "disk_handle.hpp"
#include "types.hpp"
#include <vector>

namespace disk_recover {

class DiskInfoQuery {
public:
    static std::vector<DiskInfo> EnumeratePhysicalDisks();
    static bool QueryDiskGeometry(DiskHandle& handle, DiskGeometry& geometry);
    static bool QueryPartitionTable(DiskHandle& handle, std::vector<PartitionInfo>& partitions);
    static std::wstring QueryDiskModel(DiskHandle& handle);
    static std::wstring DetectFilesystemFromBootSector(DiskHandle& handle, uint64_t start_sector);
};

} // namespace disk_recover