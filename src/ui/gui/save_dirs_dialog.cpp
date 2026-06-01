#include "save_dirs_dialog.hpp"
#include "disk-io/disk_info.hpp"
#include "common/logger.hpp"
#include <shlobj.h>
#include <commctrl.h>
#include <algorithm>

namespace disk_recover {

struct DialogData {
    std::vector<wchar_t> excluded_letters;
    std::vector<SaveDirEntry>* out_dirs;
    HWND hList;
    std::vector<SaveDirEntry> dirs;  // Working copy
};

static const wchar_t* SAVE_DIRS_CLASS = L"SaveDirsDialogClass";

static std::wstring FormatSize(uint64_t bytes) {
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.1f GB", bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.1f MB", bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        _snwprintf_s(buf, _TRUNCATE, L"%.1f KB", bytes / 1024.0);
    } else {
        _snwprintf_s(buf, _TRUNCATE, L"%llu B", bytes);
    }
    return buf;
}

static void RefreshListItems(HWND hList, std::vector<SaveDirEntry>& dirs) {
    ListBox_ResetContent(hList);
    for (auto& entry : dirs) {
        // Re-query free space
        ULARGE_INTEGER freeBytes;
        if (GetDiskFreeSpaceExW(entry.path.c_str(), &freeBytes, nullptr, nullptr)) {
            entry.free_bytes = freeBytes.QuadPart;
        }
        std::wstring display = entry.path + L"  (" + FormatSize(entry.free_bytes) + L" 可用)";
        ListBox_AddString(hList, display.c_str());
    }
}

static bool IsExcludedDrive(const std::wstring& path, const std::vector<wchar_t>& excluded) {
    if (path.size() < 2 || path[1] != L':') return false;
    wchar_t letter = towupper(path[0]);
    for (wchar_t e : excluded) {
        if (towupper(e) == letter) return true;
    }
    return false;
}

static LRESULT CALLBACK SaveDirsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<DialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        data = reinterpret_cast<DialogData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        data->hList = nullptr;

        // Create controls
        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        // ListBox
        HWND hList = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER | WS_VSCROLL,
            10, 10, 440, 250, hwnd, reinterpret_cast<HMENU>(1001), cs->hInstance, nullptr);
        SendMessageW(hList, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        data->hList = hList;

        // Buttons
        HWND hAdd = CreateWindowW(L"BUTTON", L"添加目录",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 270, 100, 30, hwnd, reinterpret_cast<HMENU>(IDOK + 1), cs->hInstance, nullptr);
        SendMessageW(hAdd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        HWND hRemove = CreateWindowW(L"BUTTON", L"删除",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 270, 80, 30, hwnd, reinterpret_cast<HMENU>(IDOK + 2), cs->hInstance, nullptr);
        SendMessageW(hRemove, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        HWND hOk = CreateWindowW(L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            270, 270, 80, 30, hwnd, reinterpret_cast<HMENU>(IDOK), cs->hInstance, nullptr);
        SendMessageW(hOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        HWND hCancel = CreateWindowW(L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 270, 80, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), cs->hInstance, nullptr);
        SendMessageW(hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK + 1: {
            // Add directory
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"选择保存目录";
            bi.ulFlags = BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH] = {};
                if (SHGetPathFromIDListW(pidl, path)) {
                    std::wstring selPath(path);
                    if (selPath.back() != L'\\') selPath += L'\\';

                    // Check excluded
                    if (IsExcludedDrive(selPath, data->excluded_letters)) {
                        MessageBoxW(hwnd, L"不能将文件保存到正在扫描的磁盘！", L"提示", MB_OK | MB_ICONWARNING);
                    } else {
                        // Check duplicate
                        bool dup = false;
                        for (auto& d : data->dirs) {
                            if (_wcsicmp(d.path.c_str(), selPath.c_str()) == 0) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup) {
                            SaveDirEntry entry;
                            entry.path = selPath;
                            ULARGE_INTEGER freeBytes;
                            if (GetDiskFreeSpaceExW(selPath.c_str(), &freeBytes, nullptr, nullptr)) {
                                entry.free_bytes = freeBytes.QuadPart;
                            }
                            data->dirs.push_back(entry);
                            RefreshListItems(data->hList, data->dirs);
                        }
                    }
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }

        case IDOK + 2: {
            // Remove selected
            int sel = ListBox_GetCurSel(data->hList);
            if (sel != LB_ERR && sel < static_cast<int>(data->dirs.size())) {
                data->dirs.erase(data->dirs.begin() + sel);
                RefreshListItems(data->hList, data->dirs);
            }
            return 0;
        }

        case IDOK: {
            // OK
            if (data->dirs.empty()) {
                MessageBoxW(hwnd, L"请至少选择一个保存目录", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            *data->out_dirs = data->dirs;
            DestroyWindow(hwnd);
            return 0;
        }

        case IDCANCEL:
            data->out_dirs->clear();
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        data->out_dirs->clear();
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SaveDirsDialog::Show(HWND parent_hwnd,
                           const std::vector<wchar_t>& excluded_letters,
                           std::vector<SaveDirEntry>& out_dirs) {
    // Register window class
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SaveDirsWndProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent_hwnd, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = SAVE_DIRS_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    DialogData dialogData;
    dialogData.excluded_letters = excluded_letters;
    dialogData.out_dirs = &out_dirs;

    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent_hwnd, GWLP_HINSTANCE));

    // Disable parent
    EnableWindow(parent_hwnd, FALSE);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        SAVE_DIRS_CLASS,
        L"选择保存目录",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 350,
        parent_hwnd, nullptr, hInst, &dialogData);

    if (!hDlg) {
        EnableWindow(parent_hwnd, TRUE);
        return false;
    }

    // Center on parent
    RECT rcParent, rcDlg;
    GetWindowRect(parent_hwnd, &rcParent);
    GetWindowRect(hDlg, &rcDlg);
    int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // Modal message loop
    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent_hwnd, TRUE);
    SetFocus(parent_hwnd);

    return !out_dirs.empty();
}

} // namespace disk_recover
