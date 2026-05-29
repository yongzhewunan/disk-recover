#pragma once

// Resource IDs for Win32 GUI
// disk-recover - Windows Disk Data Recovery Software

#define IDR_MAINFRAME       101

// Child control IDs (1000-1999)
#define IDC_DISK_LIST       1001    // ComboBox for disk selection
#define IDC_FILE_LIST       1002    // ListView for recoverable files
#define IDC_PREVIEW         1003    // Static control for preview image
#define IDC_STATUSBAR       1004    // Status bar
#define IDC_PROGRESSBAR     1005    // Progress bar
#define IDC_SCAN_BTN        1006    // Scan button
#define IDC_RECOVER_BTN     1007    // Recover button
#define IDC_PARTITION_LIST  1008    // ComboBox for partition selection
#define IDC_SCAN_MODE       1010    // ComboBox for scan mode selection
#define IDC_SCAN_IMAGES     1011    // Checkbox for image file filter
#define IDC_SCAN_VIDEOS     1012    // Checkbox for video file filter
#define IDC_BAD_SECTOR_POLICY 1013  // ComboBox for bad sector policy
#define IDC_BAD_SECTOR_PANEL  1014   // Static panel for bad sector info
#define IDC_BAD_SECTOR_COUNT  1015   // Static text for bad sector count

// Menu command IDs (2000-2999)
#define IDM_SCAN            2001
#define IDM_RECOVER         2002
#define IDM_EXIT            2003
#define IDM_ABOUT           2004
#define IDM_STOP            2005
#define IDM_DEMO_DATA       2006    // Load demo data for testing

// ListView column indices
#define COL_NAME            0
#define COL_SIZE            1
#define COL_TYPE            2
#define COL_STATUS          3
#define COL_PATH            4
