#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace disk_recover {

enum class FileType : uint8_t {
    Unknown = 0,
    Image   = 1,
    Video   = 2,
};

enum class ScanMode : uint8_t {
    Quick = 0,
    Deep  = 1,
    Full  = 2,
};

enum class BadSectorPolicy : uint8_t {
    Skip      = 0,
    Retry     = 1,
    ForceRead = 2,
};

struct DiskExtent {
    uint64_t start_sector;
    uint64_t sector_count;
};

struct RecoverableFile {
    std::wstring file_name;
    uint64_t file_size;
    std::vector<DiskExtent> fragments;
    bool is_corrupted;
    FileType file_type;
    std::optional<uint64_t> mft_id;
};

struct DiskGeometry {
    uint64_t total_sectors;
    uint32_t sector_size;
    uint32_t cylinders;
    uint32_t tracks_per_cylinder;
    uint32_t sectors_per_track;
};

struct PartitionInfo {
    uint32_t index;
    uint64_t start_sector;
    uint64_t sector_count;
    std::wstring filesystem_type;
    std::wstring volume_label;
    uint8_t type_id;
};

struct DiskInfo {
    uint32_t physical_drive_number;
    std::wstring device_path;
    DiskGeometry geometry;
    std::vector<PartitionInfo> partitions;
    uint64_t disk_size_bytes;
    std::wstring model_name;
};

struct ScanProgress {
    uint64_t sectors_scanned = 0;
    uint64_t total_sectors = 0;
    uint32_t files_found = 0;
    uint32_t bad_sectors_hit = 0;
    bool is_paused = false;
    bool is_complete = false;
    uint8_t scan_phase = 0;         // 0=not started, 1=metadata done, 2=raw in progress
    uint64_t raw_resume_sector = 0; // Absolute LBA for RAW scan resume
};

} // namespace disk_recover