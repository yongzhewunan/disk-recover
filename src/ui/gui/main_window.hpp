#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

#include "resource.h"

namespace disk_recover::gui {

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow() = default;

    // Non-copyable, non-movable
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Register the window class (call once per process)
    /// @param hInst Application instance handle
    /// @return true on success
    bool RegisterClass(HINSTANCE hInst);

    /// Create and show the main window
    /// @param hInst Application instance handle
    /// @param cmdShow Initial show state (SW_SHOWDEFAULT, etc.)
    /// @return true on success
    bool Create(HINSTANCE hInst, int cmdShow);

    /// Run the message loop (blocks until window is closed)
    void RunMessageLoop();

    /// Get the window handle
    HWND GetHwnd() const noexcept { return hwnd_; }

    /// Window procedure (static callback)
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND hwnd_ = nullptr;           // Main window handle
    HINSTANCE hInst_ = nullptr;     // Instance handle

    // Child control handles
    HWND hDiskLabel_ = nullptr;         // "Disk:" label
    HWND hDiskList_ = nullptr;          // ComboBox for disk selection
    HWND hPartitionLabel_ = nullptr;    // "Partition:" label
    HWND hPartitionList_ = nullptr;     // ComboBox for partition selection
    HWND hScanBtn_ = nullptr;           // Scan button
    HWND hRecoverBtn_ = nullptr;        // Recover button
    HWND hStopBtn_ = nullptr;           // Stop button
    HWND hFileList_ = nullptr;          // ListView for recoverable files
    HWND hPreviewLabel_ = nullptr;      // "Preview:" label
    HWND hPreview_ = nullptr;           // Static control for preview image
    HWND hStatusBar_ = nullptr;         // Status bar
    HWND hProgressBar_ = nullptr;       // Progress bar (embedded in status bar)

    // Layout constants
    static constexpr int MARGIN = 8;
    static constexpr int CONTROL_HEIGHT = 24;
    static constexpr int BUTTON_WIDTH = 80;
    static constexpr int COMBO_WIDTH = 200;
    static constexpr int PREVIEW_WIDTH = 200;
    static constexpr int STATUSBAR_HEIGHT = 24;

    // Message handlers
    void OnCreate();
    void OnSize(int cx, int cy);
    void OnCommand(int id, int notifyCode, HWND hCtrl);
    void OnDestroy();
    void OnNotify(LPNMHDR nmhdr);

    // Control creation helpers
    HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h);
    HWND CreateComboBox(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h);
    HWND CreateListView(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateStatic(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateStatusBar(HWND parent, int id);

    // ListView setup
    void SetupListViewColumns();
    void AddListViewItem(const wchar_t* name, const wchar_t* size,
                         const wchar_t* type, const wchar_t* status);

    // UI state helpers
    void EnableControls(bool scanning);
    void UpdateStatus(const wchar_t* text);
    void UpdateProgress(int percent);

    // Window class name
    static constexpr const wchar_t* CLASS_NAME = L"DiskRecoverMainWindow";
};

} // namespace disk_recover::gui
