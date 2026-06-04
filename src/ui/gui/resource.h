#pragma once

// Resource IDs for Win32 GUI

#define IDR_MAINFRAME           101

// Child control IDs (1000-1999)
#define IDC_CB_DRIVES           1001
#define IDC_BTN_START           1002   // Start/Pause/Continue toggle
#define IDC_BTN_STOP            1005
#define IDC_LV_FILES            1006
#define IDC_STATUS_BAR          1009
#define IDC_PB_PROGRESS         1010
#define IDC_CB_SCAN_MODE        1011   // Quick/Deep/Full dropdown
#define IDC_CHK_IMAGES          1012   // Image filter checkbox
#define IDC_CHK_VIDEOS          1013   // Video filter checkbox
#define IDC_ED_START_SECTOR     1014   // Start sector input
#define IDC_ED_END_SECTOR       1015   // End sector input
#define IDC_RB_SECTOR_ABS       1016   // Absolute sector radio
#define IDC_RB_SECTOR_PCT       1017   // Percentage radio
#define IDC_BTN_SAVE_DIRS       1018   // Save dirs dialog button
#define IDC_ST_SAVE_INFO        1019   // Save dirs summary label

// Menu command IDs (2000-2999)
#define IDM_SCAN                2001
#define IDM_RECOVER             2002
#define IDM_EXIT                2003
#define IDM_ABOUT               2004
#define IDM_STOP                2005
#define IDM_DEMO_DATA           2006

// Custom Windows messages for inter-thread communication
#define WM_FILE_RECOVERED       (WM_USER + 310)
#define WM_SCAN_RECOVER_PAUSED  (WM_USER + 311)
#define WM_SCAN_RECOVER_COMPLETE (WM_USER + 312)
#define WM_SETSTATUS            (WM_USER + 313)  // wParam = heap-allocated wchar_t* (receiver must delete)
