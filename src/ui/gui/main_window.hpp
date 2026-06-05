#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include "resource.h"
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "disk-io/buffered_reader.hpp"
#include "filesystem/raw/signature_scanner.hpp"
#include "common/types.hpp"
#include "business/scan_cache_db.hpp"
#include "business/scan_recover_manager.hpp"
#include "business/recovery_manager.hpp"

namespace disk_recover {

// Data passed via PostMessage for WM_FILE_RECOVERED
struct FileRecoveredData {
    RecoverableFile file;
    std::wstring save_path;
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool create(HINSTANCE hInst);
    void show(int nCmdShow);

private:
    static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    // UI creation
    void create_controls();
    void refresh_drive_list();

    // Event handlers
    void on_start_pause();
    void on_stop();
    void on_save_dirs();

    // Scan/Recover operations
    void do_scan_recover(const ScanAndRecoverManager::Config& config);

    // UI update helpers
    void update_progress(const ScanAndRecoverManager::Progress& progress);
    void add_file_to_list(const RecoverableFile& file, const std::wstring& save_path);
    void set_status(const std::wstring& text);
    void enable_config_controls(bool enabled);

    // State
    enum class State { Idle, Running, Paused };
    State state_ = State::Idle;

    // Window handle
    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;

    // Controls
    HWND h_combo_drives_ = nullptr;
    HWND h_btn_start_ = nullptr;
    HWND h_btn_stop_ = nullptr;
    HWND h_cb_scan_mode_ = nullptr;
    HWND h_chk_images_ = nullptr;
    HWND h_chk_videos_ = nullptr;
    HWND h_chk_audio_ = nullptr;
    HWND h_chk_documents_ = nullptr;
    HWND h_chk_archives_ = nullptr;
    HWND h_ed_start_sector_ = nullptr;
    HWND h_ed_end_sector_ = nullptr;
    HWND h_rb_sector_abs_ = nullptr;
    HWND h_rb_sector_pct_ = nullptr;
    HWND h_btn_save_dirs_ = nullptr;
    HWND h_st_save_info_ = nullptr;
    HWND h_list_files_ = nullptr;
    HWND h_status_bar_ = nullptr;
    HWND h_progress_ = nullptr;

    // Data
    std::vector<DiskInfo> drives_;
    std::vector<SaveDirEntry> save_dirs_;

    // Manager
    std::unique_ptr<ScanAndRecoverManager> manager_;
};

} // namespace disk_recover
