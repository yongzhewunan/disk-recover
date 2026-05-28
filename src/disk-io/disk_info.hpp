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
};

} // namespace disk_recover