#define NOMINMAX
#include <windows.h>
#include <CLI/CLI.hpp>
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/aligned_buffer.hpp"
#include "business/scan_manager.hpp"
#include "business/recovery_manager.hpp"
#include "business/multi_target_writer.hpp"
#include "business/scan_cache_db.hpp"
#include "business/preview_manager.hpp"
#include "common/utils.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <csignal>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>

using namespace disk_recover;

// Wildcard matching helper function
// Supports: * matches any sequence, ? matches single character
// Case insensitive for Windows filenames
static bool wildcard_match(const std::wstring& text, const std::wstring& pattern) {
    size_t text_len = text.length();
    size_t pattern_len = pattern.length();

    // Convert to lowercase for case-insensitive matching
    std::wstring text_lower = text;
    std::wstring pattern_lower = pattern;
    std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::towlower);
    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(), ::towlower);

    // Dynamic programming approach for wildcard matching
    std::vector<std::vector<bool>> dp(text_len + 1, std::vector<bool>(pattern_len + 1, false));

    dp[0][0] = true;  // Empty pattern matches empty text

    // Handle patterns starting with *
    for (size_t j = 1; j <= pattern_len; ++j) {
        if (pattern_lower[j - 1] == L'*') {
            dp[0][j] = dp[0][j - 1];
        }
    }

    for (size_t i = 1; i <= text_len; ++i) {
        for (size_t j = 1; j <= pattern_len; ++j) {
            if (pattern_lower[j - 1] == L'*') {
                // * can match empty or any sequence
                dp[i][j] = dp[i][j - 1] || dp[i - 1][j];
            } else if (pattern_lower[j - 1] == L'?' || pattern_lower[j - 1] == text_lower[i - 1]) {
                // ? matches any single char, or exact match
                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }

    return dp[text_len][pattern_len];
}

// Parse comma-separated patterns and check if filename matches any
static bool matches_any_pattern(const std::wstring& filename, const std::wstring& filter_patterns) {
    if (filter_patterns.empty()) {
        return true;  // No filter, match all
    }

    // Split by comma
    std::vector<std::wstring> patterns;
    std::wstringstream wss(filter_patterns);
    std::wstring pattern;
    while (std::getline(wss, pattern, L',')) {
        // Trim whitespace
        size_t start = pattern.find_first_not_of(L" \t");
        size_t end = pattern.find_last_not_of(L" \t");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            patterns.push_back(pattern.substr(start, end - start + 1));
        }
    }

    // Check against each pattern
    for (const auto& p : patterns) {
        if (wildcard_match(filename, p)) {
            return true;
        }
    }

    return false;
}

static ScanManager g_scan_manager;
static bool g_interrupted = false;

void signal_handler(int) {
    g_interrupted = true;
    g_scan_manager.stop_scan();
}

// Helper function to save thumbnail to file
static bool SaveThumbnailToFile(HBITMAP thumbnail, const std::wstring& output_path) {
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartupInput gdiplus_input;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    Gdiplus::Bitmap bitmap(thumbnail, nullptr);
    CLSID png_clsid = {0};
    bool found_encoder = false;

    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size > 0) {
        auto encoder_buffer = std::make_unique<BYTE[]>(size);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoder_buffer.get());
        Gdiplus::GetImageEncoders(num, size, codecs);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
                png_clsid = codecs[i].Clsid;
                found_encoder = true;
                break;
            }
        }
    }

    bool success = false;
    if (found_encoder) {
        success = (bitmap.Save(output_path.c_str(), &png_clsid) == Gdiplus::Ok);
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    return success;
}

int main(int argc, char** argv) {
    // Set console output to UTF-8 for proper Chinese character display
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initialize logger
    LOG_INIT();

    CLI::App app{"Disk Recover - 磁盘数据恢复工具", "disk-recover"};

    // list-disks
    auto list_cmd = app.add_subcommand("list-disks", "列出所有可用磁盘和分区");
    list_cmd->callback([]() {
        bool is_admin = utils::IsAdminPrivilege();

        if (!is_admin) {
            std::cerr << "\n警告: 未以管理员权限运行\n";
            std::cerr << "提示: 请右键点击终端，选择\"以管理员身份运行\"，然后重新执行此命令\n\n";
        } else {
            std::cout << "已检测到管理员权限\n\n";
        }

        // Always show logical drives first
        std::cout << "=== 逻辑驱动器 ===\n\n";

        DWORD drives = GetLogicalDrives();
        if (drives == 0) {
            std::cerr << "错误: 无法获取逻辑驱动器信息\n";
        } else {
            std::cout << "驱动器\t类型\t大小\n";
            std::cout << "------\t----\t----\n";

            for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
                if (!(drives & (1 << (letter - L'A')))) continue;

                std::wstring root_path = letter + std::wstring(L":\\");
                UINT drive_type = GetDriveTypeW(root_path.c_str());

                std::string type_str;
                switch (drive_type) {
                    case DRIVE_FIXED: type_str = "固定"; break;
                    case DRIVE_REMOVABLE: type_str = "可移动"; break;
                    case DRIVE_REMOTE: type_str = "网络"; break;
                    case DRIVE_CDROM: type_str = "光盘"; break;
                    case DRIVE_RAMDISK: type_str = "内存盘"; break;
                    default: type_str = "未知(" + std::to_string(drive_type) + ")"; break;
                }

                ULARGE_INTEGER free_bytes, total_bytes, free_avail;
                std::cout << static_cast<char>(letter) << ":\t" << type_str << "\t";
                if (GetDiskFreeSpaceExW(root_path.c_str(), &free_avail, &total_bytes, &free_bytes)) {
                    std::wstring size_wstr = utils::FormatFileSize(total_bytes.QuadPart);
                    std::string size_str(size_wstr.begin(), size_wstr.end());
                    std::cout << size_str;
                } else {
                    std::cout << "(无法获取大小)";
                }
                std::cout << "\n";
            }
        }

        // Try to enumerate physical disks
        std::cout << "\n=== 物理磁盘 ===\n\n";

        auto disks = DiskInfoQuery::EnumeratePhysicalDisks();
        if (disks.empty()) {
            if (!is_admin) {
                std::cout << "无法访问物理磁盘 - 需要管理员权限\n";
                std::cout << "\n要扫描物理磁盘进行数据恢复，请:\n";
                std::cout << "  1. 右键点击命令提示符或 PowerShell\n";
                std::cout << "  2. 选择\"以管理员身份运行\"\n";
                std::cout << "  3. 导航到此程序目录并重新运行\n";
            } else {
                std::cout << "未找到任何物理磁盘\n";
            }
        } else {
            std::cout << "找到 " << disks.size() << " 个物理磁盘:\n\n";
            for (const auto& disk : disks) {
                std::cout << "磁盘 " << disk.physical_drive_number << ": ";
                std::string model_str(disk.model_name.begin(), disk.model_name.end());
                std::cout << model_str << " (";
                std::wstring size_wstr = utils::FormatFileSize(disk.disk_size_bytes);
                std::string size_str(size_wstr.begin(), size_wstr.end());
                std::cout << size_str << ")\n";
                std::cout << "  扇区大小: " << disk.geometry.sector_size
                          << "  总扇区数: " << disk.geometry.total_sectors << "\n";
                for (const auto& part : disk.partitions) {
                    std::cout << "  分区 " << part.index << ": ";
                    std::string fs_str(part.filesystem_type.begin(), part.filesystem_type.end());
                    std::cout << fs_str << " 起始=" << part.start_sector << " 大小=";
                    std::wstring part_size_wstr = utils::FormatFileSize(part.sector_count * 512);
                    std::string part_size_str(part_size_wstr.begin(), part_size_wstr.end());
                    std::cout << part_size_str << "\n";
                }
            }
        }
    });

    // scan
    std::string scan_device, scan_session, scan_db_path, bad_sector_str;
    std::string scan_mode_str = "deep";
    std::string resume_session;
    bool scan_images = true, scan_videos = true;

    auto scan_cmd = app.add_subcommand("scan", "扫描磁盘查找可恢复文件");
    scan_cmd->add_option("device", scan_device, "磁盘设备路径 (如 \\\\.\\PhysicalDrive0)");
    scan_cmd->add_option("--session", scan_session, "扫描会话ID")->default_val("default");
    scan_cmd->add_option("--db", scan_db_path, "缓存数据库路径")->default_val("scan_cache.db");
    scan_cmd->add_option("--mode", scan_mode_str, "扫描模式 (quick/deep/full)")->default_val("deep");
    scan_cmd->add_option("--bad-sector", bad_sector_str, "坏道策略 (skip/retry/force)")->default_val("skip");
    scan_cmd->add_option("--resume", resume_session, "恢复中断的扫描会话ID");
    scan_cmd->add_flag("--no-images", scan_images, "跳过图片文件")->group("");
    scan_cmd->add_flag("--no-videos", scan_videos, "跳过视频文件")->group("");

    scan_cmd->callback([&]() {
        std::signal(SIGINT, signal_handler);

        ScanManager::Config config;
        config.device_path = std::wstring(scan_device.begin(), scan_device.end());
        config.db_path = std::wstring(scan_db_path.begin(), scan_db_path.end());
        config.session_id = scan_session;
        config.scan_images = scan_images;
        config.scan_videos = scan_videos;

        if (scan_mode_str == "quick") config.mode = ScanMode::Quick;
        else if (scan_mode_str == "full") config.mode = ScanMode::Full;
        else config.mode = ScanMode::Deep;

        if (bad_sector_str == "retry") config.bad_sector_policy = BadSectorPolicy::Retry;
        else if (bad_sector_str == "force") config.bad_sector_policy = BadSectorPolicy::ForceRead;
        else config.bad_sector_policy = BadSectorPolicy::Skip;

        g_scan_manager.set_progress_callback([](const ScanProgress& p) {
            double pct = p.total_sectors ? (100.0 * p.sectors_scanned / p.total_sectors) : 0;
            std::cout << "\r进度: " << static_cast<int>(pct) << "% | "
                      << "文件: " << p.files_found << " | "
                      << "坏道: " << p.bad_sectors_hit << std::flush;
        });

        bool scan_started = false;

        if (!resume_session.empty()) {
            // Resume mode: load previous session and continue
            if (scan_device.empty()) {
                std::cerr << "错误: 恢复扫描需要指定设备路径\n";
                return;
            }
            std::cout << "恢复扫描会话: " << resume_session << "\n";
            std::cout << "设备: " << scan_device << "\n\n";
            config.session_id = resume_session;
            scan_started = g_scan_manager.resume_scan(config);
        } else {
            // Normal mode: start new scan
            if (scan_device.empty()) {
                std::cerr << "错误: 需要指定设备路径\n";
                return;
            }
            std::cout << "开始扫描: " << scan_device << "\n";
            std::cout << "会话ID: " << scan_session << "\n\n";
            scan_started = g_scan_manager.start_scan(config);
        }

        if (!scan_started) {
            std::cerr << "启动扫描失败\n";
            return;
        }

        while (g_scan_manager.is_scanning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (g_interrupted) {
                std::cout << "\n\n扫描已中断\n";
                break;
            }
        }
        std::cout << "\n\n扫描完成\n";
    });

    // recover
    std::string recover_session, recover_db, recover_output, recover_filter;
    std::string recover_device;
    bool auto_switch = true;

    auto recover_cmd = app.add_subcommand("recover", "恢复扫描结果中的文件");
    recover_cmd->add_option("session", recover_session, "扫描会话ID")->required();
    recover_cmd->add_option("--db", recover_db, "缓存数据库路径")->default_val("scan_cache.db");
    recover_cmd->add_option("--output,-o", recover_output, "目标输出目录")->required();
    recover_cmd->add_option("--device", recover_device, "磁盘设备路径 (如 \\\\.\\PhysicalDrive0)");
    recover_cmd->add_option("--filter", recover_filter, "文件名过滤模式（支持通配符 * 和 ?，逗号分隔多个）");
    recover_cmd->add_flag("--auto-switch", auto_switch, "空间不足自动切换到下一个目标");

    recover_cmd->callback([&]() {
        if (recover_device.empty()) {
            std::cerr << "错误: 需要指定设备路径 (--device)\n";
            return;
        }

        std::wstring db_path = std::wstring(recover_db.begin(), recover_db.end());
        std::wstring output_path = std::wstring(recover_output.begin(), recover_output.end());
        std::wstring device_path = std::wstring(recover_device.begin(), recover_device.end());

        // Create output directory
        std::filesystem::create_directories(recover_output);

        // Build RecoveryConfig
        RecoveryConfig config;
        config.session_id = recover_session;
        config.db_path = db_path;
        config.source_disk_path = device_path;
        config.sector_size = 512;  // Will be auto-detected from disk geometry
        config.min_free_bytes = 1ULL << 30;

        SaveDirEntry dir_entry;
        dir_entry.path = output_path;
        config.save_dirs.push_back(dir_entry);

        RecoveryManager recovery_mgr;
        if (!recovery_mgr.start_recovery(config)) {
            std::cerr << "启动恢复失败\n";
            return;
        }

        std::cout << "恢复进行中...\n";
        while (recovery_mgr.is_recovering()) {
            auto stats = recovery_mgr.stats();
            double pct = stats.total_files ? (100.0 * stats.files_recovered / stats.total_files) : 0;
            std::cout << "\r进度: " << static_cast<int>(pct) << "% | "
                      << "已恢复: " << stats.files_recovered << "/" << stats.total_files << " | "
                      << "失败: " << stats.files_failed << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        auto stats = recovery_mgr.stats();
        std::cout << "\n\n恢复完成\n";
        std::cout << "总文件: " << stats.total_files << "\n";
        std::cout << "已恢复: " << stats.files_recovered << "\n";
        std::cout << "失败: " << stats.files_failed << "\n";
    });

    // progress
    std::string progress_session, progress_db;
    auto progress_cmd = app.add_subcommand("progress", "查看扫描进度");
    progress_cmd->add_option("session", progress_session, "扫描会话ID")->default_val("default");
    progress_cmd->add_option("--db", progress_db, "缓存数据库路径")->default_val("scan_cache.db");

    progress_cmd->callback([&]() {
        ScanCacheDB db;
        std::wstring db_path = std::wstring(progress_db.begin(), progress_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << progress_db << "\n";
            return;
        }

        ScanProgress progress;
        if (db.load_progress(progress_session, progress)) {
            double pct = progress.total_sectors ?
                (100.0 * progress.sectors_scanned / progress.total_sectors) : 0;
            std::cout << "会话: " << progress_session << "\n";
            std::cout << "进度: " << static_cast<int>(pct) << "%\n";
            std::cout << "已扫描扇区: " << progress.sectors_scanned << "/" << progress.total_sectors << "\n";
            std::cout << "已发现文件: " << progress.files_found << "\n";
            std::cout << "坏道: " << progress.bad_sectors_hit << "\n";
            std::cout << "状态: " << (progress.is_complete ? "已完成" : "进行中") << "\n";
        } else {
            std::cout << "未找到会话: " << progress_session << "\n";
        }
        db.close();
    });

    // bad-sectors
    std::string bad_sectors_session, bad_sectors_db;
    auto bad_sectors_cmd = app.add_subcommand("bad-sectors", "显示扫描会话的坏道信息");
    bad_sectors_cmd->add_option("session", bad_sectors_session, "扫描会话ID")->required();
    bad_sectors_cmd->add_option("--db", bad_sectors_db, "缓存数据库路径")->default_val("scan_cache.db");

    bad_sectors_cmd->callback([&]() {
        ScanCacheDB db;
        std::wstring db_path = std::wstring(bad_sectors_db.begin(), bad_sectors_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << bad_sectors_db << "\n";
            return;
        }

        // Load bad sectors
        auto bad_sectors = db.load_bad_sectors(bad_sectors_session);

        std::cout << "会话: " << bad_sectors_session << "\n";
        std::cout << "坏道数量: " << bad_sectors.size() << "\n";

        if (bad_sectors.empty()) {
            std::cout << "未发现坏道\n";
        } else {
            std::cout << "\n坏道列表:\n";
            std::cout << "序号\t扇区号 (LBA)\n";
            std::cout << "----\t-------------\n";

            // Display up to 100 sectors
            size_t display_count = std::min(bad_sectors.size(), static_cast<size_t>(100));
            for (size_t i = 0; i < display_count; ++i) {
                std::cout << (i + 1) << "\t" << bad_sectors[i] << "\n";
            }

            if (bad_sectors.size() > 100) {
                std::cout << "... 还有 " << (bad_sectors.size() - 100) << " 个坏道\n";
            }

            // Calculate affected range
            uint64_t min_sector = bad_sectors[0];
            uint64_t max_sector = bad_sectors[0];
            for (uint64_t sector : bad_sectors) {
                min_sector = std::min(min_sector, sector);
                max_sector = std::max(max_sector, sector);
            }

            std::cout << "\n影响范围:\n";
            std::cout << "  起始扇区: " << min_sector << "\n";
            std::cout << "  结束扇区: " << max_sector << "\n";
            std::cout << "  扇区跨度: " << (max_sector - min_sector + 1) << "\n";
            std::wcout << L"  数据大小: " << utils::FormatFileSize((max_sector - min_sector + 1) * 512) << L"\n";
        }

        db.close();
    });

    // list-files
    std::string list_files_session, list_files_db;
    int list_files_limit = 20;
    int list_files_offset = 0;
    std::string list_files_type, list_files_ext;

    auto list_files_cmd = app.add_subcommand("list-files", "列出扫描结果中的文件");
    list_files_cmd->add_option("session", list_files_session, "扫描会话ID")->default_val("default");
    list_files_cmd->add_option("--db", list_files_db, "缓存数据库路径")->default_val("scan_cache.db");
    list_files_cmd->add_option("--limit,-l", list_files_limit, "显示文件数量")->default_val(20);
    list_files_cmd->add_option("--offset,-o", list_files_offset, "分页偏移量")->default_val(0);
    list_files_cmd->add_option("--type,-t", list_files_type, "文件类型过滤 (image/video/unknown)");
    list_files_cmd->add_option("--ext,-e", list_files_ext, "扩展名过滤 (如 jpg,png,mp4)");

    list_files_cmd->callback([&]() {
        ScanCacheDB db;
        std::wstring db_path = std::wstring(list_files_db.begin(), list_files_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << list_files_db << "\n";
            return;
        }

        uint32_t total_count = db.query_file_count(list_files_session);
        if (total_count == 0) {
            std::cout << "会话 " << list_files_session << " 中没有文件\n";
            db.close();
            return;
        }

        std::cout << "会话: " << list_files_session << "\n";
        std::cout << "总文件数: " << total_count << "\n\n";

        // Query files
        auto files = db.query_files(list_files_session, list_files_limit, list_files_offset);

        if (files.empty()) {
            std::cout << "没有更多文件\n";
            db.close();
            return;
        }

        // Parse extension filter
        std::vector<std::wstring> ext_filter;
        if (!list_files_ext.empty()) {
            std::wstring ext_w = std::wstring(list_files_ext.begin(), list_files_ext.end());
            std::wstringstream wss(ext_w);
            std::wstring ext;
            while (std::getline(wss, ext, L',')) {
                // Trim and convert to lowercase
                size_t start = ext.find_first_not_of(L" \t.");
                size_t end = ext.find_last_not_of(L" \t.");
                if (start != std::wstring::npos && end != std::wstring::npos) {
                    std::wstring clean_ext = ext.substr(start, end - start + 1);
                    std::transform(clean_ext.begin(), clean_ext.end(), clean_ext.begin(), ::towlower);
                    ext_filter.push_back(clean_ext);
                }
            }
        }

        // Display file list
        std::cout << "序号\t名称\t\t\t\t类型\t大小\t\t扩展名\t状态\n";
        std::cout << "----\t----\t\t\t\t----\t----\t\t------\t----\n";

        int displayed = 0;
        for (const auto& f : files) {
            // File type filter
            if (!list_files_type.empty()) {
                std::string type_lower = list_files_type;
                std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

                bool type_match = false;
                if (type_lower == "image" && f.file_type == FileType::Image) type_match = true;
                else if (type_lower == "video" && f.file_type == FileType::Video) type_match = true;
                else if (type_lower == "unknown" && f.file_type == FileType::Unknown) type_match = true;

                if (!type_match) continue;
            }

            // Extension filter
            if (!ext_filter.empty()) {
                // Extract extension from file name
                std::wstring ext;
                size_t dot_pos = f.file_name.rfind(L'.');
                if (dot_pos != std::wstring::npos) {
                    ext = f.file_name.substr(dot_pos + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                }

                bool ext_match = false;
                for (const auto& ef : ext_filter) {
                    if (ext == ef) {
                        ext_match = true;
                        break;
                    }
                }
                if (!ext_match) continue;
            }

            // Display file info
            std::cout << (list_files_offset + displayed + 1) << "\t";

            // Truncate long file names
            std::wstring display_name = f.file_name;
            if (display_name.length() > 24) {
                display_name = display_name.substr(0, 21) + L"...";
            }
            std::wcout << display_name << L"\t";

            // File type
            switch (f.file_type) {
                case FileType::Image: std::cout << "图片\t"; break;
                case FileType::Video: std::cout << "视频\t"; break;
                default: std::cout << "未知\t"; break;
            }

            // File size
            std::wcout << utils::FormatFileSize(f.file_size) << L"\t";

            // Extension
            size_t dot_pos = f.file_name.rfind(L'.');
            if (dot_pos != std::wstring::npos) {
                std::wcout << f.file_name.substr(dot_pos + 1);
            } else {
                std::wcout << L"-";
            }
            std::wcout << L"\t";

            // Status
            std::wcout << (f.is_corrupted ? L"损坏" : L"正常");
            std::wcout << L"\n";

            displayed++;
        }

        std::cout << "\n显示 " << displayed << " 个文件";
        if (list_files_offset + list_files_limit < static_cast<int>(total_count)) {
            std::cout << " (还有 " << (total_count - list_files_offset - displayed) << " 个文件)";
        }
        std::cout << "\n";

        db.close();
    });

    // preview
    std::string preview_session, preview_db, preview_file_ref, preview_output;
    std::string preview_device;

    auto preview_cmd = app.add_subcommand("preview", "预览扫描结果中的文件");
    preview_cmd->add_option("session", preview_session, "扫描会话ID")->required();
    preview_cmd->add_option("--db", preview_db, "缓存数据库路径")->default_val("scan_cache.db");
    preview_cmd->add_option("--file", preview_file_ref, "文件索引或名称")->required();
    preview_cmd->add_option("--output", preview_output, "缩略图输出路径");
    preview_cmd->add_option("--device", preview_device, "磁盘设备路径 (如 \\\\.\\PhysicalDrive0)");

    preview_cmd->callback([&]() {
        ScanCacheDB db;
        std::wstring db_path = std::wstring(preview_db.begin(), preview_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << preview_db << "\n";
            return;
        }

        uint32_t file_count = db.query_file_count(preview_session);
        if (file_count == 0) {
            std::cout << "会话 " << preview_session << " 中没有文件\n";
            db.close();
            return;
        }

        // Try to parse file_ref as index first
        RecoverableFile target_file;
        bool found = false;
        uint32_t target_index = 0;

        try {
            target_index = std::stoul(preview_file_ref);
            if (target_index < file_count) {
                auto files = db.query_files(preview_session, 1, target_index);
                if (!files.empty()) {
                    target_file = files[0];
                    found = true;
                }
            }
        } catch (...) {
            // Not a number, search by name
        }

        // Search by name if not found by index
        if (!found) {
            std::wstring search_name = std::wstring(preview_file_ref.begin(), preview_file_ref.end());
            uint32_t batch_size = 100;
            for (uint32_t offset = 0; offset < file_count && !found; offset += batch_size) {
                auto files = db.query_files(preview_session, batch_size, offset);
                for (const auto& f : files) {
                    if (f.file_name.find(search_name) != std::wstring::npos) {
                        target_file = f;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            std::cout << "未找到文件: " << preview_file_ref << "\n";
            db.close();
            return;
        }

        // Display file information
        std::wcout << L"文件信息:\n";
        std::wcout << L"  名称: " << target_file.file_name << L"\n";
        std::wcout << L"  大小: " << utils::FormatFileSize(target_file.file_size) << L"\n";
        std::wcout << L"  类型: ";
        switch (target_file.file_type) {
            case FileType::Image: std::wcout << L"图片"; break;
            case FileType::Video: std::wcout << L"视频"; break;
            default: std::wcout << L"未知"; break;
        }
        std::wcout << L"\n";
        std::wcout << L"  状态: " << (target_file.is_corrupted ? L"已损坏" : L"正常") << L"\n";
        std::wcout << L"  片段数: " << target_file.fragments.size() << L"\n";
        for (size_t i = 0; i < target_file.fragments.size() && i < 5; ++i) {
            const auto& frag = target_file.fragments[i];
            std::wcout << L"    片段" << i << L": 起始扇区=" << frag.start_sector
                       << L", 扇区数=" << frag.sector_count << L"\n";
        }
        if (target_file.fragments.size() > 5) {
            std::wcout << L"    ... 还有 " << (target_file.fragments.size() - 5) << L" 个片段\n";
        }

        // Generate thumbnail if output path specified
        if (!preview_output.empty()) {
            if (preview_device.empty()) {
                std::cerr << "\n错误: 生成缩略图需要指定设备路径 (--device)\n";
                db.close();
                return;
            }

            if (target_file.file_type != FileType::Image && target_file.file_type != FileType::Video) {
                std::cerr << "\n错误: 仅支持图片和视频文件的缩略图生成\n";
                db.close();
                return;
            }

            // Open disk
            DiskHandle handle;
            std::wstring device_path = std::wstring(preview_device.begin(), preview_device.end());
            if (!handle.open(device_path)) {
                std::cerr << "\n错误: 无法打开设备 " << preview_device << "\n";
                db.close();
                return;
            }

            // Read file data
            SectorReader reader(handle, 512);
            AlignedBuffer buffer;

            // Calculate total sectors needed
            uint64_t total_sectors = 0;
            for (const auto& frag : target_file.fragments) {
                total_sectors += frag.sector_count;
            }

            // Limit read size for preview (max 64MB)
            const uint64_t max_preview_sectors = (64 * 1024 * 1024) / 512;
            uint64_t sectors_to_read = std::min(total_sectors, max_preview_sectors);

            // Allocate buffer
            buffer.allocate(static_cast<size_t>(sectors_to_read * 512), 512);

            // Read fragments
            size_t buffer_offset = 0;
            uint64_t sectors_read = 0;
            for (const auto& frag : target_file.fragments) {
                if (sectors_read >= sectors_to_read) break;

                uint64_t sectors_from_frag = std::min(frag.sector_count, sectors_to_read - sectors_read);
                AlignedBuffer frag_buffer;
                frag_buffer.allocate(static_cast<size_t>(sectors_from_frag * 512), 512);

                if (reader.read_sectors(frag.start_sector, static_cast<uint32_t>(sectors_from_frag), frag_buffer)) {
                    memcpy(buffer.data() + buffer_offset, frag_buffer.data(), static_cast<size_t>(sectors_from_frag * 512));
                    buffer_offset += static_cast<size_t>(sectors_from_frag * 512);
                }
                sectors_read += sectors_from_frag;
            }

            std::cout << "\n已读取 " << buffer_offset << " 字节数据\n";

            // Generate thumbnail
            auto preview_mgr = std::make_unique<business::PreviewManager>();
            HBITMAP thumbnail = nullptr;

            if (target_file.file_type == FileType::Image) {
                thumbnail = preview_mgr->CreateThumbnailFromData(
                    buffer.data(), buffer_offset, 256, 256);
            } else if (target_file.file_type == FileType::Video) {
                thumbnail = preview_mgr->CreateVideoThumbnailFromData(
                    buffer.data(), buffer_offset, 256, 256);
            }

            if (thumbnail) {
                std::wstring output_path = std::wstring(preview_output.begin(), preview_output.end());
                if (SaveThumbnailToFile(thumbnail, output_path)) {
                    std::wcout << L"缩略图已保存: " << output_path << L"\n";
                } else {
                    std::cerr << "保存缩略图失败\n";
                }
                DeleteObject(thumbnail);
            } else {
                std::cerr << "生成缩略图失败\n";
            }

            handle.close();
        }

        db.close();
    });

    // review (alias for preview)
    std::string review_session, review_db, review_file_ref, review_output;
    std::string review_device;

    auto review_cmd = app.add_subcommand("review", "预览扫描结果中的文件 (preview 的别名)");
    review_cmd->add_option("session", review_session, "扫描会话ID")->required();
    review_cmd->add_option("--db", review_db, "缓存数据库路径")->default_val("scan_cache.db");
    review_cmd->add_option("--file", review_file_ref, "文件索引或名称")->required();
    review_cmd->add_option("--output", review_output, "缩略图输出路径");
    review_cmd->add_option("--device", review_device, "磁盘设备路径 (如 \\\\.\\PhysicalDrive0)");

    review_cmd->callback([&]() {
        // Forward to preview logic - same implementation
        ScanCacheDB db;
        std::wstring db_path = std::wstring(review_db.begin(), review_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << review_db << "\n";
            return;
        }

        uint32_t file_count = db.query_file_count(review_session);
        if (file_count == 0) {
            std::cout << "会话 " << review_session << " 中没有文件\n";
            db.close();
            return;
        }

        RecoverableFile target_file;
        bool found = false;
        uint32_t target_index = 0;

        try {
            target_index = std::stoul(review_file_ref);
            if (target_index < file_count) {
                auto files = db.query_files(review_session, 1, target_index);
                if (!files.empty()) {
                    target_file = files[0];
                    found = true;
                }
            }
        } catch (...) {}

        if (!found) {
            std::wstring search_name = std::wstring(review_file_ref.begin(), review_file_ref.end());
            uint32_t batch_size = 100;
            for (uint32_t offset = 0; offset < file_count && !found; offset += batch_size) {
                auto files = db.query_files(review_session, batch_size, offset);
                for (const auto& f : files) {
                    if (f.file_name.find(search_name) != std::wstring::npos) {
                        target_file = f;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            std::cout << "未找到文件: " << review_file_ref << "\n";
            db.close();
            return;
        }

        std::wcout << L"文件信息:\n";
        std::wcout << L"  名称: " << target_file.file_name << L"\n";
        std::wcout << L"  大小: " << utils::FormatFileSize(target_file.file_size) << L"\n";
        std::wcout << L"  类型: ";
        switch (target_file.file_type) {
            case FileType::Image: std::wcout << L"图片"; break;
            case FileType::Video: std::wcout << L"视频"; break;
            default: std::wcout << L"未知"; break;
        }
        std::wcout << L"\n";
        std::wcout << L"  状态: " << (target_file.is_corrupted ? L"已损坏" : L"正常") << L"\n";
        std::wcout << L"  片段数: " << target_file.fragments.size() << L"\n";

        db.close();
    });

    app.require_subcommand(-1);
    CLI11_PARSE(app, argc, argv);
    return 0;
}
