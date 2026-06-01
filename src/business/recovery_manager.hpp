#pragma once
#include "common/types.hpp"
#include "multi_target_writer.hpp"
#include "scan_cache_db.hpp"
#include "disk-io/sector_reader.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <windows.h>

namespace disk_recover {

struct SaveDirEntry {
    std::wstring path;
    uint64_t free_bytes = 0;
};

struct RecoveryConfig {
    std::vector<SaveDirEntry> save_dirs;
    std::string session_id;
    std::wstring db_path;
    std::wstring source_disk_path;              // e.g. L"\\\\.\\PhysicalDrive1"
    uint32_t sector_size = 512;                 // Disk sector size for reading
    HWND hwnd = nullptr;                        // For PostMessage progress
    uint64_t min_free_bytes = 1ULL << 30;       // 1 GB threshold
};

// WM_RECOVERY_PROGRESS: wParam = files_recovered (LOWORD) reserved, lParam = pointer to RecoveryProgress (caller must copy)
// WM_RECOVERY_COMPLETE: wParam = 0 success, 1 error
// WM_RECOVERY_PAUSED: wParam = 0 space, lParam = 0
#define WM_RECOVERY_PROGRESS  (WM_USER + 301)
#define WM_RECOVERY_COMPLETE  (WM_USER + 302)
#define WM_RECOVERY_PAUSED    (WM_USER + 303)

class RecoveryManager {
public:
    ~RecoveryManager();

    bool start_recovery(const RecoveryConfig& config);
    void stop_recovery();
    bool is_recovering() const { return recovering_.load(); }
    bool is_paused() const { return paused_.load(); }
    void pause_recovery();
    void resume_recovery();

    struct RecoveryStats {
        uint64_t files_recovered = 0;
        uint64_t bytes_recovered = 0;
        uint64_t total_files = 0;
        uint64_t files_failed = 0;
    };
    RecoveryStats stats() const;

private:
    void recovery_thread_func(RecoveryConfig config);
    bool recover_single_file(const RecoverableFile& file, SectorReader& reader);
    std::wstring build_output_path(const std::wstring& base_dir,
                                    const std::wstring& filename,
                                    const std::wstring& extension);
    bool check_space_and_switch(const RecoveryConfig& config);
    void save_recovery_progress_state();
    bool load_recovery_progress_state(const std::string& session_id);

    std::thread recovery_thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> recovering_{false};
    mutable std::mutex stats_mutex_;
    RecoveryStats stats_;

    MultiTargetWriter writer_;
    ScanCacheDB cache_db_;

    // Current session ID for progress saving
    std::string current_session_id_;

    // Extension grouping counters: key = base_dir + L"/" + ext, value = current count
    std::unordered_map<std::wstring, uint32_t> ext_counters_;
    // Track current subfolder number per extension per base_dir: key = base_dir + L"/" + ext, value = subfolder index (1=default, 2=ext(1), etc.)
    std::unordered_map<std::wstring, uint32_t> ext_subfolder_;

    int last_file_index_ = 0;  // For resume
};

} // namespace disk_recover
