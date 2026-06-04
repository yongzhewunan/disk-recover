#include "main_window.hpp"
#include "disk-io/buffered_reader.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "filesystem/raw/signature_scanner.hpp"
#include "ui/gui/save_dirs_dialog.hpp"
#include "common/logger.hpp"
#include <shlobj.h>
#include <algorithm>

namespace disk_recover {

static const wchar_t CLASS_NAME[] = L"DiskRecoverMainWindow";
static const int WIN_WIDTH = 960;
static const int WIN_HEIGHT = 700;

static std::wstring FormatFileSize(uint64_t bytes) {
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        _snwprintf_s(buf, _TRUNCATE, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        _snwprintf_s(buf, _TRUNCATE, L"%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        _snwprintf_s(buf, _TRUNCATE, L"%.1f KB", bytes / 1024.0);
    else
        _snwprintf_s(buf, _TRUNCATE, L"%llu B", bytes);
    return buf;
}

static std::string GenerateSessionId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return "session_" + std::to_string(ms);
}

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
    if (manager_) {
        manager_->stop();
    }
}

bool MainWindow::create(HINSTANCE hInst) {
    hInst_ = hInst;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, CLASS_NAME, L"Disk Recover",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_WIDTH, WIN_HEIGHT,
        nullptr, nullptr, hInst, this);

    if (!hwnd_) return false;

    create_controls();
    refresh_drive_list();
    return true;
}

void MainWindow::show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

LRESULT CALLBACK MainWindow::wnd_proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hWnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(msg, wp, lp);
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case IDC_BTN_START:    on_start_pause(); break;
        case IDC_BTN_STOP:     on_stop(); break;
        case IDC_BTN_SAVE_DIRS: on_save_dirs(); break;
        }
        break;
    }
    case WM_FILE_RECOVERED: {
        // wp = pointer to FileRecoveredData (heap-allocated, we must delete)
        auto* data = reinterpret_cast<FileRecoveredData*>(wp);
        if (data) {
            add_file_to_list(data->file, data->save_path);
            delete data;
        }
        break;
    }
    case WM_SETSTATUS: {
        // wp = heap-allocated wchar_t* text (we must delete)
        auto* text = reinterpret_cast<wchar_t*>(wp);
        if (text) {
            set_status(text);
            delete[] text;
        }
        break;
    }
    case WM_SCAN_RECOVER_PAUSED: {
        state_ = State::Paused;
        SetWindowTextW(h_btn_start_, L"Continue");
        set_status(L"All save directories have less than 2GB free space. Please add directories or free up space, then click Continue.");
        break;
    }
    case WM_SCAN_RECOVER_COMPLETE: {
        // Worker thread has exited — safe to join and clean up.
        if (manager_) {
            manager_->stop();  // This joins the now-exited worker thread (non-blocking)
        }
        state_ = State::Idle;
        SetWindowTextW(h_btn_start_, L"Start");
        enable_config_controls(true);

        if (manager_) {
            auto p = manager_->progress();
            wchar_t text[256];
            _snwprintf_s(text, _TRUNCATE,
                L"Complete! Found: %u | Recovered: %u | Failed: %u | Size: %s",
                p.files_found, p.files_recovered, p.files_failed,
                FormatFileSize(p.bytes_recovered).c_str());
            set_status(text);
        }
        break;
    }
    case WM_SIZE: {
        if (h_list_files_) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            MoveWindow(h_list_files_, 10, 140, rc.right - 20, rc.bottom - 200, TRUE);
        }
        break;
    }
    case WM_DESTROY:
        if (manager_) {
            manager_->stop();  // Full stop with join — window is being destroyed, must block
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    return 0;
}

void MainWindow::create_controls() {
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // --- Row 1 (y=10): Drive, Scan Mode, Filters ---
    // Drive selector
    CreateWindowExW(0, L"STATIC", L"Drive:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        10, 14, 50, 20, hwnd_, nullptr, hInst_, nullptr);

    h_combo_drives_ = CreateWindowExW(0, WC_COMBOBOX, L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        65, 10, 200, 300, hwnd_, (HMENU)IDC_CB_DRIVES, hInst_, nullptr);
    SendMessageW(h_combo_drives_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Scan mode
    CreateWindowExW(0, L"STATIC", L"Mode:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        280, 14, 40, 20, hwnd_, nullptr, hInst_, nullptr);

    h_cb_scan_mode_ = CreateWindowExW(0, WC_COMBOBOX, L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        325, 10, 220, 300, hwnd_, (HMENU)IDC_CB_SCAN_MODE, hInst_, nullptr);
    SendMessageW(h_cb_scan_mode_, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(h_cb_scan_mode_, CB_ADDSTRING, 0, (LPARAM)L"Quick (Partition Table)");
    SendMessageW(h_cb_scan_mode_, CB_ADDSTRING, 0, (LPARAM)L"Deep (Partition Table + Raw)");
    SendMessageW(h_cb_scan_mode_, CB_ADDSTRING, 0, (LPARAM)L"Full (Raw Only)");
    SendMessageW(h_cb_scan_mode_, CB_SETCURSEL, 1, 0);  // Default: Deep

    // Image/Video checkboxes
    h_chk_images_ = CreateWindowExW(0, L"BUTTON", L"Images",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        560, 12, 80, 22, hwnd_, (HMENU)IDC_CHK_IMAGES, hInst_, nullptr);
    SendMessageW(h_chk_images_, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(h_chk_images_, BM_SETCHECK, BST_CHECKED, 0);

    h_chk_videos_ = CreateWindowExW(0, L"BUTTON", L"Videos",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        645, 12, 80, 22, hwnd_, (HMENU)IDC_CHK_VIDEOS, hInst_, nullptr);
    SendMessageW(h_chk_videos_, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(h_chk_videos_, BM_SETCHECK, BST_CHECKED, 0);

    // --- Row 2 (y=42): Sector range ---
    CreateWindowExW(0, L"STATIC", L"Start:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        10, 46, 50, 20, hwnd_, nullptr, hInst_, nullptr);

    h_ed_start_sector_ = CreateWindowExW(0, L"EDIT", L"0",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        65, 44, 100, 22, hwnd_, (HMENU)IDC_ED_START_SECTOR, hInst_, nullptr);
    SendMessageW(h_ed_start_sector_, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateWindowExW(0, L"STATIC", L"End:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        175, 46, 40, 20, hwnd_, nullptr, hInst_, nullptr);

    h_ed_end_sector_ = CreateWindowExW(0, L"EDIT", L"0",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        220, 44, 100, 22, hwnd_, (HMENU)IDC_ED_END_SECTOR, hInst_, nullptr);
    SendMessageW(h_ed_end_sector_, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateWindowExW(0, L"STATIC", L"(0 = full disk)",
        WS_VISIBLE | WS_CHILD,
        325, 46, 100, 20, hwnd_, nullptr, hInst_, nullptr);

    h_rb_sector_abs_ = CreateWindowExW(0, L"BUTTON", L"Absolute",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        440, 44, 80, 22, hwnd_, (HMENU)IDC_RB_SECTOR_ABS, hInst_, nullptr);
    SendMessageW(h_rb_sector_abs_, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(h_rb_sector_abs_, BM_SETCHECK, BST_CHECKED, 0);

    h_rb_sector_pct_ = CreateWindowExW(0, L"BUTTON", L"Percentage %",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        525, 44, 110, 22, hwnd_, (HMENU)IDC_RB_SECTOR_PCT, hInst_, nullptr);
    SendMessageW(h_rb_sector_pct_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Row 3 (y=74): Save directories ---
    CreateWindowExW(0, L"STATIC", L"Save:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        10, 78, 50, 20, hwnd_, nullptr, hInst_, nullptr);

    h_btn_save_dirs_ = CreateWindowExW(0, L"BUTTON", L"Save Dirs...",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        65, 74, 110, 26, hwnd_, (HMENU)IDC_BTN_SAVE_DIRS, hInst_, nullptr);
    SendMessageW(h_btn_save_dirs_, WM_SETFONT, (WPARAM)hFont, TRUE);

    h_st_save_info_ = CreateWindowExW(0, L"STATIC", L"No save directory selected",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        185, 78, 500, 20, hwnd_, (HMENU)IDC_ST_SAVE_INFO, hInst_, nullptr);
    SendMessageW(h_st_save_info_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Row 4 (y=106): Start/Pause and Stop ---
    h_btn_start_ = CreateWindowExW(0, L"BUTTON", L"Start",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        65, 106, 120, 28, hwnd_, (HMENU)IDC_BTN_START, hInst_, nullptr);
    SendMessageW(h_btn_start_, WM_SETFONT, (WPARAM)hFont, TRUE);

    h_btn_stop_ = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
        195, 106, 80, 28, hwnd_, (HMENU)IDC_BTN_STOP, hInst_, nullptr);
    SendMessageW(h_btn_stop_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- ListView ---
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    h_list_files_ = CreateWindowExW(0, WC_LISTVIEW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
        10, 140, WIN_WIDTH - 20, WIN_HEIGHT - 260,
        hwnd_, (HMENU)IDC_LV_FILES, hInst_, nullptr);

    ListView_SetExtendedListViewStyle(h_list_files_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    struct ColDef { const wchar_t* name; int width; };
    ColDef cols[] = {
        { L"File Name", 280 },
        { L"Type", 60 },
        { L"Size", 100 },
        { L"Save Path", 250 },
        { L"Status", 80 },
    };
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.cx = cols[i].width;
        col.pszText = const_cast<LPWSTR>(cols[i].name);
        ListView_InsertColumn(h_list_files_, i, &col);
    }

    // --- Progress bar ---
    h_progress_ = CreateWindowExW(0, PROGRESS_CLASS, L"",
        WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
        10, WIN_HEIGHT - 110, WIN_WIDTH - 20, 20,
        hwnd_, (HMENU)IDC_PB_PROGRESS, hInst_, nullptr);

    // --- Status bar ---
    h_status_bar_ = CreateWindowExW(0, STATUSCLASSNAME, L"Ready",
        WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP,
        0, WIN_HEIGHT - 80, WIN_WIDTH, 22,
        hwnd_, (HMENU)IDC_STATUS_BAR, hInst_, nullptr);
}

void MainWindow::refresh_drive_list() {
    if (!h_combo_drives_) return;
    SendMessageW(h_combo_drives_, CB_RESETCONTENT, 0, 0);
    drives_ = DiskInfoQuery::EnumeratePhysicalDisks();

    for (size_t i = 0; i < drives_.size(); ++i) {
        auto& d = drives_[i];
        std::wstring label = d.device_path + L" - " + FormatFileSize(d.disk_size_bytes);
        if (!d.model_name.empty()) label += L" (" + d.model_name + L")";
        SendMessageW(h_combo_drives_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!drives_.empty()) {
        SendMessageW(h_combo_drives_, CB_SETCURSEL, 0, 0);
    }
}

void MainWindow::on_start_pause() {
    switch (state_) {
    case State::Idle: {
        // Validate drive selection
        int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(drives_.size())) {
            MessageBoxW(hwnd_, L"Please select a drive.", L"Error", MB_OK);
            return;
        }

        // Validate save directories
        if (save_dirs_.empty()) {
            MessageBoxW(hwnd_, L"Please select at least one save directory.", L"Error", MB_OK);
            on_save_dirs();
            if (save_dirs_.empty()) return;
        }

        auto& drive = drives_[sel];

        // Build config
        ScanAndRecoverManager::Config config;
        config.device_path = drive.device_path;
        config.session_id = GenerateSessionId();

        // Scan mode
        int mode_sel = static_cast<int>(SendMessageW(h_cb_scan_mode_, CB_GETCURSEL, 0, 0));
        switch (mode_sel) {
        case 0: config.mode = ScanMode::Quick; break;
        case 1: config.mode = ScanMode::Deep; break;
        case 2: config.mode = ScanMode::Full; break;
        default: config.mode = ScanMode::Deep; break;
        }

        // File type filters
        config.scan_images = (SendMessageW(h_chk_images_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        config.scan_videos = (SendMessageW(h_chk_videos_, BM_GETCHECK, 0, 0) == BST_CHECKED);

        // Sector range
        wchar_t buf_start[32] = {}, buf_end[32] = {};
        GetWindowTextW(h_ed_start_sector_, buf_start, 32);
        GetWindowTextW(h_ed_end_sector_, buf_end, 32);

        uint64_t user_start = 0, user_end = 0;
        if (wcslen(buf_start) > 0) user_start = _wtoi64(buf_start);
        if (wcslen(buf_end) > 0) user_end = _wtoi64(buf_end);

        bool is_pct = (SendMessageW(h_rb_sector_pct_, BM_GETCHECK, 0, 0) == BST_CHECKED);
        uint64_t total_sectors = drive.geometry.total_sectors;

        if (is_pct && total_sectors > 0) {
            config.start_sector = user_start * total_sectors / 100;
            config.end_sector = (user_end > 0) ? (user_end * total_sectors / 100) : 0;
        } else {
            config.start_sector = user_start;
            config.end_sector = user_end;  // 0 = auto-detect full disk
        }

        // Output directories
        for (const auto& sd : save_dirs_) {
            config.output_dirs.push_back(sd.path);
        }

        // Min free space
        config.min_free_space = 2ULL * 1024 * 1024 * 1024;  // 2GB

        // HWND for PostMessage
        config.hwnd = hwnd_;

        // File recovered callback
        config.on_file_recovered = [this](const RecoverableFile& file, const std::wstring& save_path) {
            auto* data = new FileRecoveredData{file, save_path};
            PostMessageW(hwnd_, WM_FILE_RECOVERED, reinterpret_cast<WPARAM>(data), 0);
        };

        // Progress callback
        config.on_progress = [this](const ScanAndRecoverManager::Progress& p) {
            update_progress(p);
        };

        // Clear previous results
        ListView_DeleteAllItems(h_list_files_);

        // Start
        manager_ = std::make_unique<ScanAndRecoverManager>();
        if (!manager_->start(config)) {
            set_status(L"Failed to start scan & recover");
            return;
        }

        state_ = State::Running;
        SetWindowTextW(h_btn_start_, L"Pause");
        EnableWindow(h_btn_stop_, TRUE);
        enable_config_controls(false);
        set_status(L"Scanning and recovering...");
        break;
    }

    case State::Running:
        // Pause
        if (manager_) manager_->pause();
        state_ = State::Paused;
        SetWindowTextW(h_btn_start_, L"Continue");
        set_status(L"Paused");
        break;

    case State::Paused:
        // Resume
        if (manager_) manager_->resume();
        state_ = State::Running;
        SetWindowTextW(h_btn_start_, L"Pause");
        set_status(L"Resuming scan & recover...");
        break;
    }
}

void MainWindow::on_stop() {
    if (manager_) {
        // Request stop but do NOT join the worker thread here.
        // join() would block the GUI thread, causing deadlock if the worker
        // is currently calling SendMessageW (synchronous) from a callback.
        // The worker will post WM_SCAN_RECOVER_COMPLETE when it exits,
        // and we handle cleanup there. For a user-initiated stop, we
        // set state immediately so the UI is responsive.
        manager_->stop_request_only();
    }
    state_ = State::Idle;
    SetWindowTextW(h_btn_start_, L"Start");
    EnableWindow(h_btn_stop_, FALSE);
    enable_config_controls(true);
    set_status(L"Stopped");
}

void MainWindow::on_save_dirs() {
    // Compute excluded drive letters from selected source disk
    std::vector<wchar_t> excluded_letters;
    int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(drives_.size())) {
        excluded_letters = DiskInfoQuery::GetDriveLettersForPhysicalDrive(drives_[sel].device_path);
    }

    if (SaveDirsDialog::Show(hwnd_, excluded_letters, save_dirs_)) {
        // Update summary label
        if (save_dirs_.empty()) {
            SetWindowTextW(h_st_save_info_, L"No save directory selected");
        } else {
            std::wstring info = std::to_wstring(save_dirs_.size()) + L" dir(s) selected";
            for (const auto& sd : save_dirs_) {
                info += L" | " + sd.path + L" (" + FormatFileSize(sd.free_bytes) + L")";
            }
            SetWindowTextW(h_st_save_info_, info.c_str());
        }
    }
}

void MainWindow::update_progress(const ScanAndRecoverManager::Progress& progress) {
    PostMessageW(h_progress_, PBM_SETPOS, progress.percent, 0);

    // Use PostMessageW for status text to avoid deadlock:
    // This callback runs on the worker thread. SendMessageW (synchronous)
    // would block until the GUI thread processes it, but the GUI thread
    // might be blocked on join() — classic deadlock.
    // We allocate the text on the heap; the GUI thread frees it.
    wchar_t* text = new wchar_t[256];
    _snwprintf_s(text, 256, _TRUNCATE,
        L"Progress: %u%% | Found: %u | Recovered: %u | Failed: %u | Size: %s | Bad: %u",
        progress.percent, progress.files_found, progress.files_recovered,
        progress.files_failed, FormatFileSize(progress.bytes_recovered).c_str(),
        progress.bad_sectors);
    PostMessageW(hwnd_, WM_SETSTATUS, reinterpret_cast<WPARAM>(text), 0);
}

void MainWindow::add_file_to_list(const RecoverableFile& file, const std::wstring& save_path) {
    int idx = ListView_GetItemCount(h_list_files_);

    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = idx;
    item.pszText = const_cast<LPWSTR>(file.file_name.c_str());
    ListView_InsertItem(h_list_files_, &item);

    const wchar_t* type = file.file_type == FileType::Image ? L"Image"
                        : file.file_type == FileType::Video ? L"Video"
                        : L"Other";
    ListView_SetItemText(h_list_files_, idx, 1, const_cast<LPWSTR>(type));

    std::wstring size_str = FormatFileSize(file.file_size);
    ListView_SetItemText(h_list_files_, idx, 2, const_cast<LPWSTR>(size_str.c_str()));

    // Show the directory portion of the save path (not the filename)
    std::wstring dir_path = save_path;
    size_t last_slash = dir_path.find_last_of(L"\\/");
    if (last_slash != std::wstring::npos) {
        dir_path = dir_path.substr(0, last_slash);
    }
    ListView_SetItemText(h_list_files_, idx, 3, const_cast<LPWSTR>(dir_path.c_str()));

    const wchar_t* status = file.is_corrupted ? L"Damaged" : L"OK";
    ListView_SetItemText(h_list_files_, idx, 4, const_cast<LPWSTR>(status));
}

void MainWindow::set_status(const std::wstring& text) {
    if (h_status_bar_) {
        SendMessageW(h_status_bar_, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void MainWindow::enable_config_controls(bool enabled) {
    EnableWindow(h_combo_drives_, enabled);
    EnableWindow(h_cb_scan_mode_, enabled);
    EnableWindow(h_chk_images_, enabled);
    EnableWindow(h_chk_videos_, enabled);
    EnableWindow(h_ed_start_sector_, enabled);
    EnableWindow(h_ed_end_sector_, enabled);
    EnableWindow(h_rb_sector_abs_, enabled);
    EnableWindow(h_rb_sector_pct_, enabled);
    EnableWindow(h_btn_save_dirs_, enabled);
}

} // namespace disk_recover
