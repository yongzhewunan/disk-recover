#include "main_window.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <string>

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

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::OnCreate() {
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

    // Add some demo data to disk list
    ComboBox_AddString(hDiskList_, L"\\\\.\\PhysicalDrive0 - 256 GB SSD");
    ComboBox_AddString(hDiskList_, L"\\\\.\\PhysicalDrive1 - 1 TB HDD");
    ComboBox_SetCurSel(hDiskList_, 0);

    // Add demo data to partition list
    ComboBox_AddString(hPartitionList_, L"Partition 0 - NTFS (System)");
    ComboBox_AddString(hPartitionList_, L"Partition 1 - NTFS (Data)");
    ComboBox_SetCurSel(hPartitionList_, 0);

    // Add demo items to file list
    AddListViewItem(L"IMG_001.jpg", L"2.5 MB", L"Image", L"Good");
    AddListViewItem(L"VID_001.mp4", L"150 MB", L"Video", L"Good");
    AddListViewItem(L"photo.png", L"1.2 MB", L"Image", L"Corrupted");

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
            UpdateStatus(L"Scanning disk...");
            EnableControls(true);
            // TODO: Start scan operation
            break;

        case IDC_RECOVER_BTN:
            UpdateStatus(L"Recovering files...");
            // TODO: Start recover operation
            break;

        case IDM_STOP:
            UpdateStatus(L"Scan stopped.");
            EnableControls(false);
            // TODO: Stop current operation
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd_);
            break;

        case IDC_DISK_LIST:
            if (notifyCode == CBN_SELCHANGE) {
                // Disk selection changed - update partition list
                // TODO: Query partitions for selected disk
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
    // No cleanup needed - Windows destroys child controls automatically
    hwnd_ = nullptr;
}

void MainWindow::OnNotify(LPNMHDR nmhdr) {
    if (nmhdr->idFrom == IDC_FILE_LIST && nmhdr->code == LVN_ITEMCHANGED) {
        // Selection changed in file list
        auto* pnmlv = reinterpret_cast<LPNMLISTVIEW>(nmhdr);
        if ((pnmlv->uNewState & LVIS_SELECTED) && !(pnmlv->uOldState & LVIS_SELECTED)) {
            // New item selected - update preview
            // TODO: Load preview for selected file
            SetWindowTextW(hPreview_, L"Loading preview...");
        }
    }
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

void MainWindow::AddListViewItem(const wchar_t* name, const wchar_t* size,
                                  const wchar_t* type, const wchar_t* status) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(hFileList_);
    item.pszText = const_cast<wchar_t*>(name);
    int idx = ListView_InsertItem(hFileList_, &item);

    ListView_SetItemText(hFileList_, idx, COL_SIZE, const_cast<wchar_t*>(size));
    ListView_SetItemText(hFileList_, idx, COL_TYPE, const_cast<wchar_t*>(type));
    ListView_SetItemText(hFileList_, idx, COL_STATUS, const_cast<wchar_t*>(status));
}

void MainWindow::EnableControls(bool scanning) {
    EnableWindow(hDiskList_, !scanning);
    EnableWindow(hPartitionList_, !scanning);
    EnableWindow(hScanBtn_, !scanning);
    EnableWindow(hRecoverBtn_, !scanning && ListView_GetSelectedCount(hFileList_) > 0);
    EnableWindow(hStopBtn_, scanning);
}

void MainWindow::UpdateStatus(const wchar_t* text) {
    SetWindowTextW(hStatusBar_, text);
}

void MainWindow::UpdateProgress(int percent) {
    SendMessageW(hProgressBar_, PBM_SETPOS, percent, 0);
}

} // namespace disk_recover::gui
