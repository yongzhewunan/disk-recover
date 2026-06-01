#include "main_window.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <shlobj.h>  // For folder browse dialog
#include <chrono>
#include <random>
#include <sstream>

#include "disk-io/disk_handle.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/aligned_buffer.hpp"
#include "business/multi_target_writer.hpp"
#include "common/logger.hpp"

// For admin check
#include <sddl.h>

namespace disk_recover::gui {

// Helper: Convert integer to wstring
static std::wstring IntToWStr(int value) {
    wchar_t buf[32];
    _snwprintf_s(buf, _TRUNCATE, L"%d", value);
    return buf;
}

// Helper: Format file size
static std::wstring FormatSize(uint64_t bytes) {
    wchar_t buf[32];
    if (bytes >= 1024ULL * 1024 * 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.2f MB", bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.2f KB", bytes / 1024.0);
    } else {
        _snwprintf_s(buf, _TRUNCATE, L"%llu B", bytes);
    }
    return buf;
}

// Helper: Get file type string
static const wchar_t* FileTypeToString(FileType type) {
    switch (type) {
        case FileType::Image: return L"图片";
        case FileType::Video: return L"视频";
        default: return L"未知";
    }
}

// Helper: Generate unique session ID
static std::string GenerateSessionId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::ostringstream oss;
    oss << "session_" << ms << "_" << dis(gen);
    return oss.str();
}

// Helper: Get temp path for database
static std::wstring GetTempDbPath() {
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dbPath = tempPath;
    dbPath += L"disk_recover_scan.db";
    return dbPath;
}

MainWindow::~MainWindow() {
    // Stop any ongoing scan before destruction
    if (scanManager_ && scanManager_->is_scanning()) {
        scanManager_->stop_scan();
    }
}

bool MainWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(MainWindow*);  // Store 'this' pointer
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MAINFRAME);
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE hInst, int cmdShow) {
    hInst_ = hInst;

    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // Create main window
    hwnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"磁盘数据恢复 - Disk Recover",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,  // Initial size
        nullptr,
        nullptr,
        hInst,
        this  // Pass 'this' to WM_CREATE
    );

    if (!hwnd_) {
        return false;
    }

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::RunMessageLoop() {
    MSG msg;
    HACCEL hAccel = LoadAcceleratorsW(hInst_, MAKEINTRESOURCE(IDR_MAINFRAME));

    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!hAccel || !TranslateAcceleratorW(hwnd_, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) {
        DestroyAcceleratorTable(hAccel);
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;

    if (msg == WM_CREATE) {
        // Store 'this' pointer in window extra bytes
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(self));

        // IMPORTANT: Set hwnd_ BEFORE OnCreate, because child controls need it
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, 0));
    }

    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_CREATE:
            self->OnCreate();
            return 0;

        case WM_SIZE:
            self->OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_COMMAND:
            self->OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;

        case WM_NOTIFY:
            self->OnNotify(reinterpret_cast<LPNMHDR>(lParam));
            return 0;

        case WM_DESTROY:
            self->OnDestroy();
            PostQuitMessage(0);
            return 0;

        case WM_SCAN_PROGRESS:
            if (lParam) {
                // Use unique_ptr for RAII cleanup - ensures deletion even if
                // OnScanProgress throws or window is being destroyed
                auto progressPtr = std::unique_ptr<ScanProgress>(
                    reinterpret_cast<ScanProgress*>(lParam));
                if (self->hwnd_) {
                    self->OnScanProgress(*progressPtr);
                }
            }
            return 0;

        case WM_SCAN_COMPLETE:
            self->OnScanComplete();
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::OnCreate() {
    // Initialize logger
    LOG_INIT(L"");
    LOG_MSG(L"[MainWindow] OnCreate started");

    // Mark window as alive for safe PostMessage from scan thread
    windowAlive_ = std::make_shared<std::atomic<bool>>(true);

    // Initialize business logic managers
    scanManager_ = std::make_unique<ScanManager>();
    recoverManager_ = std::make_unique<RecoverManager>();
    previewManager_ = std::make_unique<business::PreviewManager>();

    LOG_MSG(L"[MainWindow] Business managers initialized");

    // Set up scan callbacks with thread-safe UI updates via PostMessage
    auto aliveFlag = windowAlive_;  // Copy shared_ptr for lambda capture
    scanManager_->set_progress_callback([this, aliveFlag](const ScanProgress& progress) {
        // If window is already destroyed, don't bother posting
        if (!aliveFlag->load()) return;

        // Post message to UI thread (allocate copy on heap)
        ScanProgress* p = new ScanProgress(progress);
        if (!PostMessageW(hwnd_, WM_SCAN_PROGRESS, 0, reinterpret_cast<LPARAM>(p))) {
            // PostMessage failed (window destroyed or queue full) - free memory
            delete p;
        }
    });

    // Batch file updates via progress callback (no per-file messages)
    scanManager_->set_file_found_callback(nullptr);

    LOG_MSG(L"[MainWindow] Scan callbacks set up");

    // Row 1: Disk and partition selection
    int row1Y = MARGIN;
    int col1X = MARGIN;

    // Disk label and ComboBox
    hDiskLabel_ = CreateLabel(hwnd_, L"磁盘:", col1X, row1Y, DISK_LABEL_W, CONTROL_HEIGHT);
    hDiskList_ = CreateComboBox(hwnd_, IDC_DISK_LIST, col1X + DISK_LABEL_W + 4, row1Y, COMBO_WIDTH, CONTROL_HEIGHT + 200);
    LOG_FMT(L"[MainWindow] Created disk list ComboBox: hwnd=%p", hDiskList_);
    col1X += DISK_LABEL_W + 4 + COMBO_WIDTH + MARGIN;

    // Partition label and ComboBox
    hPartitionLabel_ = CreateLabel(hwnd_, L"分区:", col1X, row1Y, PART_LABEL_W, CONTROL_HEIGHT);
    hPartitionList_ = CreateComboBox(hwnd_, IDC_PARTITION_LIST, col1X + PART_LABEL_W + 4, row1Y, COMBO_WIDTH - 86, CONTROL_HEIGHT + 200);
    LOG_FMT(L"[MainWindow] Created partition list ComboBox: hwnd=%p", hPartitionList_);
    col1X += PART_LABEL_W + 4 + COMBO_WIDTH - 86 + MARGIN;

    // Buttons
    hScanBtn_ = CreateButton(hwnd_, L"扫描", IDC_SCAN_BTN, col1X, row1Y, BUTTON_WIDTH, CONTROL_HEIGHT);
    col1X += BUTTON_WIDTH + 6;
    hPauseBtn_ = CreateButton(hwnd_, L"暂停", IDC_PAUSE_BTN, col1X, row1Y, BUTTON_WIDTH, CONTROL_HEIGHT);
    EnableWindow(hPauseBtn_, FALSE);
    col1X += BUTTON_WIDTH + 6;
    hRecoverBtn_ = CreateButton(hwnd_, L"恢复", IDC_RECOVER_BTN, col1X, row1Y, BUTTON_WIDTH, CONTROL_HEIGHT);
    EnableWindow(hRecoverBtn_, FALSE);
    col1X += BUTTON_WIDTH + 6;
    hStopBtn_ = CreateButton(hwnd_, L"停止", IDM_STOP, col1X, row1Y, BUTTON_WIDTH, CONTROL_HEIGHT);
    EnableWindow(hStopBtn_, FALSE);

    // Row 2: Configuration controls
    int row2Y = row1Y + CONTROL_HEIGHT + 4;
    int col2X = MARGIN;

    // Scan mode: ComboBox only (items include "Quick/Deep/Full" prefix)
    hScanModeCombo_ = CreateComboBox(hwnd_, IDC_SCAN_MODE, col2X, row2Y, 110, CONTROL_HEIGHT + 200);
    ComboBox_AddString(hScanModeCombo_, L"快速扫描");
    ComboBox_AddString(hScanModeCombo_, L"深度扫描");
    ComboBox_AddString(hScanModeCombo_, L"完整扫描");
    ComboBox_SetCurSel(hScanModeCombo_, 0);
    col2X += 110 + 8;

    // File type checkboxes (no control IDs needed - we read state directly)
    hScanImagesCheck_ = CreateWindowW(
        L"BUTTON", L"图片", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        col2X, row2Y, 70, CONTROL_HEIGHT, hwnd_, nullptr, hInst_, nullptr);
    Button_SetCheck(hScanImagesCheck_, BST_CHECKED);
    col2X += 70 + 8;

    hScanVideosCheck_ = CreateWindowW(
        L"BUTTON", L"视频", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        col2X, row2Y, 70, CONTROL_HEIGHT, hwnd_, nullptr, hInst_, nullptr);
    Button_SetCheck(hScanVideosCheck_, BST_CHECKED);
    col2X += 70 + 8;

    // Bad sector policy label and combo
    CreateLabel(hwnd_, L"Bad Sectors:", col2X, row2Y, 150, CONTROL_HEIGHT);
    hBadSectorCombo_ = CreateComboBox(hwnd_, IDC_BAD_SECTOR_POLICY, col2X + 150 + 4, row2Y, 120, CONTROL_HEIGHT + 200);
    ComboBox_AddString(hBadSectorCombo_, L"跳过");
    ComboBox_AddString(hBadSectorCombo_, L"重试3次");
    ComboBox_AddString(hBadSectorCombo_, L"尽力读取");
    ComboBox_SetCurSel(hBadSectorCombo_, 0);
    col2X += 150 + 4 + 120 + 8;

    // Bad sector count panel
    hBadSectorPanel_ = CreateWindowW(
        L"STATIC", L"Bad: 0",
        WS_VISIBLE | WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN,
        col2X, row2Y, 90, CONTROL_HEIGHT,
        hwnd_, nullptr, hInst_, nullptr);

    // Row 3: File list and preview (starts after config row)
    int row3Y = row2Y + CONTROL_HEIGHT + 4;
    hFileList_ = CreateListView(hwnd_, IDC_FILE_LIST, MARGIN, row3Y, 700, 400);
    SetupListViewColumns();

    // Preview panel on the right
    hPreviewLabel_ = CreateLabel(hwnd_, L"预览:", MARGIN + 700 + MARGIN, row3Y, PREVIEW_WIDTH, CONTROL_HEIGHT);
    hPreview_ = CreateStatic(hwnd_, IDC_PREVIEW,
                             MARGIN + 700 + MARGIN, row3Y + CONTROL_HEIGHT + 4,
                             PREVIEW_WIDTH, PREVIEW_WIDTH);
    SetWindowTextW(hPreview_, L"No preview");

    // Status bar
    hStatusBar_ = CreateStatusBar(hwnd_, IDC_STATUSBAR);

    LOG_MSG(L"[MainWindow] UI controls created, calling RefreshDiskList");

    // Populate disk list with real disk information
    RefreshDiskList();

    LOG_FMT(L"[MainWindow] RefreshDiskList completed, %zu disks found", cachedDisks_.size());

    UpdateStatus(L"Ready. Select a disk and click Scan.");
}

void MainWindow::OnSize(int cx, int cy) {
    if (!hFileList_) return;  // Controls not created yet

    // Calculate file list area
    // Two header rows: disk/partition row + config row
    int headerHeight = 2 * (CONTROL_HEIGHT + 4) + MARGIN;
    int fileListY = headerHeight;
    int bottomY = cy - STATUSBAR_HEIGHT - 4;

    // File list width - leave space for preview
    int fileListWidth = cx - PREVIEW_WIDTH - 3 * MARGIN;
    int fileListHeight = bottomY - fileListY - 4;

    // Resize file list
    SetWindowPos(hFileList_, nullptr, MARGIN, fileListY,
                 std::max(100, fileListWidth), std::max(100, fileListHeight),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Move preview controls
    int previewX = MARGIN + fileListWidth + 2 * MARGIN;
    SetWindowPos(hPreviewLabel_, nullptr, previewX, fileListY, PREVIEW_WIDTH, CONTROL_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hPreview_, nullptr, previewX, fileListY + CONTROL_HEIGHT + 4,
                 PREVIEW_WIDTH, std::max(100, fileListHeight - CONTROL_HEIGHT - 4),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Resize status bar
    SetWindowPos(hStatusBar_, nullptr, 0, cy - STATUSBAR_HEIGHT, cx, STATUSBAR_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Move progress bar within status bar
    if (hProgressBar_) {
        RECT rcStatus;
        GetClientRect(hStatusBar_, &rcStatus);
        SetWindowPos(hProgressBar_, nullptr, rcStatus.right - 200, 2, 180, 18,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::OnCommand(int id, int notifyCode, HWND hCtrl) {
    switch (id) {
        // Button commands
        case IDC_SCAN_BTN:
            StartScan();
            break;

        case IDC_PAUSE_BTN:
            TogglePause();
            break;

        case IDC_RECOVER_BTN:
            StartRecovery();
            break;

        case IDM_STOP:
            StopScan();
            break;

        // Menu commands
        case IDM_SCAN:
            StartScan();
            break;

        case IDM_RECOVER:
            StartRecovery();
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd_);
            break;

        case IDM_ABOUT:
            MessageBoxW(hwnd_,
                L"Disk Recover - Data Recovery Tool\n\n"
                L"Version 1.0\n\n"
                L"A Windows disk data recovery software supporting\n"
                L"NTFS, FAT, and exFAT partitions.\n\n"
                L"Features:\n"
                L"- Scan disks for recoverable images and videos\n"
                L"- Preview thumbnails before recovery\n"
                L"- Recover files to any output folder\n"
                L"- Supports WinPE environment",
                L"About Disk Recover",
                MB_OK | MB_ICONINFORMATION);
            break;

        case IDM_DEMO_DATA:
            LoadDemoData();
            break;

        // ComboBox notifications
        case IDC_DISK_LIST:
            if (notifyCode == CBN_SELCHANGE) {
                // Disk selection changed - update partition list
                const DiskInfo* disk = GetSelectedDisk();
                if (disk) {
                    RefreshPartitionList(*disk);
                }
            }
            break;

        case IDC_PARTITION_LIST:
            if (notifyCode == CBN_SELCHANGE) {
                // Partition selection changed
            }
            break;
    }
}

void MainWindow::OnDestroy() {
    // Signal that the window is being destroyed so scan callback
    // won't PostMessage to a dead window or leak memory
    if (windowAlive_) {
        windowAlive_->store(false);
    }

    // Stop any ongoing scan
    if (scanManager_ && scanManager_->is_scanning()) {
        scanManager_->stop_scan();
    }
    hwnd_ = nullptr;
}

void MainWindow::OnNotify(LPNMHDR nmhdr) {
    if (nmhdr->idFrom == IDC_FILE_LIST && nmhdr->code == LVN_ITEMCHANGED) {
        // Selection changed in file list
        auto* pnmlv = reinterpret_cast<LPNMLISTVIEW>(nmhdr);
        if ((pnmlv->uNewState & LVIS_SELECTED) && !(pnmlv->uOldState & LVIS_SELECTED)) {
            // New item selected - update preview
            UpdatePreview(pnmlv->iItem);
        }
    }
}

void MainWindow::OnScanProgress(const ScanProgress& progress) {
    // Batch-add any new files found since last update
    if (scanManager_) {
        auto newFiles = scanManager_->take_found_files();
        if (!newFiles.empty()) {
            // Suspend redraw during batch insert to prevent UI freeze
            SendMessageW(hFileList_, WM_SETREDRAW, FALSE, 0);
            for (const auto& file : newFiles) {
                foundFiles_.push_back(file);
                AddListViewItem(file, foundFiles_.size() - 1);
            }
            SendMessageW(hFileList_, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(hFileList_, nullptr, TRUE);

            if (!IsWindowEnabled(hRecoverBtn_)) {
                EnableWindow(hRecoverBtn_, TRUE);
            }
        }
    }

    // Check if scan is complete
    if (progress.is_complete) {
        OnScanComplete();
        return;
    }

    // Update progress bar
    if (progress.total_sectors > 0) {
        int percent = static_cast<int>((progress.sectors_scanned * 100) / progress.total_sectors);
        UpdateProgress(percent);
    }

    // Update bad sector count display
    badSectorsCount_ = progress.bad_sectors_hit;
    if (hBadSectorPanel_) {
        wchar_t badSectorText[64];
        _snwprintf_s(badSectorText, _TRUNCATE, L"Bad: %u", progress.bad_sectors_hit);
        SetWindowTextW(hBadSectorPanel_, badSectorText);
    }

    // Update status text
    wchar_t status[256];
    _snwprintf_s(status, _TRUNCATE, L"扫描中... %llu/%llu 扇区, 已找到 %zu 个文件",
                 progress.sectors_scanned, progress.total_sectors, foundFiles_.size());
    UpdateStatus(status);
}

void MainWindow::OnScanComplete() {
    SetWindowTextW(hPauseBtn_, L"暂停");
    EnableControls(false);
    UpdateProgress(100);

    wchar_t status[256];
    if (badSectorsCount_ > 0) {
        _snwprintf_s(status, _TRUNCATE, L"Scan complete. %zu files found, %u bad sectors detected.",
                     foundFiles_.size(), badSectorsCount_);
    } else {
        _snwprintf_s(status, _TRUNCATE, L"Scan complete. %zu files found.", foundFiles_.size());
    }
    UpdateStatus(status);
}

HWND MainWindow::CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(
        0, L"STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, w, h,
        parent, nullptr, hInst_, nullptr
    );
    if (!hwnd) {
        OutputDebugStringW(L"[DiskRecover] Failed to create label control\n");
    }
    return hwnd;
}

HWND MainWindow::CreateComboBox(HWND parent, int id, int x, int y, int w, int h) {
    // ComboBox is a standard Windows control, uses "COMBOBOX" class name
    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,  // Add border for visibility
        WC_COMBOBOX, L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        x, y, w, h,
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );
    if (!hwnd) {
        DWORD err = GetLastError();
        LOG_FMT(L"[MainWindow] CreateComboBox FAILED for id=%d, error=%d", id, err);
    } else {
        LOG_FMT(L"[MainWindow] CreateComboBox SUCCESS for id=%d, hwnd=%p", id, hwnd);
    }
    return hwnd;
}

HWND MainWindow::CreateButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(
        0, L"BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, w, h,
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );
    if (!hwnd) {
        OutputDebugStringW(L"[DiskRecover] Failed to create button control\n");
    }
    return hwnd;
}

HWND MainWindow::CreateListView(HWND parent, int id, int x, int y, int w, int h) {
    HWND hList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
        x, y, w, h,
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );

    if (!hList) {
        OutputDebugStringW(L"[DiskRecover] Failed to create list view control\n");
        return nullptr;
    }

    // Set extended styles
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    return hList;
}

HWND MainWindow::CreateStatic(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        x, y, w, h,
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );
    if (!hwnd) {
        OutputDebugStringW(L"[DiskRecover] Failed to create static control\n");
    }
    return hwnd;
}

HWND MainWindow::CreateStatusBar(HWND parent, int id) {
    HWND hStatus = CreateWindowExW(
        0, STATUSCLASSNAME, L"",
        WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP,
        0, 0, 0, 0,  // Position set in OnSize
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );

    if (!hStatus) {
        OutputDebugStringW(L"[DiskRecover] Failed to create status bar\n");
        return nullptr;
    }

    // Create progress bar as child of status bar
    RECT rc;
    GetClientRect(hStatus, &rc);
    hProgressBar_ = CreateWindowExW(
        0, PROGRESS_CLASS, L"",
        WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
        rc.right - 200, 2, 180, 18,
        hStatus, reinterpret_cast<HMENU>(IDC_PROGRESSBAR), hInst_, nullptr
    );

    if (!hProgressBar_) {
        OutputDebugStringW(L"[DiskRecover] Failed to create progress bar\n");
    }

    return hStatus;
}

void MainWindow::SetupListViewColumns() {
    // Add columns to the ListView
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = const_cast<wchar_t*>(L"Name");
    col.cx = 200;
    col.iSubItem = COL_NAME;
    ListView_InsertColumn(hFileList_, COL_NAME, &col);

    col.pszText = const_cast<wchar_t*>(L"Size");
    col.cx = 100;
    col.iSubItem = COL_SIZE;
    ListView_InsertColumn(hFileList_, COL_SIZE, &col);

    col.pszText = const_cast<wchar_t*>(L"Type");
    col.cx = 80;
    col.iSubItem = COL_TYPE;
    ListView_InsertColumn(hFileList_, COL_TYPE, &col);

    col.pszText = const_cast<wchar_t*>(L"Status");
    col.cx = 120;
    col.iSubItem = COL_STATUS;
    ListView_InsertColumn(hFileList_, COL_STATUS, &col);

    col.pszText = const_cast<wchar_t*>(L"Path");
    col.cx = 150;
    col.iSubItem = COL_PATH;
    ListView_InsertColumn(hFileList_, COL_PATH, &col);
}

void MainWindow::AddListViewItem(const RecoverableFile& file, size_t index) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(hFileList_);
    item.pszText = const_cast<wchar_t*>(file.file_name.c_str());
    item.lParam = static_cast<LPARAM>(index);  // Index in foundFiles_
    int idx = ListView_InsertItem(hFileList_, &item);

    // Set size
    std::wstring sizeStr = FormatSize(file.file_size);
    ListView_SetItemText(hFileList_, idx, COL_SIZE, const_cast<wchar_t*>(sizeStr.c_str()));

    // Set type
    ListView_SetItemText(hFileList_, idx, COL_TYPE, const_cast<wchar_t*>(FileTypeToString(file.file_type)));

    // Set status
    ListView_SetItemText(hFileList_, idx, COL_STATUS,
                         const_cast<wchar_t*>(file.is_corrupted ? L"Corrupted" : L"Good"));

    // Set path — show start sector of first fragment
    if (!file.fragments.empty()) {
        wchar_t pathBuf[64];
        _snwprintf_s(pathBuf, _TRUNCATE, L"Sector %llu", file.fragments[0].start_sector);
        ListView_SetItemText(hFileList_, idx, COL_PATH, pathBuf);
    } else {
        ListView_SetItemText(hFileList_, idx, COL_PATH, const_cast<wchar_t*>(L""));
    }
}

void MainWindow::ClearFileList() {
    ListView_DeleteAllItems(hFileList_);
    foundFiles_.clear();
}

void MainWindow::EnableControls(bool scanning) {
    EnableWindow(hDiskList_, !scanning);
    EnableWindow(hPartitionList_, !scanning);
    EnableWindow(hScanBtn_, !scanning);
    EnableWindow(hRecoverBtn_, !scanning && !foundFiles_.empty());
    EnableWindow(hStopBtn_, scanning);
    EnableWindow(hPauseBtn_, scanning);
    // Disable configuration controls during scanning
    EnableWindow(hScanModeCombo_, !scanning);
    EnableWindow(hScanImagesCheck_, !scanning);
    EnableWindow(hScanVideosCheck_, !scanning);
    EnableWindow(hBadSectorCombo_, !scanning);
}

void MainWindow::UpdateStatus(const wchar_t* text) {
    SetWindowTextW(hStatusBar_, text);
}

void MainWindow::UpdateProgress(int percent) {
    SendMessageW(hProgressBar_, PBM_SETPOS, percent, 0);
}

void MainWindow::RefreshDiskList() {
    LOG_MSG(L"[MainWindow] RefreshDiskList: starting enumeration");

    // Enumerate physical disks
    cachedDisks_ = DiskInfoQuery::EnumeratePhysicalDisks();

    LOG_FMT(L"[MainWindow] RefreshDiskList: found %zu disks", cachedDisks_.size());

    // Clear and populate disk ComboBox
    ComboBox_ResetContent(hDiskList_);

    if (cachedDisks_.empty()) {
        LOG_MSG(L"[MainWindow] No disks found, checking admin status");

        ComboBox_AddString(hDiskList_, L"No disks - Run as Admin");
        ComboBox_SetCurSel(hDiskList_, 0);
        EnableWindow(hScanBtn_, FALSE);

        // Check if running as administrator
        BOOL isAdmin = FALSE;
        PSID adminGroup = nullptr;
        SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
        if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }

        LOG_FMT(L"[MainWindow] Admin check result: isAdmin=%d", isAdmin);

        // Show log file location in message
        std::wstring logPath = Logger::instance().getLogPath();
        std::wstring logInfo = L"\n\nLog file location: " + logPath;

        if (!isAdmin) {
            LOG_MSG(L"[MainWindow] Not running as admin, showing error");
            UpdateStatus(L"ERROR: Run as Administrator to access physical disks.");
            MessageBoxW(hwnd_,
                (L"This application requires Administrator privileges to access physical disks.\n\n"
                 L"Please right-click disk-recover.exe and select \"Run as administrator\"." + logInfo).c_str(),
                L"Administrator Required",
                MB_OK | MB_ICONWARNING);
        } else {
            LOG_MSG(L"[MainWindow] Running as admin but no disks found");
            UpdateStatus(L"No physical disks found on this system.");
            MessageBoxW(hwnd_,
                (L"No physical disks were found on this system.\n\n"
                 L"This could happen if:\n"
                 L"- No disks are connected\n"
                 L"- All disks are in use by other applications\n"
                 L"- Disk drivers are not properly installed" + logInfo).c_str(),
                L"No Disks Found",
                MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    LOG_MSG(L"[MainWindow] Populating disk ComboBox...");

    for (const auto& disk : cachedDisks_) {
        // Format: "PhysicalDrive0 - 256 GB (Samsung SSD 860)"
        std::wstring text = disk.device_path + L" - " + FormatSize(disk.disk_size_bytes);
        if (!disk.model_name.empty()) {
            text += L" (" + disk.model_name + L")";
        }
        LOG_FMT(L"[MainWindow] Adding disk: %s", text.c_str());
        int idx = ComboBox_AddString(hDiskList_, text.c_str());
        LOG_FMT(L"[MainWindow] ComboBox_AddString returned %d", idx);
        ComboBox_SetItemData(hDiskList_, idx, disk.physical_drive_number);
    }

    // Select first disk and update partition list
    LOG_MSG(L"[MainWindow] Setting first disk selection");
    int selResult = ComboBox_SetCurSel(hDiskList_, 0);
    LOG_FMT(L"[MainWindow] ComboBox_SetCurSel returned %d", selResult);

    // Verify the selection
    int verifySel = ComboBox_GetCurSel(hDiskList_);
    LOG_FMT(L"[MainWindow] Verify selection: %d", verifySel);

    // Get the text of the selected item
    wchar_t verifyText[256] = {};
    ComboBox_GetLBText(hDiskList_, 0, verifyText);
    LOG_FMT(L"[MainWindow] First item text: %s", verifyText);

    // Force the ComboBox to show its content
    SetWindowTextW(hDiskList_, verifyText);

    LOG_MSG(L"[MainWindow] Calling RefreshPartitionList");
    RefreshPartitionList(cachedDisks_[0]);

    LOG_MSG(L"[MainWindow] Enabling Scan button");
    EnableWindow(hScanBtn_, TRUE);

    LOG_MSG(L"[MainWindow] RefreshDiskList completed successfully");
}

void MainWindow::RefreshPartitionList(const DiskInfo& disk) {
    LOG_MSG(L"[MainWindow] RefreshPartitionList called");
    ComboBox_ResetContent(hPartitionList_);

    LOG_FMT(L"[MainWindow] Disk has %zu partitions", disk.partitions.size());

    if (disk.partitions.empty()) {
        LOG_MSG(L"[MainWindow] No partitions, adding placeholder");
        ComboBox_AddString(hPartitionList_, L"No partitions");
        ComboBox_SetCurSel(hPartitionList_, 0);
        return;
    }

    for (const auto& part : disk.partitions) {
        std::wstring text = L"Partition " + std::to_wstring(part.index) +
            L" - " + part.filesystem_type;
        if (!part.volume_label.empty()) {
            text += L" (" + part.volume_label + L")";
        }
        LOG_FMT(L"[MainWindow] Adding partition: %s", text.c_str());
        ComboBox_AddString(hPartitionList_, text.c_str());
    }

    ComboBox_SetCurSel(hPartitionList_, 0);
    LOG_MSG(L"[MainWindow] RefreshPartitionList completed");
}

const DiskInfo* MainWindow::GetSelectedDisk() const {
    int sel = ComboBox_GetCurSel(hDiskList_);
    if (sel == CB_ERR || sel < 0 || static_cast<size_t>(sel) >= cachedDisks_.size()) {
        return nullptr;
    }
    return &cachedDisks_[sel];
}

const PartitionInfo* MainWindow::GetSelectedPartition() const {
    const DiskInfo* disk = GetSelectedDisk();
    if (!disk) return nullptr;

    int sel = ComboBox_GetCurSel(hPartitionList_);
    if (sel == CB_ERR || sel < 0 || static_cast<size_t>(sel) >= disk->partitions.size()) {
        return nullptr;
    }
    return &disk->partitions[sel];
}

void MainWindow::StartScan() {
    const DiskInfo* disk = GetSelectedDisk();
    if (!disk) {
        MessageBoxW(hwnd_, L"请先选择一个磁盘。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    // Check if there's a resumable scan from a previous interrupted session
    if (!lastSessionId_.empty() && !lastDbPath_.empty()) {
        ScanCacheDB check_db;
        if (check_db.open(lastDbPath_)) {
            ScanProgress saved;
            if (check_db.load_progress(lastSessionId_, saved) &&
                !saved.is_complete && saved.scan_phase > 0) {
                check_db.close();

                int result = MessageBoxW(hwnd_,
                    L"上次扫描被中断，是否从断点继续扫描？\n\n"
                    L"点击[是]继续扫描，点击[否]重新开始。",
                    L"恢复扫描", MB_YESNO | MB_ICONQUESTION);

                if (result == IDYES) {
                    ScanManager::Config config;
                    config.device_path = disk->device_path;
                    config.db_path = lastDbPath_;
                    config.session_id = lastSessionId_;
                    config.mode = static_cast<ScanMode>(ComboBox_GetCurSel(hScanModeCombo_));
                    config.bad_sector_policy = static_cast<BadSectorPolicy>(ComboBox_GetCurSel(hBadSectorCombo_));
                    config.scan_images = (Button_GetCheck(hScanImagesCheck_) == BST_CHECKED);
                    config.scan_videos = (Button_GetCheck(hScanVideosCheck_) == BST_CHECKED);
                    const PartitionInfo* part = GetSelectedPartition();
                    if (part && part->sector_count > 0) {
                        config.start_sector = part->start_sector;
                        config.end_sector = part->start_sector + part->sector_count;
                    } else {
                        config.start_sector = 0;
                        config.end_sector = disk->geometry.total_sectors;
                    }

                    if (scanManager_->resume_scan(config)) {
                        EnableControls(true);
                        UpdateStatus(L"恢复扫描中...");
                        return;
                    }
                    // Resume failed — fall through to start new scan
                }
            } else {
                check_db.close();
            }
        }
    }

    // Clear previous results for new scan
    ClearFileList();
    UpdateProgress(0);
    badSectorsCount_ = 0;

    if (hBadSectorPanel_) {
        SetWindowTextW(hBadSectorPanel_, L"Bad: 0");
    }

    // Stop any previous scan before starting a new one
    if (scanManager_ && scanManager_->is_scanning()) {
        scanManager_->stop_scan();
    }

    // Configure scan
    ScanManager::Config config;
    config.device_path = disk->device_path;
    config.db_path = GetTempDbPath();
    config.session_id = GenerateSessionId();

    // Save for potential resume
    lastSessionId_ = config.session_id;
    lastDbPath_ = config.db_path;

    int modeIdx = ComboBox_GetCurSel(hScanModeCombo_);
    config.mode = static_cast<ScanMode>(modeIdx);

    int policyIdx = ComboBox_GetCurSel(hBadSectorCombo_);
    config.bad_sector_policy = static_cast<BadSectorPolicy>(policyIdx);

    config.scan_images = (Button_GetCheck(hScanImagesCheck_) == BST_CHECKED);
    config.scan_videos = (Button_GetCheck(hScanVideosCheck_) == BST_CHECKED);

    const PartitionInfo* part = GetSelectedPartition();
    if (part && part->sector_count > 0) {
        config.start_sector = part->start_sector;
        config.end_sector = part->start_sector + part->sector_count;
        LOG_FMT(L"[MainWindow] Scan partition: start=%llu, count=%llu, fs=%s",
                 part->start_sector, part->sector_count, part->filesystem_type.c_str());
    } else {
        config.start_sector = 0;
        config.end_sector = disk->geometry.total_sectors;
        LOG_FMT(L"[MainWindow] Scan entire disk: total_sectors=%llu", disk->geometry.total_sectors);
    }

    LOG_FMT(L"[MainWindow] Scan config: path=%s, mode=%d, images=%d, videos=%d, start=%llu, end=%llu",
             config.device_path.c_str(), static_cast<int>(config.mode),
             config.scan_images, config.scan_videos,
             config.start_sector, config.end_sector);

    if (!scanManager_->start_scan(config)) {
        LOG_MSG(L"[MainWindow] start_scan FAILED");
        MessageBoxW(hwnd_, L"无法启动扫描。", L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    LOG_MSG(L"[MainWindow] Scan started successfully");
    EnableControls(true);
    UpdateStatus(L"扫描中...");
}

void MainWindow::StopScan() {
    if (!scanManager_ || !scanManager_->is_scanning()) return;

    LOG_MSG(L"[MainWindow] Stop scan requested");

    // If paused, resume first so stop_scan can join the thread cleanly
    if (scanManager_->is_paused()) {
        scanManager_->resume_from_pause();
    }

    // stop_scan() sets the flag and waits for the scan thread to finish
    scanManager_->stop_scan();

    // Reset pause button text
    SetWindowTextW(hPauseBtn_, L"暂停");

    UpdateStatus(L"扫描已停止。");
}

void MainWindow::TogglePause() {
    if (!scanManager_ || !scanManager_->is_scanning()) return;

    if (scanManager_->is_paused()) {
        scanManager_->resume_from_pause();
        SetWindowTextW(hPauseBtn_, L"暂停");
        UpdateStatus(L"扫描中...");
        LOG_MSG(L"[MainWindow] Scan resumed from pause");
    } else {
        scanManager_->pause_scan();
        SetWindowTextW(hPauseBtn_, L"继续");
        UpdateStatus(L"扫描已暂停。");
        LOG_MSG(L"[MainWindow] Scan paused");
    }
}

void MainWindow::StartRecovery() {
    if (foundFiles_.empty()) {
        MessageBoxW(hwnd_, L"No files to recover.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get selected files from ListView
    // For simplicity, recover all files (could add multi-select support)
    std::vector<RecoverableFile> selectedFiles;

    int idx = -1;
    while ((idx = ListView_GetNextItem(hFileList_, idx, LVNI_SELECTED)) != -1) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = idx;
        ListView_GetItem(hFileList_, &item);

        // Defensive bounds check - lParam could be invalid if list is corrupted
        if (item.lParam < 0) continue;
        size_t fileIdx = static_cast<size_t>(item.lParam);
        if (fileIdx >= foundFiles_.size()) continue;

        selectedFiles.push_back(foundFiles_[fileIdx]);
    }

    if (selectedFiles.empty()) {
        // If nothing selected, recover all
        selectedFiles = foundFiles_;
    }

    // Browse for output folder
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd_;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select output folder for recovered files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return;  // User cancelled
    }

    wchar_t outputPath[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, outputPath);
    CoTaskMemFree(pidl);

    // Get the selected disk for SectorReader
    const DiskInfo* disk = GetSelectedDisk();
    if (!disk) {
        MessageBoxW(hwnd_, L"No disk selected.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Open the disk for reading
    DiskHandle diskHandle;
    if (!diskHandle.open(disk->device_path)) {
        MessageBoxW(hwnd_, L"Failed to open disk for reading.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Create SectorReader
    SectorReader sectorReader(diskHandle, disk->geometry.sector_size);

    // Create MultiTargetWriter with output path
    MultiTargetWriter writer;
    writer.add_target(outputPath);
    writer.set_auto_switch(true);

    // Set progress callback
    recoverManager_->set_progress_callback([this](uint32_t current, uint32_t total) {
        if (total > 0) {
            int percent = static_cast<int>((current * 100) / total);
            PostMessageW(hwnd_, WM_SCAN_PROGRESS, percent, 0);
        }
    });

    // Perform recovery
    UpdateStatus(L"Starting recovery...");
    EnableWindow(hRecoverBtn_, FALSE);
    EnableWindow(hScanBtn_, FALSE);

    bool success = recoverManager_->start_recovery(sectorReader, selectedFiles, writer);
    const auto& report = recoverManager_->report();

    // Show result
    wchar_t msg[512];
    if (success) {
        _snwprintf_s(msg, _TRUNCATE,
                     L"Recovery complete!\n\n"
                     L"Total files: %u\n"
                     L"Recovered: %u\n"
                     L"Failed: %u\n"
                     L"Bytes recovered: %s",
                     report.total_files, report.success_count, report.failed_count,
                     FormatSize(report.total_bytes_recovered).c_str());
        MessageBoxW(hwnd_, msg, L"Recovery Complete", MB_OK | MB_ICONINFORMATION);
        UpdateStatus(L"Recovery complete.");
    } else {
        _snwprintf_s(msg, _TRUNCATE,
                     L"Recovery partially completed.\n\n"
                     L"Total files: %u\n"
                     L"Recovered: %u\n"
                     L"Failed: %u",
                     report.total_files, report.success_count, report.failed_count);
        MessageBoxW(hwnd_, msg, L"Recovery", MB_OK | MB_ICONWARNING);
        UpdateStatus(L"Recovery completed with errors.");
    }

    EnableWindow(hRecoverBtn_, TRUE);
    EnableWindow(hScanBtn_, TRUE);
}

void MainWindow::UpdatePreview(int selectedIndex) {
    if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= foundFiles_.size()) {
        SetWindowTextW(hPreview_, L"No preview");
        return;
    }

    const RecoverableFile& file = foundFiles_[selectedIndex];

    // Check if file is previewable
    std::wstring ext;
    size_t dotPos = file.file_name.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        ext = file.file_name.substr(dotPos);
    }

    if (!business::PreviewManager::IsImageFile(ext) &&
        !business::PreviewManager::IsVideoFile(ext)) {
        SetWindowTextW(hPreview_, L"No preview available\nfor this file type");
        return;
    }

    // Need to read file data from disk
    const DiskInfo* disk = GetSelectedDisk();
    if (!disk) {
        SetWindowTextW(hPreview_, L"No disk selected");
        return;
    }

    // Check if file has fragments
    if (file.fragments.empty()) {
        SetWindowTextW(hPreview_, L"No data available");
        return;
    }

    // Open disk for reading
    DiskHandle diskHandle;
    if (!diskHandle.open(disk->device_path)) {
        SetWindowTextW(hPreview_, L"Cannot open disk");
        return;
    }

    SectorReader sectorReader(diskHandle, disk->geometry.sector_size);

    // Calculate total file size and allocate buffer
    uint64_t fileSize = file.file_size;
    if (fileSize > 10 * 1024 * 1024) {  // Limit preview to 10MB
        SetWindowTextW(hPreview_, L"File too large\nfor preview");
        return;
    }

    AlignedBuffer buffer(static_cast<size_t>(fileSize), disk->geometry.sector_size);
    if (buffer.empty()) {
        SetWindowTextW(hPreview_, L"Memory allocation failed");
        return;
    }

    // Read file data from fragments
    uint64_t bytesRead = 0;
    for (const auto& fragment : file.fragments) {
        uint64_t fragmentBytes = fragment.sector_count * disk->geometry.sector_size;
        uint64_t bytesToRead = std::min(fragmentBytes, fileSize - bytesRead);

        uint32_t sectorsToRead = static_cast<uint32_t>((bytesToRead + disk->geometry.sector_size - 1) /
                                                        disk->geometry.sector_size);

        AlignedBuffer frag_buf(sectorsToRead * disk->geometry.sector_size, disk->geometry.sector_size);
        if (!sectorReader.read_sectors(fragment.start_sector, sectorsToRead, frag_buf)) {
            SetWindowTextW(hPreview_, L"Read error");
            return;
        }

        // Copy fragment data into the main buffer at the correct offset
        memcpy(buffer.data() + bytesRead, frag_buf.data(), static_cast<size_t>(bytesToRead));

        bytesRead += bytesToRead;
        if (bytesRead >= fileSize) break;
    }

    // Generate thumbnail
    constexpr int THUMBNAIL_SIZE = 180;
    HBITMAP hBitmap = nullptr;

    if (business::PreviewManager::IsImageFile(ext)) {
        hBitmap = previewManager_->CreateThumbnailFromData(
            buffer.data(), static_cast<size_t>(fileSize),
            THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    } else if (business::PreviewManager::IsVideoFile(ext)) {
        hBitmap = previewManager_->CreateVideoThumbnailFromData(
            buffer.data(), static_cast<size_t>(fileSize),
            THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    }

    if (!hBitmap) {
        SetWindowTextW(hPreview_, L"Preview failed\n(corrupted data)");
        return;
    }

    // Display the bitmap in the static control
    // Need to change the static control style to SS_BITMAP
    LONG style = GetWindowLongW(hPreview_, GWL_STYLE);
    SetWindowLongW(hPreview_, GWL_STYLE, (style & ~SS_CENTER & ~SS_CENTERIMAGE) | SS_BITMAP);
    SendMessageW(hPreview_, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(hBitmap));

    // Note: HBITMAP ownership transfers to the static control
    // It will be deleted when the control is destroyed or a new image is set
}

void MainWindow::LoadDemoData() {
    // Clear existing data
    ClearFileList();
    cachedDisks_.clear();
    ComboBox_ResetContent(hDiskList_);
    ComboBox_ResetContent(hPartitionList_);

    // Add demo disk
    DiskInfo demoDisk;
    demoDisk.physical_drive_number = 0;
    demoDisk.device_path = L"\\\\.\\PhysicalDrive0";
    demoDisk.disk_size_bytes = 256ULL * 1024 * 1024 * 1024;  // 256 GB
    demoDisk.model_name = L"Demo SSD";
    demoDisk.geometry.total_sectors = demoDisk.disk_size_bytes / 512;
    demoDisk.geometry.sector_size = 512;

    // Add demo partitions
    PartitionInfo part1;
    part1.index = 0;
    part1.start_sector = 2048;
    part1.sector_count = 1000000000;
    part1.filesystem_type = L"NTFS";
    part1.volume_label = L"System";
    demoDisk.partitions.push_back(part1);

    PartitionInfo part2;
    part2.index = 1;
    part2.start_sector = 1000002048;
    part2.sector_count = 400000000;
    part2.filesystem_type = L"NTFS";
    part2.volume_label = L"Data";
    demoDisk.partitions.push_back(part2);

    cachedDisks_.push_back(demoDisk);

    // Populate disk ComboBox
    std::wstring diskText = L"PhysicalDrive0 - 256 GB (Demo SSD)";
    ComboBox_AddString(hDiskList_, diskText.c_str());
    ComboBox_SetCurSel(hDiskList_, 0);

    // Populate partition ComboBox
    ComboBox_AddString(hPartitionList_, L"Partition 0 - NTFS (System)");
    ComboBox_AddString(hPartitionList_, L"Partition 1 - NTFS (Data)");
    ComboBox_SetCurSel(hPartitionList_, 0);

    EnableWindow(hScanBtn_, TRUE);

    // Add demo files
    RecoverableFile file1;
    file1.file_name = L"photo_001.jpg";
    file1.file_size = 2500000;
    file1.file_type = FileType::Image;
    file1.is_corrupted = false;
    file1.fragments.push_back({1000, 5000});
    foundFiles_.push_back(file1);
    AddListViewItem(file1, foundFiles_.size() - 1);

    RecoverableFile file2;
    file2.file_name = L"video_001.mp4";
    file2.file_size = 150000000;
    file2.file_type = FileType::Video;
    file2.is_corrupted = false;
    file2.fragments.push_back({5000, 300000});
    foundFiles_.push_back(file2);
    AddListViewItem(file2, foundFiles_.size() - 1);

    RecoverableFile file3;
    file3.file_name = L"photo_002.png";
    file3.file_size = 1200000;
    file3.file_type = FileType::Image;
    file3.is_corrupted = true;
    file3.fragments.push_back({10000, 2400});
    foundFiles_.push_back(file3);
    AddListViewItem(file3, foundFiles_.size() - 1);

    RecoverableFile file4;
    file4.file_name = L"IMG_2023.cr2";
    file4.file_size = 25000000;
    file4.file_type = FileType::Image;
    file4.is_corrupted = false;
    file4.fragments.push_back({15000, 50000});
    foundFiles_.push_back(file4);
    AddListViewItem(file4, foundFiles_.size() - 1);

    RecoverableFile file5;
    file5.file_name = L"birthday.mov";
    file5.file_size = 80000000;
    file5.file_type = FileType::Video;
    file5.is_corrupted = false;
    file5.fragments.push_back({20000, 160000});
    foundFiles_.push_back(file5);
    AddListViewItem(file5, foundFiles_.size() - 1);

    EnableWindow(hRecoverBtn_, TRUE);
    UpdateStatus(L"Demo data loaded. Click Scan to simulate scanning, or select files and Recover.");
    UpdateProgress(0);

    MessageBoxW(hwnd_,
        L"Demo data has been loaded for testing purposes.\n\n"
        L"- Demo disk: PhysicalDrive0 (256 GB Demo SSD)\n"
        L"- 5 demo files added to the list\n\n"
        L"Note: This is simulated data for UI testing.\n"
        L"Actual disk scanning requires Administrator privileges.",
        L"Demo Data Loaded",
        MB_OK | MB_ICONINFORMATION);
}

} // namespace disk_recover::gui
