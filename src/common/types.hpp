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

// Skip-ahead configuration for adaptive bad sector handling
struct SkipAheadConfig {
    uint32_t consecutive_bad_threshold = 4;   // Skip after N consecutive bad batches
    uint64_t skip_distance_sectors = 1024;    // How many sectors to skip (default 512KB)
    bool enabled = true;
};

// Read timeout configuration
struct ReadTimeoutConfig {
    uint32_t timeout_ms = 5000;               // Timeout per read operation (ms)
    uint32_t retry_count = 3;                 // Retries before marking as bad
    bool enabled = false;                     // Timeout feature toggle
};

struct DiskExtent {
    uint64_t start_sector;
    uint64_t sector_count;
};

struct RecoverableFile {
    uint64_t db_id = 0;  // Database row ID for efficient pagination
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