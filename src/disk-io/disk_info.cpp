#include "disk_info.hpp"
#include "../common/logger.hpp"
#include <winioctl.h>
#include <ntddscsi.h>
#include <cstring>

namespace disk_recover {

std::vector<DiskInfo> DiskInfoQuery::EnumeratePhysicalDisks() {
    std::vector<DiskInfo> disks;

    LOG_MSG(L"[DiskInfo] Starting disk enumeration...");

    // Try to enumerate up to 32 physical drives
    // Note: Requires Administrator privileges to access physical disks
    int consecutiveFailures = 0;
    for (uint32_t i = 0; i < 32; ++i) {
        std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
        DiskHandle handle;

        LOG_FMT(L"[DiskInfo] Trying to open %s", path.c_str());

        // Try to open this disk
        if (!handle.open(path)) {
            DWORD err = GetLastError();
            LOG_FMT(L"[DiskInfo] Failed to open %s, error=%d", path.c_str(), err);

            consecutiveFailures++;
            // If we found disks and hit 3 consecutive failures, stop
            if (!disks.empty() && consecutiveFailures >= 3) {
                break;
            }
            continue;
        }

        // Successfully opened a disk
        consecutiveFailures = 0;

        LOG_MSG(L"[DiskInfo] Successfully opened disk, querying info...");

        DiskInfo info{};
        info.physical_drive_number = i;
        info.device_path = path;

        if (!QueryDiskGeometry(handle, info.geometry)) {
            LOG_MSG(L"[DiskInfo] Failed to query disk geometry");
        }

        if (!QueryPartitionTable(handle, info.partitions)) {
            LOG_MSG(L"[DiskInfo] Failed to query partition table");
        }

        info.disk_size_bytes = info.geometry.total_sectors * info.geometry.sector_size;
        info.model_name = QueryDiskModel(handle);

        LOG_FMT(L"[DiskInfo] Found disk: %s, size=%llu bytes, model=%s",
                path.c_str(), info.disk_size_bytes, info.model_name.c_str());

        disks.push_back(std::move(info));
    }

    LOG_FMT(L"[DiskInfo] Enumeration complete, found %zu disks", disks.size());

    return disks;
}

bool DiskInfoQuery::QueryDiskGeometry(DiskHandle& handle, DiskGeometry& geometry) {
    DISK_GEOMETRY_EX geo_ex{};
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(handle.native_handle(),
            IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
            nullptr, 0, &geo_ex, sizeof(geo_ex), &bytes_returned, nullptr)) {
        return false;
    }
    geometry.sector_size = geo_ex.Geometry.BytesPerSector;
    geometry.cylinders = geo_ex.Geometry.Cylinders.LowPart;
    geometry.tracks_per_cylinder = geo_ex.Geometry.TracksPerCylinder;
    geometry.sectors_per_track = geo_ex.Geometry.SectorsPerTrack;
    geometry.total_sectors = geo_ex.DiskSize.QuadPart / geometry.sector_size;
    return true;
}

bool DiskInfoQuery::QueryPartitionTable(DiskHandle& handle, std::vector<PartitionInfo>& partitions) {
    DWORD bytes_returned = 0;
    std::vector<uint8_t> buffer(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX));
    if (!DeviceIoControl(handle.native_handle(),
            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
            nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytes_returned, nullptr)) {
        return false;
    }
    auto* layout = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(buffer.data());
    DWORD partition_count = layout->PartitionCount;
    for (DWORD i = 0; i < partition_count && i < 128; ++i) {
        auto& src = layout->PartitionEntry[i];
        if (src.PartitionStyle != PARTITION_STYLE_MBR &&
            src.PartitionStyle != PARTITION_STYLE_GPT) continue;
        if (src.PartitionLength.QuadPart == 0) continue;

        PartitionInfo pi{};
        pi.index = i;
        pi.start_sector = src.StartingOffset.QuadPart / 512;
        pi.sector_count = src.PartitionLength.QuadPart / 512;
        if (src.PartitionStyle == PARTITION_STYLE_MBR) {
            pi.type_id = src.Mbr.PartitionType;
        }
        partitions.push_back(pi);
    }
    return true;
}

std::wstring DiskInfoQuery::QueryDiskModel(DiskHandle& handle) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<uint8_t> buffer(4096);
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(handle.native_handle(),
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytes_returned, nullptr)) {
        return L"Unknown";
    }
    auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
    if (desc->ProductIdOffset != 0 && desc->ProductIdOffset < buffer.size()) {
        const char* model = reinterpret_cast<const char*>(buffer.data() + desc->ProductIdOffset);
        size_t len = strnlen(model, buffer.size() - desc->ProductIdOffset);
        return std::wstring(model, model + len);
    }
    return L"Unknown";
}

} // namespace disk_recover
