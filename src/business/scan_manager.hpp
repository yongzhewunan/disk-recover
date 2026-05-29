#pragma once

#include "../common/types.hpp"
#include "scan_cache_db.hpp"
#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <string>
#include <thread>

namespace disk_recover {

enum class FileSystemType : uint8_t {
    Unknown = 0,
    NTFS    = 1,
    FAT12   = 2,
    FAT16   = 3,
    FAT32   = 4,
    ExFAT   = 5,
};

class ScanManager {
public:
    struct Config {
        std::wstring device_path;
        std::wstring db_path;
        std::string session_id;
        ScanMode mode = ScanMode::Deep;
        BadSectorPolicy bad_sector_policy = BadSectorPolicy::Skip;
        bool scan_images = true;
        bool scan_videos = true;
        uint64_t start_sector = 0;
        uint64_t end_sector = 0;
    };

    ScanManager() = default;
    ~ScanManager() {
        stop_scan();
        if (scan_thread_.joinable()) {
            scan_thread_.join();
        }
    }

    // Non-copyable: owns a thread
    ScanManager(const ScanManager&) = delete;
    ScanManager& operator=(const ScanManager&) = delete;

    bool start_scan(const Config& config);
    bool resume_scan(const Config& config);
    void pause_scan();
    void resume_from_pause();
    void stop_scan();

    bool is_scanning() const { return scanning_.load(); }
    bool is_paused() const { return paused_.load(); }
    ScanProgress progress() const;

    // Thread-safe access to found files for UI batch updates
    std::vector<RecoverableFile> take_found_files();

    void set_progress_callback(std::function<void(const ScanProgress&)> cb) { on_progress_ = std::move(cb); }
    void set_file_found_callback(std::function<void(const RecoverableFile&)> cb) { on_file_found_ = std::move(cb); }

private:
    static constexpr size_t FLUSH_THRESHOLD = 50;

    void scan_thread_func(Config config);
    void flush_cache(const std::string& session_id);

    std::atomic<bool> scanning_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::mutex progress_mutex_;
    ScanProgress progress_{};

    std::mutex files_mutex_;
    std::vector<RecoverableFile> pending_files_;
    std::vector<RecoverableFile> ui_files_;  // Files for UI to pick up

    ScanCacheDB cache_db_;
    std::string current_session_id_;

    std::function<void(const ScanProgress&)> on_progress_;
    std::function<void(const RecoverableFile&)> on_file_found_;

    std::thread scan_thread_;
};

} // namespace disk_recover
