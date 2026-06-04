#pragma once

#include "common/types.hpp"
#include "scan_cache_db.hpp"
#include "multi_target_writer.hpp"
#include "disk-io/buffered_reader.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/disk_handle.hpp"
#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <string>
#include <thread>
#include <windows.h>

namespace disk_recover {

// Combined scan and recover manager for damaged disks
// Scans disk and immediately recovers found files
class ScanAndRecoverManager {
public:
    struct Progress {
        uint64_t sectors_scanned = 0;
        uint64_t total_sectors = 0;
        uint32_t files_found = 0;
        uint32_t files_recovered = 0;
        uint64_t bytes_recovered = 0;
        uint32_t files_failed = 0;
        uint32_t bad_sectors = 0;
        uint32_t sectors_skipped = 0;
        uint64_t current_sector = 0;  // Current scanning position for resume
        double scan_rate_mbps = 0.0;  // Scan rate in MB/s
        uint8_t percent = 0;
        bool is_complete = false;
        bool is_paused = false;
    };

    // Custom WM_USER messages for PostMessage notifications
// (values match those in resource.h for the GUI layer)
#define SRM_WM_SCAN_RECOVER_PAUSED    (WM_USER + 311)
#define SRM_WM_SCAN_RECOVER_COMPLETE  (WM_USER + 312)

    struct Config {
        std::wstring device_path;
        std::wstring db_path;
        std::vector<std::wstring> output_dirs;
        std::string session_id;
        ScanMode mode = ScanMode::Deep;

        // File type filters
        bool scan_images = true;
        bool scan_videos = true;

        // Sector range
        uint64_t start_sector = 0;
        uint64_t end_sector = 0;  // 0 = auto-detect
        uint64_t resume_from_sector = 0;  // Resume position (0 = start from beginning)

        // Bad sector handling
        BadSectorPolicy bad_sector_policy = BadSectorPolicy::Skip;
        SkipAheadConfig skip_config;
        ReadTimeoutConfig timeout_config;

        // Buffer size in bytes (default 128MB)
        size_t buffer_size = BufferedSectorReader::DEFAULT_BUFFER_SIZE;

        // Recovery options
        uint64_t min_free_space = 2ULL * 1024 * 1024 * 1024;  // 2GB minimum
        uint32_t max_files_per_dir = 500;

        // HWND for PostMessage notifications (auto-pause, completion)
        HWND hwnd = nullptr;

        // Callback: called on worker thread when a file is successfully recovered
        // save_path is the full path where the file was written
        std::function<void(const RecoverableFile& file, const std::wstring& save_path)> on_file_recovered;

        // Progress callback
        std::function<void(const Progress&)> on_progress;
    };

    ScanAndRecoverManager() = default;
    ~ScanAndRecoverManager();

    // Non-copyable
    ScanAndRecoverManager(const ScanAndRecoverManager&) = delete;
    ScanAndRecoverManager& operator=(const ScanAndRecoverManager&) = delete;

    bool start(const Config& config);
    void pause();
    void resume();
    void stop();               // Request stop AND join worker thread (use from non-GUI threads or destructor)
    void stop_request_only();  // Request stop WITHOUT joining (safe to call from GUI thread)

    bool is_running() const { return running_.load(); }
    bool is_paused() const { return paused_.load(); }
    Progress progress() const;

private:
    void worker_thread(Config config);
    bool recover_file(const RecoverableFile& file, BufferedSectorReader& reader,
                      const std::wstring& output_dir);
    std::wstring generate_output_path(const std::wstring& base_dir,
                                       const std::wstring& filename);

    // Check space and switch target; returns true if space available, false if auto-paused
    bool check_space_and_switch(const Config& config);

    // Filesystem detection helper (uses SectorReader directly for boot sector reads)
    enum class FileSystemType { Unknown, NTFS, FAT12, FAT16, FAT32, ExFAT };
    FileSystemType detect_filesystem(SectorReader& reader, uint64_t start_sector);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::mutex progress_mutex_;
    Progress progress_;

    MultiTargetWriter writer_;
    ScanCacheDB cache_db_;
    std::string current_session_id_;

    // Extension grouping
    std::unordered_map<std::wstring, uint32_t> ext_counters_;
    std::unordered_map<std::wstring, uint32_t> ext_subfolders_;

    // Store config for callbacks
    Config active_config_;
};

} // namespace disk_recover