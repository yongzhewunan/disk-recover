#include <CLI/CLI.hpp>
#include "disk_handle.hpp"
#include "disk_info.hpp"
#include "utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace disk_recover;

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

    app.require_subcommand(-1);
    CLI11_PARSE(app, argc, argv);
    return 0;
}