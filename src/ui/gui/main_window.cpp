#include "main_window.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <shlobj.h>  // For folder browse dialog

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
        case FileType::Image: return L"Image";
        case FileType::Video: return L"Video";
        default: return L"Unknown";
    }
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
        L"Disk Recover - Data Recovery Tool",
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
                self->OnScanProgress(*reinterpret_cast<const ScanProgress*>(lParam));
                delete reinterpret_cast<const ScanProgress*>(lParam);
            }
            return 0;

        case WM_FILE_FOUND:
            if (lParam) {
                self->OnFileFound(*reinterpret_cast<const RecoverableFile*>(lParam));
                delete reinterpret_cast<const RecoverableFile*>(lParam);
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
    // Initialize business logic managers
    scanManager_ = std::make_unique<ScanManager>();
    recoverManager_ = std::make_unique<RecoverManager>();
    previewManager_ = std::make_unique<business::PreviewManager>();

    // Set up scan callbacks with thread-safe UI updates via PostMessage
    scanManager_->set_progress_callback([this](const ScanProgress& progress) {
        // Post message to UI thread (allocate copy on heap)
        ScanProgress* p = new ScanProgress(progress);
        PostMessageW(hwnd_, WM_SCAN_PROGRESS, 0, reinterpret_cast<LPARAM>(p));
    });

    scanManager_->set_file_found_callback([this](const RecoverableFile& file) {
        // Post message to UI thread (allocate copy on heap)
        RecoverableFile* f = new RecoverableFile(file);
        PostMessageW(hwnd_, WM_FILE_FOUND, 0, reinterpret_cast<LPARAM>(f));
    });

    // Create disk selection label
    hDiskLabel_ = CreateLabel(hwnd_, L"Physical Disk:", MARGIN, MARGIN, 80, CONTROL_HEIGHT);

    // Create disk selection ComboBox
    hDiskList_ = CreateComboBox(hwnd_, IDC_DISK_LIST, MARGIN + 85, MARGIN, COMBO_WIDTH, CONTROL_HEIGHT + 200);

    // Create partition label
    hPartitionLabel_ = CreateLabel(hwnd_, L"Partition:", MARGIN + 85 + COMBO_WIDTH + MARGIN, MARGIN, 60, CONTROL_HEIGHT);

    // Create partition ComboBox
    hPartitionList_ = CreateComboBox(hwnd_, IDC_PARTITION_LIST,
                                     MARGIN + 85 + COMBO_WIDTH + MARGIN + 65, MARGIN,
                                     COMBO_WIDTH, CONTROL_HEIGHT + 200);

    // Create Scan button
    hScanBtn_ = CreateButton(hwnd_, L"Scan", IDC_SCAN_BTN,
                             MARGIN + 85 + COMBO_WIDTH + MARGIN + 65 + COMBO_WIDTH + MARGIN, MARGIN,
                             BUTTON_WIDTH, CONTROL_HEIGHT);

    // Create Recover button (initially disabled)
    hRecoverBtn_ = CreateButton(hwnd_, L"Recover", IDC_RECOVER_BTN,
                                MARGIN + 85 + COMBO_WIDTH + MARGIN + 65 + COMBO_WIDTH + MARGIN + BUTTON_WIDTH + MARGIN, MARGIN,
                                BUTTON_WIDTH, CONTROL_HEIGHT);
    EnableWindow(hRecoverBtn_, FALSE);

    // Create Stop button (initially disabled)
    hStopBtn_ = CreateButton(hwnd_, L"Stop", IDM_STOP,
                             MARGIN + 85 + COMBO_WIDTH + MARGIN + 65 + COMBO_WIDTH + MARGIN + 2 * (BUTTON_WIDTH + MARGIN), MARGIN,
                             BUTTON_WIDTH, CONTROL_HEIGHT);
    EnableWindow(hStopBtn_, FALSE);

    // Create file list ListView
    int fileListY = MARGIN + CONTROL_HEIGHT + MARGIN;
    hFileList_ = CreateListView(hwnd_, IDC_FILE_LIST, MARGIN, fileListY, 600, 400);
    SetupListViewColumns();

    // Create preview label
    hPreviewLabel_ = CreateLabel(hwnd_, L"Preview:", MARGIN + 600 + MARGIN, fileListY, PREVIEW_WIDTH, CONTROL_HEIGHT);

    // Create preview static control
    hPreview_ = CreateStatic(hwnd_, IDC_PREVIEW,
                             MARGIN + 600 + MARGIN, fileListY + CONTROL_HEIGHT + MARGIN,
                             PREVIEW_WIDTH, PREVIEW_WIDTH);
    SetWindowTextW(hPreview_, L"No preview");

    // Create status bar
    hStatusBar_ = CreateStatusBar(hwnd_, IDC_STATUSBAR);

    // Populate disk list with real disk information
    RefreshDiskList();

    UpdateStatus(L"Ready. Select a disk and partition, then click Scan.");
}

void MainWindow::OnSize(int cx, int cy) {
    if (!hFileList_) return;  // Controls not created yet

    int topRowHeight = MARGIN + CONTROL_HEIGHT + MARGIN;
    int bottomRowY = cy - STATUSBAR_HEIGHT - MARGIN;

    // Resize file list to fill available space
    int fileListWidth = cx - MARGIN - PREVIEW_WIDTH - 3 * MARGIN;
    int fileListHeight = bottomRowY - topRowHeight - MARGIN;
    SetWindowPos(hFileList_, nullptr, MARGIN, topRowHeight, fileListWidth, fileListHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Move preview controls
    int previewX = MARGIN + fileListWidth + 2 * MARGIN;
    SetWindowPos(hPreviewLabel_, nullptr, previewX, topRowHeight, PREVIEW_WIDTH, CONTROL_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hPreview_, nullptr, previewX, topRowHeight + CONTROL_HEIGHT + MARGIN,
                 PREVIEW_WIDTH, fileListHeight - CONTROL_HEIGHT - MARGIN,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Resize status bar
    SetWindowPos(hStatusBar_, nullptr, 0, cy - STATUSBAR_HEIGHT, cx, STATUSBAR_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Move progress bar within status bar (if exists)
    if (hProgressBar_) {
        RECT rcStatus;
        GetClientRect(hStatusBar_, &rcStatus);
        SetWindowPos(hProgressBar_, nullptr, rcStatus.right - 200, 2, 180, 18,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::OnCommand(int id, int notifyCode, HWND hCtrl) {
    switch (id) {
        case IDC_SCAN_BTN:
            StartScan();
            break;

        case IDC_RECOVER_BTN:
            StartRecovery();
            break;

        case IDM_STOP:
            StopScan();
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd_);
            break;

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
    // Update progress bar
    if (progress.total_sectors > 0) {
        int percent = static_cast<int>((progress.sectors_scanned * 100) / progress.total_sectors);
        UpdateProgress(percent);
    }

    // Update status text
    wchar_t status[256];
    _snwprintf_s(status, _TRUNCATE, L"Scanning... %llu/%llu sectors, %u files found",
                 progress.sectors_scanned, progress.total_sectors, progress.files_found);
    UpdateStatus(status);
}

void MainWindow::OnFileFound(const RecoverableFile& file) {
    // Add file to list
    foundFiles_.push_back(file);
    AddListViewItem(file);

    // Enable recover button if we have files
    if (foundFiles_.size() == 1) {
        EnableWindow(hRecoverBtn_, TRUE);
    }
}

void MainWindow::OnScanComplete() {
    EnableControls(false);
    UpdateProgress(100);

    wchar_t status[256];
    _snwprintf_s(status, _TRUNCATE, L"Scan complete. %zu files found.", foundFiles_.size());
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
    HWND hwnd = CreateWindowExW(
        0, WC_COMBOBOX, L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h,
        parent, reinterpret_cast<HMENU>(id), hInst_, nullptr
    );
    if (!hwnd) {
        OutputDebugStringW(L"[DiskRecover] Failed to create combo box control\n");
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
    col.cx = 80;
    col.iSubItem = COL_STATUS;
    ListView_InsertColumn(hFileList_, COL_STATUS, &col);

    col.pszText = const_cast<wchar_t*>(L"Path");
    col.cx = 150;
    col.iSubItem = COL_PATH;
    ListView_InsertColumn(hFileList_, COL_PATH, &col);
}

void MainWindow::AddListViewItem(const RecoverableFile& file) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(hFileList_);
    item.pszText = const_cast<wchar_t*>(file.file_name.c_str());
    item.lParam = static_cast<LPARAM>(foundFiles_.size() - 1);  // Index in foundFiles_
    int idx = ListView_InsertItem(hFileList_, &item);

    // Set size
    std::wstring sizeStr = FormatSize(file.file_size);
    ListView_SetItemText(hFileList_, idx, COL_SIZE, const_cast<wchar_t*>(sizeStr.c_str()));

    // Set type
    ListView_SetItemText(hFileList_, idx, COL_TYPE, const_cast<wchar_t*>(FileTypeToString(file.file_type)));

    // Set status
    ListView_SetItemText(hFileList_, idx, COL_STATUS,
                         const_cast<wchar_t*>(file.is_corrupted ? L"Corrupted" : L"Good"));

    // Set path (empty for now, could be derived from fragments)
    ListView_SetItemText(hFileList_, idx, COL_PATH, const_cast<wchar_t*>(L""));
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
}

void MainWindow::UpdateStatus(const wchar_t* text) {
    SetWindowTextW(hStatusBar_, text);
}

void MainWindow::UpdateProgress(int percent) {
    SendMessageW(hProgressBar_, PBM_SETPOS, percent, 0);
}

void MainWindow::RefreshDiskList() {
    // Enumerate physical disks
    cachedDisks_ = DiskInfoQuery::EnumeratePhysicalDisks();

    // Clear and populate disk ComboBox
    ComboBox_ResetContent(hDiskList_);

    if (cachedDisks_.empty()) {
        ComboBox_AddString(hDiskList_, L"No disks found");
        ComboBox_SetCurSel(hDiskList_, 0);
        EnableWindow(hScanBtn_, FALSE);
        UpdateStatus(L"No physical disks found. Run as Administrator.");
        return;
    }

    for (const auto& disk : cachedDisks_) {
        // Format: "PhysicalDrive0 - 256 GB (Samsung SSD 860)"
        std::wstring text = disk.device_path + L" - " + FormatSize(disk.disk_size_bytes);
        if (!disk.model_name.empty()) {
            text += L" (" + disk.model_name + L")";
        }
        int idx = ComboBox_AddString(hDiskList_, text.c_str());
        ComboBox_SetItemData(hDiskList_, idx, disk.physical_drive_number);
    }

    // Select first disk and update partition list
    ComboBox_SetCurSel(hDiskList_, 0);
    RefreshPartitionList(cachedDisks_[0]);
    EnableWindow(hScanBtn_, TRUE);
}

void MainWindow::RefreshPartitionList(const DiskInfo& disk) {
    ComboBox_ResetContent(hPartitionList_);

    if (disk.partitions.empty()) {
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
        ComboBox_AddString(hPartitionList_, text.c_str());
    }

    ComboBox_SetCurSel(hPartitionList_, 0);
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
        MessageBoxW(hwnd_, L"Please select a disk first.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Clear previous results
    ClearFileList();
    UpdateProgress(0);

    // Configure scan
    ScanManager::Config config;
    config.device_path = disk->device_path;
    config.db_path = L"scan_cache.db";  // TODO: Use proper temp path
    config.session_id = "session_1";  // TODO: Generate unique session ID
    config.mode = ScanMode::Deep;
    config.bad_sector_policy = BadSectorPolicy::Skip;
    config.scan_images = true;
    config.scan_videos = true;

    // If a partition is selected, limit scan to that partition
    const PartitionInfo* part = GetSelectedPartition();
    if (part) {
        config.start_sector = part->start_sector;
        config.end_sector = part->start_sector + part->sector_count;
    } else {
        // Scan entire disk
        config.start_sector = 0;
        config.end_sector = disk->geometry.total_sectors;
    }

    // Start scan
    if (!scanManager_->start_scan(config)) {
        MessageBoxW(hwnd_, L"Failed to start scan.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    EnableControls(true);
    UpdateStatus(L"Starting scan...");
}

void MainWindow::StopScan() {
    if (scanManager_ && scanManager_->is_scanning()) {
        scanManager_->stop_scan();
        UpdateStatus(L"Scan stopped.");
        EnableControls(false);
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

        size_t fileIdx = static_cast<size_t>(item.lParam);
        if (fileIdx < foundFiles_.size()) {
            selectedFiles.push_back(foundFiles_[fileIdx]);
        }
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

    // TODO: Implement actual recovery with RecoverManager
    // This requires a SectorReader and MultiTargetWriter
    // For now, show a placeholder message
    wchar_t msg[512];
    _snwprintf_s(msg, _TRUNCATE,
                 L"Recovery to: %s\n\n%zu files selected.\n\n"
                 L"Note: Full recovery implementation requires SectorReader integration.",
                 outputPath, selectedFiles.size());
    MessageBoxW(hwnd_, msg, L"Recovery", MB_OK | MB_ICONINFORMATION);

    UpdateStatus(L"Recovery not yet fully implemented.");
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

    if (business::PreviewManager::IsImageFile(ext)) {
        SetWindowTextW(hPreview_, L"Image preview\n(requires file data)");
        // TODO: To show actual preview, need to:
        // 1. Read file data from disk using SectorReader
        // 2. Call previewManager_->CreateThumbnailFromData()
        // 3. Display HBITMAP in static control (needs SS_BITMAP style)
    } else if (business::PreviewManager::IsVideoFile(ext)) {
        SetWindowTextW(hPreview_, L"Video preview\n(requires file data)");
        // TODO: Same as above, but use CreateVideoThumbnailFromData()
    } else {
        SetWindowTextW(hPreview_, L"No preview available\nfor this file type");
    }
}

} // namespace disk_recover::gui
