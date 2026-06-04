#include "main_window.hpp"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    disk_recover::MainWindow win;
    if (!win.create(hInst)) {
        return 1;
    }
    win.show(nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
