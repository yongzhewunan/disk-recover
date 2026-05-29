#pragma once
#include "scan_cache_db.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <functional>
#include <string>
#include <atomic>
#include <mutex>

namespace disk_recover {

// File system type detection result
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

    bool start_scan(const Config& config);
    void pause_scan();
    void resume_scan();
    void stop_scan();

    bool is_scanning() const { return scanning_.load(); }
    bool is_paused() const { return paused_.load(); }
    ScanProgress progress() const;

    void set_progress_callback(std::function<void(const ScanProgress&)> cb) { on_progress_ = cb; }
    void set_file_found_callback(std::function<void(const RecoverableFile&)> cb) { on_file_found_ = cb; }

private:
    void scan_thread_func(Config config);
    void flush_cache(const std::string& session_id);

    std::atomic<bool> scanning_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex progress_mutex_;
    ScanProgress progress_{};
    std::string current_session_id_;

    ScanCacheDB cache_db_;
    std::vector<RecoverableFile> pending_files_;
    static constexpr uint32_t FLUSH_THRESHOLD = 1000;

    std::function<void(const ScanProgress&)> on_progress_;
    std::function<void(const RecoverableFile&)> on_file_found_;
};

} // namespace disk_recover
