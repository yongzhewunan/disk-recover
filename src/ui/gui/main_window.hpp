#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>

#include "resource.h"
#include "disk-io/disk_info.hpp"
#include "business/scan_manager.hpp"
#include "business/recover_manager.hpp"
#include "business/preview_manager.hpp"
#include "common/types.hpp"

namespace disk_recover::gui {

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow();

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

    // Configuration controls
    HWND hScanModeCombo_ = nullptr;         // ComboBox for scan mode
    HWND hScanImagesCheck_ = nullptr;       // Checkbox for image filter
    HWND hScanVideosCheck_ = nullptr;       // Checkbox for video filter
    HWND hBadSectorCombo_ = nullptr;        // ComboBox for bad sector policy
    HWND hBadSectorPanel_ = nullptr;        // Panel for bad sector info
    HWND hBadSectorCount_ = nullptr;        // Static text for bad sector count

    // Business logic managers
    std::unique_ptr<ScanManager> scanManager_;
    std::unique_ptr<RecoverManager> recoverManager_;
    std::unique_ptr<business::PreviewManager> previewManager_;

    // Disk information cache
    std::vector<DiskInfo> cachedDisks_;
    std::vector<RecoverableFile> foundFiles_;  // Files found during scan
    uint32_t badSectorsCount_ = 0;  // Bad sectors detected during scan

    // Layout constants
    static constexpr int MARGIN = 8;
    static constexpr int CONTROL_HEIGHT = 24;
    static constexpr int BUTTON_WIDTH = 80;
    static constexpr int COMBO_WIDTH = 200;
    static constexpr int PREVIEW_WIDTH = 200;
    static constexpr int STATUSBAR_HEIGHT = 24;

    // Custom message for thread-safe UI updates
    static constexpr UINT WM_SCAN_PROGRESS = WM_USER + 1;
    static constexpr UINT WM_FILE_FOUND = WM_USER + 2;
    static constexpr UINT WM_SCAN_COMPLETE = WM_USER + 3;

    // Message handlers
    void OnCreate();
    void OnSize(int cx, int cy);
    void OnCommand(int id, int notifyCode, HWND hCtrl);
    void OnDestroy();
    void OnNotify(LPNMHDR nmhdr);
    void OnScanProgress(const ScanProgress& progress);
    void OnFileFound(const RecoverableFile& file);
    void OnScanComplete();

    // Control creation helpers
    HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h);
    HWND CreateComboBox(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h);
    HWND CreateListView(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateStatic(HWND parent, int id, int x, int y, int w, int h);
    HWND CreateStatusBar(HWND parent, int id);

    // ListView setup
    void SetupListViewColumns();
    void AddListViewItem(const RecoverableFile& file);
    void ClearFileList();

    // UI state helpers
    void EnableControls(bool scanning);
    void UpdateStatus(const wchar_t* text);
    void UpdateProgress(int percent);

    // Disk and partition management
    void RefreshDiskList();
    void RefreshPartitionList(const DiskInfo& disk);
    const DiskInfo* GetSelectedDisk() const;
    const PartitionInfo* GetSelectedPartition() const;

    // Operations
    void StartScan();
    void StopScan();
    void StartRecovery();
    void UpdatePreview(int selectedIndex);

    // Window class name
    static constexpr const wchar_t* CLASS_NAME = L"DiskRecoverMainWindow";
};

} // namespace disk_recover::gui
