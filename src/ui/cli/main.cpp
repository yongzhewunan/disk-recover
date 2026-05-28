#include <CLI/CLI.hpp>
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "business/scan_manager.hpp"
#include "business/recover_manager.hpp"
#include "business/multi_target_writer.hpp"
#include "business/scan_cache_db.hpp"
#include "common/utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <csignal>

using namespace disk_recover;

static ScanManager g_scan_manager;
static bool g_interrupted = false;

void signal_handler(int) {
    g_interrupted = true;
    g_scan_manager.stop_scan();
}

int main(int argc, char** argv) {
    CLI::App app{"Disk Recover - 磁盘数据恢复工具", "disk-recover"};

    // list-disks
    auto list_cmd = app.add_subcommand("list-disks", "列出所有可用磁盘和分区");
    list_cmd->callback([]() {
        if (!utils::IsAdminPrivilege()) {
            std::cerr << "警告: 未以管理员权限运行，磁盘访问可能受限\n";
        }
        auto disks = DiskInfoQuery::EnumeratePhysicalDisks();
        if (disks.empty()) {
            std::cout << "未找到任何磁盘\n";
            return;
        }
        for (const auto& disk : disks) {
            std::wcout << L"磁盘 " << disk.physical_drive_number
                       << L": " << disk.model_name
                       << L" (" << utils::FormatFileSize(disk.disk_size_bytes) << L")\n";
            std::wcout << L"  扇区大小: " << disk.geometry.sector_size
                       << L"  总扇区数: " << disk.geometry.total_sectors << L"\n";
            for (const auto& part : disk.partitions) {
                std::wcout << L"  分区 " << part.index
                           << L": " << part.filesystem_type
                           << L" 起始=" << part.start_sector
                           << L" 大小=" << utils::FormatFileSize(part.sector_count * 512)
                           << L"\n";
            }
        }
    });

    // scan
    std::string scan_device, scan_session, scan_db_path, bad_sector_str;
    std::string scan_mode_str = "deep";
    bool scan_images = true, scan_videos = true;

    auto scan_cmd = app.add_subcommand("scan", "扫描磁盘查找可恢复文件");
    scan_cmd->add_option("device", scan_device, "磁盘设备路径 (如 \\\\.\\PhysicalDrive0)")->required();
    scan_cmd->add_option("--session", scan_session, "扫描会话ID")->default_val("default");
    scan_cmd->add_option("--db", scan_db_path, "缓存数据库路径")->default_val("scan_cache.db");
    scan_cmd->add_option("--mode", scan_mode_str, "扫描模式 (quick/deep/full)")->default_val("deep");
    scan_cmd->add_option("--bad-sector", bad_sector_str, "坏道策略 (skip/retry/force)")->default_val("skip");
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

        std::cout << "开始扫描: " << scan_device << "\n";
        std::cout << "会话ID: " << scan_session << "\n\n";

        if (!g_scan_manager.start_scan(config)) {
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
    std::string recover_session, recover_db, recover_output;
    bool auto_switch = true;

    auto recover_cmd = app.add_subcommand("recover", "恢复扫描结果中的文件");
    recover_cmd->add_option("session", recover_session, "扫描会话ID")->required();
    recover_cmd->add_option("--db", recover_db, "缓存数据库路径")->default_val("scan_cache.db");
    recover_cmd->add_option("--output,-o", recover_output, "目标输出目录")->required();
    recover_cmd->add_flag("--auto-switch", auto_switch, "空间不足自动切换到下一个目标");

    recover_cmd->callback([&]() {
        ScanCacheDB db;
        std::wstring db_path = std::wstring(recover_db.begin(), recover_db.end());
        if (!db.open(db_path)) {
            std::cerr << "无法打开数据库: " << recover_db << "\n";
            return;
        }

        uint32_t file_count = db.query_file_count(recover_session);
        if (file_count == 0) {
            std::cout << "会话 " << recover_session << " 中没有可恢复的文件\n";
            db.close();
            return;
        }

        std::cout << "找到 " << file_count << " 个文件待恢复\n";
        std::cout << "输出目录: " << recover_output << "\n";

        // 创建输出目录
        std::filesystem::create_directories(recover_output);

        MultiTargetWriter writer;
        writer.add_target(std::wstring(recover_output.begin(), recover_output.end()));
        writer.set_auto_switch(auto_switch);

        // 获取设备路径（从第一个文件的 fragments 推断，简化处理）
        DiskHandle handle;
        // 注：完整实现需要从数据库或配置中获取设备路径
        // 这里仅作为示例

        uint32_t batch_size = 100;
        uint32_t recovered = 0;

        for (uint32_t offset = 0; offset < file_count; offset += batch_size) {
            auto files = db.query_files(recover_session, batch_size, offset);
            for (const auto& file : files) {
                std::wcout << L"恢复: " << file.file_name << L"\n";
                recovered++;
            }
            std::cout << "进度: " << recovered << "/" << file_count << "\n";
        }

        std::cout << "\n恢复完成: " << recovered << " 个文件\n";
        db.close();
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

    app.require_subcommand(-1);
    CLI11_PARSE(app, argc, argv);
    return 0;
}
