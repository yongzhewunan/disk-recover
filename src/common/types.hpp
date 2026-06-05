#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace disk_recover {

enum class FileType : uint8_t {
    Unknown  = 0,
    Image    = 1,
    Video    = 2,
    Audio    = 3,
    Document = 4,
    Archive  = 5,
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
    bool enabled = true;                      // Timeout feature toggle (enabled by default)
};

struct DiskExtent {
    uint64_t start_sector;
    uint64_t sector_count;
};

// Corruption severity levels for recovered files
enum class CorruptionLevel : uint8_t {
    None = 0,       // File appears intact (header + footer + high confidence)
    Minor = 1,      // Missing footer or partial match, but header validated
    Moderate = 2,   // Container parsed but incomplete (AcceptContainer without footer)
    Major = 3,      // Only magic matched, no structure validation (AcceptHeader)
    Severe = 4      // Read errors, failed merge, or very low confidence
};

// Minimum confidence threshold for file recovery.
// Files below this threshold are very likely false positives.
// AcceptHeader = 25, AcceptStructure = 50, AcceptContainer = 75, AcceptVerified = 100
constexpr uint8_t MIN_RECOVERY_CONFIDENCE = 50;

struct RecoverableFile {
    uint64_t db_id = 0;  // Database row ID for efficient pagination
    std::wstring file_name;
    uint64_t file_size;
    std::vector<DiskExtent> fragments;
    CorruptionLevel corruption_level = CorruptionLevel::None;
    FileType file_type;
    std::optional<uint64_t> mft_id;

    // Confidence scoring from file signature validators (0-100)
    uint8_t confidence = 0;
    // Raw MatchFlags bits from file_signatures.hpp MatchFlags enum
    uint32_t match_flags_raw = 0;

    // Legacy accessor for backward compatibility
    bool is_corrupted() const { return corruption_level != CorruptionLevel::None; }
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