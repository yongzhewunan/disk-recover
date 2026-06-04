#include "main_window.hpp"
#include "disk-io/buffered_reader.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "filesystem/raw/signature_scanner.hpp"
#include "common/logger.hpp"
#include <shlobj.h>
#include <algorithm>

namespace disk_recover {

static const wchar_t CLASS_NAME[] = L"DiskRecoverMainWindow";
static const int WIN_WIDTH = 900;
static const int WIN_HEIGHT = 600;

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
    stop_operation();
    if (worker_thread_.joinable()) worker_thread_.join();
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
        case IDC_BTN_SCAN:         on_scan(); break;
        case IDC_BTN_RECOVER:      on_recover(); break;
        case IDC_BTN_SCAN_RECOVER: on_scan_recover(); break;
        case IDC_BTN_STOP:         on_stop(); break;
        case IDC_BTN_BROWSE:       on_browse_output(); break;
        }
        break;
    }
    case WM_SIZE: {
        if (h_list_files_) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            MoveWindow(h_list_files_, 10, 100, rc.right - 20, rc.bottom - 160, TRUE);
        }
        break;
    }
    case WM_DESTROY:
        stop_operation();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    return 0;
}

void MainWindow::create_controls() {
    // Drive selector
    CreateWindowExW(0, L"STATIC", L"Drive:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        10, 12, 50, 22, hwnd_, nullptr, hInst_, nullptr);

    h_combo_drives_ = CreateWindowExW(0, WC_COMBOBOX, L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        65, 10, 200, 300, hwnd_, (HMENU)IDC_CB_DRIVES, hInst_, nullptr);

    // Scan button
    h_btn_scan_ = CreateWindowExW(0, L"BUTTON", L"Scan",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        280, 10, 80, 28, hwnd_, (HMENU)IDC_BTN_SCAN, hInst_, nullptr);

    // Scan & Recover button
    h_btn_scan_recover_ = CreateWindowExW(0, L"BUTTON", L"Scan & Recover",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        370, 10, 130, 28, hwnd_, (HMENU)IDC_BTN_SCAN_RECOVER, hInst_, nullptr);

    // Recover button
    h_btn_recover_ = CreateWindowExW(0, L"BUTTON", L"Recover",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        510, 10, 80, 28, hwnd_, (HMENU)IDC_BTN_RECOVER, hInst_, nullptr);

    // Stop button
    h_btn_stop_ = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
        600, 10, 70, 28, hwnd_, (HMENU)IDC_BTN_STOP, hInst_, nullptr);

    // Output directory
    CreateWindowExW(0, L"STATIC", L"Output:",
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        10, 50, 50, 22, hwnd_, nullptr, hInst_, nullptr);

    h_ed_output_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        65, 48, 550, 24, hwnd_, nullptr, hInst_, nullptr);

    h_btn_browse_ = CreateWindowExW(0, L"BUTTON", L"...",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        620, 48, 40, 24, hwnd_, (HMENU)IDC_BTN_BROWSE, hInst_, nullptr);

    // File list (ListView)
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    h_list_files_ = CreateWindowExW(0, WC_LISTVIEW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
        10, 100, WIN_WIDTH - 20, WIN_HEIGHT - 200,
        hwnd_, (HMENU)IDC_LV_FILES, hInst_, nullptr);

    ListView_SetExtendedListViewStyle(h_list_files_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    struct ColDef { const wchar_t* name; int width; };
    ColDef cols[] = {
        { L"File Name", 300 },
        { L"Type", 60 },
        { L"Size", 100 },
        { L"Sector", 100 },
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

    // Progress bar
    h_progress_ = CreateWindowExW(0, PROGRESS_CLASS, L"",
        WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
        10, WIN_HEIGHT - 90, WIN_WIDTH - 20, 20,
        hwnd_, (HMENU)IDC_PB_PROGRESS, hInst_, nullptr);

    // Status bar
    h_status_bar_ = CreateWindowExW(0, STATUSCLASSNAME, L"Ready",
        WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP,
        0, WIN_HEIGHT - 60, WIN_WIDTH, 22,
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

void MainWindow::on_scan() {
    if (is_scanning_) return;
    if (worker_thread_.joinable()) worker_thread_.join();

    int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(drives_.size())) {
        MessageBoxW(hwnd_, L"Please select a drive.", L"Error", MB_OK);
        return;
    }

    found_files_.clear();
    ListView_DeleteAllItems(h_list_files_);
    enable_buttons(true);
    is_scanning_ = true;

    auto& drive = drives_[sel];
    worker_thread_ = std::thread([this, drive]() { do_scan(); });
}

void MainWindow::on_scan_recover() {
    if (is_scanning_) return;
    if (output_dir_.empty()) {
        on_browse_output();
        if (output_dir_.empty()) return;
    }
    if (worker_thread_.joinable()) worker_thread_.join();

    int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(drives_.size())) {
        MessageBoxW(hwnd_, L"Please select a drive.", L"Error", MB_OK);
        return;
    }

    found_files_.clear();
    ListView_DeleteAllItems(h_list_files_);
    enable_buttons(true);
    is_scanning_ = true;

    worker_thread_ = std::thread([this]() { do_scan_recover(); });
}

void MainWindow::on_recover() {
    if (is_scanning_) return;
    if (found_files_.empty()) {
        MessageBoxW(hwnd_, L"No files to recover. Please scan first.", L"Error", MB_OK);
        return;
    }
    if (output_dir_.empty()) {
        on_browse_output();
        if (output_dir_.empty()) return;
    }
    MessageBoxW(hwnd_, L"Recovery from cached files is not yet implemented. Use Scan & Recover instead.",
                L"Info", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::on_stop() {
    stop_operation();
    set_status(L"Stopped");
    enable_buttons(false);
    is_scanning_ = false;
}

void MainWindow::on_browse_output() {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"Select output directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            output_dir_ = path;
            SetWindowTextW(h_ed_output_, output_dir_.c_str());
        }
        CoTaskMemFree(pidl);
    }
}

void MainWindow::do_scan() {
    int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(drives_.size())) return;

    auto& drive = drives_[sel];
    set_status(L"Opening drive...");

    DiskHandle handle;
    if (!handle.open(drive.device_path)) {
        set_status(L"Failed to open drive");
        enable_buttons(false);
        is_scanning_ = false;
        return;
    }

    uint32_t sector_size = drive.geometry.sector_size ? drive.geometry.sector_size : 512;
    SectorReader reader(handle, sector_size);
    SignatureScanner scanner;

    SignatureScanner::ScanConfig config;
    config.start_sector = 0;
    config.end_sector = drive.geometry.total_sectors;
    config.step_sectors = 1;
    config.scan_images = true;
    config.scan_videos = true;
    config.should_stop = [this]() { return stop_flag_.load(); };

    set_status(L"Scanning...");

    auto on_file = [this](RecoverableFile&& file) {
        found_files_.push_back(std::move(file));
        const auto& f = found_files_.back();
        add_file_to_list(f);
    };

    auto on_progress = [this](const ScanProgress& p) {
        update_progress(p);
    };

    scanner.scan(reader, config, on_file, on_progress);

    set_status(L"Scan complete - " + std::to_wstring(found_files_.size()) + L" files found");
    enable_buttons(false);
    is_scanning_ = false;
}

void MainWindow::do_scan_recover() {
    int sel = static_cast<int>(SendMessageW(h_combo_drives_, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(drives_.size())) return;

    auto& drive = drives_[sel];
    set_status(L"Starting scan & recover...");

    ScanAndRecoverManager mgr;
    ScanAndRecoverManager::Config config;
    config.device_path = drive.device_path;
    config.output_dirs.push_back(output_dir_);
    config.scan_images = true;
    config.scan_videos = true;
    config.session_id = GenerateSessionId();

    mgr.set_progress_callback([this](const ScanAndRecoverManager::Progress& p) {
        wchar_t text[256];
        _snwprintf_s(text, _TRUNCATE, L"Progress: %u%% | Files: %u | Recovered: %u | Failed: %u | Bad: %u",
                     p.percent, p.files_found, p.files_recovered, p.files_failed, p.bad_sectors);
        set_status(text);
        PostMessageW(h_progress_, PBM_SETPOS, p.percent, 0);
    });

    if (!mgr.start(config)) {
        set_status(L"Failed to start scan & recover");
        enable_buttons(false);
        is_scanning_ = false;
        return;
    }

    set_status(L"Scan & Recover running...");

    while (mgr.is_running()) {
        if (stop_flag_.load()) {
            mgr.stop();
            break;
        }
        Sleep(200);
    }

    auto p = mgr.progress();
    wchar_t result[512];
    _snwprintf_s(result, _TRUNCATE,
        L"Scan & Recover complete!\n\nFiles found: %u\nRecovered: %u\nFailed: %u\nBytes: %s\nBad sectors: %u",
        p.files_found, p.files_recovered, p.files_failed,
        FormatFileSize(p.bytes_recovered).c_str(), p.bad_sectors);

    set_status(L"Scan & Recover complete");
    enable_buttons(false);
    is_scanning_ = false;

    MessageBoxW(hwnd_, result, L"Complete", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::stop_operation() {
    stop_flag_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    stop_flag_ = false;
}

void MainWindow::update_progress(const ScanProgress& progress) {
    if (!h_progress_ || !h_status_bar_) return;

    uint64_t total = progress.total_sectors;
    uint64_t scanned = progress.sectors_scanned;

    if (total > 0) {
        int pct = static_cast<int>((scanned * 100) / total);
        PostMessageW(h_progress_, PBM_SETPOS, pct, 0);
    }

    wchar_t text[256];
    _snwprintf_s(text, _TRUNCATE, L"Scanned: %llu / %llu MB | Files: %u | Bad: %u",
                 scanned / 2048, total / 2048, progress.files_found, progress.bad_sectors_hit);
    set_status(text);
}

void MainWindow::add_file_to_list(const RecoverableFile& file) {
    int idx = ListView_GetItemCount(h_list_files_);

    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = idx;
    item.pszText = const_cast<LPWSTR>(file.file_name.c_str());
    ListView_InsertItem(h_list_files_, &item);

    const wchar_t* type = file.file_type == FileType::Image ? L"Image" : L"Video";
    ListView_SetItemText(h_list_files_, idx, 1, const_cast<LPWSTR>(type));

    std::wstring size_str = FormatFileSize(file.file_size);
    ListView_SetItemText(h_list_files_, idx, 2, const_cast<LPWSTR>(size_str.c_str()));

    if (!file.fragments.empty()) {
        wchar_t sector_str[32];
        _snwprintf_s(sector_str, _TRUNCATE, L"%llu", file.fragments[0].start_sector);
        ListView_SetItemText(h_list_files_, idx, 3, sector_str);
    }

    const wchar_t* status = file.is_corrupted ? L"Damaged" : L"OK";
    ListView_SetItemText(h_list_files_, idx, 4, const_cast<LPWSTR>(status));
}

void MainWindow::set_status(const std::wstring& text) {
    if (h_status_bar_) {
        SendMessageW(h_status_bar_, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void MainWindow::enable_buttons(bool scanning) {
    EnableWindow(h_btn_scan_, !scanning);
    EnableWindow(h_btn_recover_, !scanning && !found_files_.empty());
    EnableWindow(h_btn_scan_recover_, !scanning);
    EnableWindow(h_btn_stop_, scanning);
    EnableWindow(h_combo_drives_, !scanning);
}

} // namespace disk_recover