#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <atomic>

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
    uint64_t start_sector = 0;
    uint64_t sector_count = 0;
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
    uint64_t file_size = 0;
    std::vector<DiskExtent> fragments;
    CorruptionLevel corruption_level = CorruptionLevel::None;
    FileType file_type = FileType::Unknown;
    std::optional<uint64_t> mft_id;

    // Confidence scoring from file signature validators (0-100)
    uint8_t confidence = 0;
    // Raw MatchFlags bits from file_signatures.hpp MatchFlags enum
    uint32_t match_flags_raw = 0;

    // Multi-format matching metadata
    uint32_t candidate_index = 0;       // Index in multi-format candidate list
    uint8_t total_candidates = 0;       // Total number of candidates for this sector
    bool is_primary_candidate = false;  // True if highest confidence candidate

    // Legacy accessor for backward compatibility
    bool is_corrupted() const { return corruption_level != CorruptionLevel::None; }
};

struct DiskGeometry {
    uint64_t total_sectors = 0;
    uint32_t sector_size = 0;
    uint32_t cylinders = 0;
    uint32_t tracks_per_cylinder = 0;
    uint32_t sectors_per_track = 0;
};

struct PartitionInfo {
    uint32_t index = 0;
    uint64_t start_sector = 0;
    uint64_t sector_count = 0;
    std::wstring filesystem_type;
    std::wstring volume_label;
    uint8_t type_id = 0;
};

struct DiskInfo {
    uint32_t physical_drive_number = 0;
    std::wstring device_path;
    DiskGeometry geometry;
    std::vector<PartitionInfo> partitions;
    uint64_t disk_size_bytes = 0;
    std::wstring model_name;
};

// Thread-safe scan progress structure.
// Atomic fields ensure safe concurrent access from scan and UI threads.
// Use snapshot() to get a consistent copy of all values.
struct ScanProgress {
    std::atomic<uint64_t> sectors_scanned{0};
    std::atomic<uint64_t> total_sectors{0};
    std::atomic<uint32_t> files_found{0};
    std::atomic<uint32_t> bad_sectors_hit{0};
    std::atomic<bool> is_paused{false};
    std::atomic<bool> is_complete{false};
    std::atomic<uint8_t> scan_phase{0};         // 0=not started, 1=metadata done, 2=raw in progress
    std::atomic<uint64_t> raw_resume_sector{0}; // Absolute LBA for RAW scan resume

    // Non-copyable due to atomics
    ScanProgress() = default;
    ScanProgress(const ScanProgress&) = delete;
    ScanProgress& operator=(const ScanProgress&) = delete;
    ScanProgress(ScanProgress&&) = delete;
    ScanProgress& operator=(ScanProgress&&) = delete;

    // Snapshot struct for passing progress data
    struct Snapshot {
        uint64_t sectors_scanned = 0;
        uint64_t total_sectors = 0;
        uint32_t files_found = 0;
        uint32_t bad_sectors_hit = 0;
        bool is_paused = false;
        bool is_complete = false;
        uint8_t scan_phase = 0;
        uint64_t raw_resume_sector = 0;
    };

    // Get a snapshot of current progress (individual atomics loaded, not atomic as a whole)
    Snapshot snapshot() const {
        Snapshot s;
        s.sectors_scanned = sectors_scanned.load(std::memory_order_relaxed);
        s.total_sectors = total_sectors.load(std::memory_order_relaxed);
        s.files_found = files_found.load(std::memory_order_relaxed);
        s.bad_sectors_hit = bad_sectors_hit.load(std::memory_order_relaxed);
        s.is_paused = is_paused.load(std::memory_order_relaxed);
        s.is_complete = is_complete.load(std::memory_order_relaxed);
        s.scan_phase = scan_phase.load(std::memory_order_relaxed);
        s.raw_resume_sector = raw_resume_sector.load(std::memory_order_relaxed);
        return s;
    }

    // Load from a snapshot (for resuming)
    void load_from(const Snapshot& s) {
        sectors_scanned.store(s.sectors_scanned, std::memory_order_relaxed);
        total_sectors.store(s.total_sectors, std::memory_order_relaxed);
        files_found.store(s.files_found, std::memory_order_relaxed);
        bad_sectors_hit.store(s.bad_sectors_hit, std::memory_order_relaxed);
        is_paused.store(s.is_paused, std::memory_order_relaxed);
        is_complete.store(s.is_complete, std::memory_order_relaxed);
        scan_phase.store(s.scan_phase, std::memory_order_relaxed);
        raw_resume_sector.store(s.raw_resume_sector, std::memory_order_relaxed);
    }
};

} // namespace disk_recover