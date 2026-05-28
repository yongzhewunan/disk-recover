// Main entry point for disk-recover GUI application
// disk-recover - Windows Disk Data Recovery Software

#include "main_window.hpp"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Create and run main window
    disk_recover::gui::MainWindow mainWindow;

    if (!mainWindow.RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!mainWindow.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to create main window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    mainWindow.RunMessageLoop();
    return 0;
}
