#include "disk_info.hpp"
#include "sector_reader.hpp"
#include "aligned_buffer.hpp"
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
            // Map common MBR partition types
            switch (src.Mbr.PartitionType) {
            case 0x07: pi.filesystem_type = L"NTFS"; break;
            case 0x0B: pi.filesystem_type = L"FAT32"; break;
            case 0x0C: pi.filesystem_type = L"FAT32"; break;
            case 0x0E: pi.filesystem_type = L"FAT16"; break;
            case 0x06: pi.filesystem_type = L"FAT16"; break;
            case 0x04: pi.filesystem_type = L"FAT16"; break;
            case 0x01: pi.filesystem_type = L"FAT12"; break;
            case 0x11: pi.filesystem_type = L"FAT12"; break;
            case 0x14: pi.filesystem_type = L"FAT16"; break;
            case 0x27: pi.filesystem_type = L"NTFS"; break; // Recovery
            default: break;
            }
        }

        // Detect filesystem from boot sector if not already identified from MBR type
        if (pi.filesystem_type.empty()) {
            pi.filesystem_type = DetectFilesystemFromBootSector(handle, pi.start_sector);
        }

        LOG_FMT(L"[DiskInfo] Partition %d: start=%llu, sectors=%llu, fs=%s, mbr_type=0x%02X",
                 pi.index, pi.start_sector, pi.sector_count,
                 pi.filesystem_type.c_str(),
                 src.PartitionStyle == PARTITION_STYLE_MBR ? src.Mbr.PartitionType : 0);

        partitions.push_back(pi);
    }
    return true;
}

std::wstring DiskInfoQuery::DetectFilesystemFromBootSector(DiskHandle& handle, uint64_t start_sector) {
    // Use SectorReader for proper aligned I/O
    SectorReader reader(handle, 512);
    AlignedBuffer buf(512, 512);
    if (!reader.read_sectors(start_sector, 1, buf)) {
        LOG_FMT(L"[DiskInfo] Failed to read boot sector at %llu", start_sector);
        return L"";
    }

    const uint8_t* data = buf.data();
    std::wstring result;

    // Check boot sector signature 0xAA55
    if (data[510] == 0x55 && data[511] == 0xAA) {
        // Check for NTFS: "NTFS    " at offset 3
        if (memcmp(data + 3, "NTFS    ", 8) == 0) {
            result = L"NTFS";
        }
        // Check for exFAT: "EXFAT   " at offset 3
        else if (memcmp(data + 3, "EXFAT   ", 8) == 0) {
            result = L"exFAT";
        }
        else {
            // Check for FAT variants
            uint16_t bytes_per_sector = *reinterpret_cast<const uint16_t*>(data + 11);
            uint8_t sectors_per_cluster = data[13];

            if (bytes_per_sector > 0 && bytes_per_sector % 512 == 0 &&
                sectors_per_cluster > 0 &&
                (sectors_per_cluster & (sectors_per_cluster - 1)) == 0) {

                uint16_t root_entry_count = *reinterpret_cast<const uint16_t*>(data + 17);
                uint16_t sectors_per_fat_16 = *reinterpret_cast<const uint16_t*>(data + 22);

                if (root_entry_count == 0 && sectors_per_fat_16 == 0) {
                    result = L"FAT32";
                } else if (sectors_per_fat_16 > 0) {
                    uint16_t reserved = *reinterpret_cast<const uint16_t*>(data + 14);
                    uint8_t fat_count = data[16];
                    uint32_t total_sectors_16 = *reinterpret_cast<const uint16_t*>(data + 19);
                    uint32_t total_sectors_32 = *reinterpret_cast<const uint32_t*>(data + 32);
                    uint32_t total_sectors = total_sectors_16 ? total_sectors_16 : total_sectors_32;
                    uint32_t data_sectors = total_sectors - reserved - fat_count * sectors_per_fat_16;
                    uint32_t total_clusters = data_sectors / sectors_per_cluster;

                    if (total_clusters < 4085) result = L"FAT12";
                    else result = L"FAT16";
                }
            }
        }
    }

    LOG_FMT(L"[DiskInfo] DetectFilesystem at sector %llu: %s", start_sector, result.c_str());
    return result;
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
