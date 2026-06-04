#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "resource.h"
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "disk-io/buffered_reader.hpp"
#include "filesystem/raw/signature_scanner.hpp"
#include "common/types.hpp"
#include "business/scan_cache_db.hpp"
#include "business/scan_recover_manager.hpp"

namespace disk_recover {

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
    void on_scan();
    void on_recover();
    void on_scan_recover();
    void on_stop();
    void on_browse_output();

    // Scan/Recover operations
    void do_scan();
    void do_scan_recover();
    void stop_operation();

    // UI update helpers
    void update_progress(const ScanProgress& progress);
    void add_file_to_list(const RecoverableFile& file);
    void set_status(const std::wstring& text);
    void enable_buttons(bool scanning);

    // Window handle
    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;

    // Controls
    HWND h_combo_drives_ = nullptr;
    HWND h_btn_scan_ = nullptr;
    HWND h_btn_recover_ = nullptr;
    HWND h_btn_scan_recover_ = nullptr;
    HWND h_btn_stop_ = nullptr;
    HWND h_list_files_ = nullptr;
    HWND h_status_bar_ = nullptr;
    HWND h_progress_ = nullptr;
    HWND h_ed_output_ = nullptr;
    HWND h_btn_browse_ = nullptr;

    // State
    std::vector<DiskInfo> drives_;
    std::vector<RecoverableFile> found_files_;
    std::wstring output_dir_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    bool is_scanning_ = false;

    // Scan cache DB
    std::unique_ptr<ScanCacheDB> cache_db_;
};

} // namespace disk_recover