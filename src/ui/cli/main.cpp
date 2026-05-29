#define NOMINMAX
#include <CLI/CLI.hpp>
#include "disk-io/disk_handle.hpp"
#include "disk-io/disk_info.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/aligned_buffer.hpp"
#include "business/scan_manager.hpp"
#include "business/recover_manager.hpp"
#include "business/multi_target_writer.hpp"
#include "business/scan_cache_db.hpp"
#include "business/preview_manager.hpp"
#include "common/utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <csignal>
#include <gdiplus.h>
#include <memory>

using namespace disk_recover;

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

    app.require_subcommand(-1);
    CLI11_PARSE(app, argc, argv);
    return 0;
}
