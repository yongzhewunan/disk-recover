#include "scan_recover_manager.hpp"
#include "disk_info.hpp"
#include "signature_scanner.hpp"
#include "../disk-io/buffered_reader.hpp"
#include "../filesystem/ntfs/mft_parser.hpp"
#include "../filesystem/fat/fat_parser.hpp"
#include "../filesystem/exfat/exfat_parser.hpp"
#include "../common/logger.hpp"
#include <algorithm>
#include <filesystem>
#include <cstring>

namespace disk_recover {

ScanAndRecoverManager::~ScanAndRecoverManager() {
    stop();
}

bool ScanAndRecoverManager::start(const Config& config) {
    if (running_.load()) return false;

    if (worker_.joinable()) {
        worker_.join();
    }

    running_ = true;
    paused_ = false;
    stop_requested_ = false;
    progress_ = {};
    ext_counters_.clear();
    ext_subfolders_.clear();
    active_config_ = config;

    worker_ = std::thread(&ScanAndRecoverManager::worker_thread, this, config);
    return true;
}

void ScanAndRecoverManager::pause() {
    paused_ = true;
}

void ScanAndRecoverManager::resume() {
    paused_ = false;
}

void ScanAndRecoverManager::stop() {
    stop_requested_ = true;
    paused_ = false;  // Unpause so worker can see stop flag and exit
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ScanAndRecoverManager::stop_request_only() {
    stop_requested_ = true;
    paused_ = false;  // Unpause so worker can see stop flag and exit
    // Do NOT join here — caller must not block (e.g. GUI thread).
    // The worker will exit on its own and post WM_SCAN_RECOVER_COMPLETE.
    // Join happens in start() (before spawning new worker) or destructor.
}

ScanAndRecoverManager::Progress ScanAndRecoverManager::progress() const {
    std::lock_guard lock(progress_mutex_);
    return progress_;
}

std::wstring ScanAndRecoverManager::generate_output_path(const std::wstring& base_dir,
                                                          const std::wstring& filename) {
    // Extract extension
    std::wstring ext;
    size_t dot_pos = filename.rfind(L'.');
    if (dot_pos != std::wstring::npos) {
        ext = filename.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    } else {
        ext = L"unknown";
    }

    // Build key for counter tracking
    std::wstring key = base_dir + L"/" + ext;
    uint32_t& counter = ext_counters_[key];
    uint32_t& subfolder = ext_subfolders_[key];

    // Create subfolder path
    std::wstring subfolder_path;
    if (counter > 0 && counter % 500 == 0) {
        // Start new subfolder every 500 files
        subfolder++;
    }

    if (subfolder > 0) {
        subfolder_path = ext + L"(" + std::to_wstring(subfolder) + L")";
    } else {
        subfolder_path = ext;
    }

    counter++;

    // Build full path
    std::filesystem::path full_path = std::filesystem::path(base_dir) / subfolder_path;
    std::filesystem::create_directories(full_path);

    return full_path.wstring() + L"\\" + filename;
}

bool ScanAndRecoverManager::recover_file(const RecoverableFile& file, BufferedSectorReader& reader,
                                          const std::wstring& output_dir) {
    std::wstring output_path = generate_output_path(output_dir, file.file_name);

    // Calculate total size from fragments
    uint64_t total_size = 0;
    for (const auto& frag : file.fragments) {
        total_size += frag.sector_count * reader.sector_size();
    }

    // Allocate buffer for file content
    uint32_t sector_size = reader.sector_size();
    uint32_t max_sectors_per_read = 8192;  // 4MB chunks

    std::vector<uint8_t> file_buffer;
    file_buffer.reserve(total_size);

    AlignedBuffer read_buf(max_sectors_per_read * sector_size, sector_size);

    // Read all fragments
    for (const auto& frag : file.fragments) {
        uint64_t remaining = frag.sector_count;
        uint64_t current_sector = frag.start_sector;

        while (remaining > 0 && !stop_requested_.load()) {
            // Wait if paused
            while (paused_.load() && !stop_requested_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (stop_requested_.load()) {
                return false;
            }

            uint32_t to_read = static_cast<uint32_t>(std::min<uint64_t>(remaining, max_sectors_per_read));

            if (!reader.read_sectors_checked(current_sector, to_read, read_buf)) {
                // Partial read - fill with zeros
                file_buffer.insert(file_buffer.end(), to_read * sector_size, 0);
                LOG_FMT(L"[ScanRecover] Partial read at sector %llu, filling zeros", current_sector);
            } else {
                file_buffer.insert(file_buffer.end(), read_buf.data(), read_buf.data() + to_read * sector_size);
            }

            current_sector += to_read;
            remaining -= to_read;
        }

        if (stop_requested_.load()) {
            return false;
        }
    }

    // Check space and switch if needed
    if (!check_space_and_switch(active_config_)) {
        // Auto-paused due to low space - wait for resume
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stop_requested_.load()) return false;

        // Re-check space after resume
        writer_.refresh_space_info();
        if (!writer_.has_space(active_config_.min_free_space)) {
            LOG_MSG(L"[ScanRecover] Still no space after resume, skipping file");
            return false;
        }
    }

    // Write file
    HANDLE hFile = CreateFileW(output_path.c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_FMT(L"[ScanRecover] Failed to create file: %s, error=%d",
                 output_path.c_str(), GetLastError());
        return false;
    }

    DWORD bytes_written = 0;
    BOOL ok = WriteFile(hFile, file_buffer.data(), static_cast<DWORD>(file_buffer.size()),
                        &bytes_written, nullptr);
    CloseHandle(hFile);

    if (!ok || bytes_written != file_buffer.size()) {
        LOG_FMT(L"[ScanRecover] Failed to write file: %s", output_path.c_str());
        DeleteFileW(output_path.c_str());
        return false;
    }

    LOG_FMT(L"[ScanRecover] Recovered: %s (%llu bytes)", output_path.c_str(), file_buffer.size());

    // Notify callback of recovered file
    if (active_config_.on_file_recovered) {
        active_config_.on_file_recovered(file, output_path);
    }

    return true;
}

bool ScanAndRecoverManager::check_space_and_switch(const Config& config) {
    writer_.refresh_space_info();

    if (writer_.has_space(config.min_free_space)) {
        return true;
    }

    // Try to switch to next target
    if (writer_.switch_to_next_target()) {
        LOG_FMT(L"[ScanRecover] Switched to target: %s", writer_.current_target().c_str());
        return true;
    }

    // All targets lack space - auto-pause
    LOG_MSG(L"[ScanRecover] All save directories have less than 2GB free space - auto-pausing");
    paused_ = true;

    if (config.hwnd) {
        PostMessageW(config.hwnd, SRM_WM_SCAN_RECOVER_PAUSED, 0, 0);
    }

    return false;
}

// Detect filesystem type from boot sector
ScanAndRecoverManager::FileSystemType ScanAndRecoverManager::detect_filesystem(SectorReader& reader, uint64_t start_sector) {
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(start_sector, 1, buf)) {
        return FileSystemType::Unknown;
    }

    const uint8_t* data = buf.data();

    // Check MBR signature
    uint16_t signature = *reinterpret_cast<const uint16_t*>(data + 510);
    if (signature != 0xAA55) {
        return FileSystemType::Unknown;
    }

    // Check exFAT
    const char exfat_id[] = "EXFAT   ";
    if (std::memcmp(data + 3, exfat_id, 8) == 0) {
        return FileSystemType::ExFAT;
    }

    // Check NTFS
    const char ntfs_id[] = "NTFS    ";
    if (std::memcmp(data + 3, ntfs_id, 8) == 0) {
        return FileSystemType::NTFS;
    }

    // Check FAT
    uint16_t bytes_per_sector = *reinterpret_cast<const uint16_t*>(data + 11);
    uint8_t sectors_per_cluster = data[13];

    if (bytes_per_sector == 0 || bytes_per_sector % 512 != 0 ||
        sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        return FileSystemType::Unknown;
    }

    uint16_t reserved_sectors = *reinterpret_cast<const uint16_t*>(data + 14);
    uint8_t fat_count = data[16];
    uint16_t root_entry_count = *reinterpret_cast<const uint16_t*>(data + 17);
    uint16_t sectors_per_fat_16 = *reinterpret_cast<const uint16_t*>(data + 22);
    uint32_t total_sectors_16 = *reinterpret_cast<const uint16_t*>(data + 19);
    uint32_t total_sectors_32 = *reinterpret_cast<const uint32_t*>(data + 32);

    uint32_t total_sectors = total_sectors_16 != 0 ? total_sectors_16 : total_sectors_32;
    uint32_t fat_size = sectors_per_fat_16 != 0 ? sectors_per_fat_16 : *reinterpret_cast<const uint32_t*>(data + 36);

    uint32_t root_dir_sectors = ((root_entry_count * 32) + bytes_per_sector - 1) / bytes_per_sector;
    uint32_t data_sectors = total_sectors - (reserved_sectors + (fat_count * fat_size) + root_dir_sectors);
    uint32_t total_clusters = data_sectors / sectors_per_cluster;

    if (total_clusters < 4085) return FileSystemType::FAT12;
    if (total_clusters < 65525) return FileSystemType::FAT16;
    return FileSystemType::FAT32;
}

void ScanAndRecoverManager::worker_thread(Config config) {
    LOG_MSG(L"[ScanRecover] Starting scan-recover thread");

    DiskHandle handle;
    if (!handle.open(config.device_path)) {
        LOG_FMT(L"[ScanRecover] Failed to open disk: %s", config.device_path.c_str());
        running_ = false;
        Progress p{};
        p.is_complete = true;
        if (config.on_progress) config.on_progress(p);
        if (config.hwnd) PostMessageW(config.hwnd, SRM_WM_SCAN_RECOVER_COMPLETE, 0, 0);
        return;
    }

    LOG_FMT(L"[ScanRecover] Opened disk: %s", config.device_path.c_str());

    // Query disk geometry
    DiskGeometry geo{};
    DiskInfoQuery::QueryDiskGeometry(handle, geo);
    LOG_FMT(L"[ScanRecover] Disk geometry: sector_size=%u, total_sectors=%llu",
             geo.sector_size, geo.total_sectors);

    uint64_t start_sector = config.start_sector;
    uint64_t end_sector = (config.end_sector > 0) ? config.end_sector : geo.total_sectors;

    {
        std::lock_guard lock(progress_mutex_);
        progress_.total_sectors = end_sector - start_sector;
    }

    // Create sector reader with optimizations
    BufferedSectorReader reader(handle, geo.sector_size, 16 * 1024 * 1024);
    reader.set_bad_sector_policy(config.bad_sector_policy);
    reader.set_skip_ahead_config(config.skip_config);
    reader.set_timeout_config(config.timeout_config);

    // Create a separate SectorReader for metadata parsing (filesystem parsers need SectorReader&)
    SectorReader meta_reader(handle, geo.sector_size);

    // Prepare output directory
    if (config.output_dirs.empty()) {
        LOG_MSG(L"[ScanRecover] No output directories specified");
        running_ = false;
        if (config.hwnd) PostMessageW(config.hwnd, SRM_WM_SCAN_RECOVER_COMPLETE, 0, 0);
        return;
    }

    // Create output directories and setup MultiTargetWriter
    for (const auto& dir : config.output_dirs) {
        std::filesystem::create_directories(dir);
        LOG_FMT(L"[ScanRecover] Output directory: %s", dir.c_str());
        writer_.add_target(dir);
    }
    writer_.set_auto_switch(true);
    writer_.set_min_free_space(config.min_free_space);

    // Callback for recovering found files
    auto on_file_found = [this, &reader](RecoverableFile&& file) {
        // Wait if paused
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (stop_requested_.load()) return;

        // Skip files with any corruption — they are too damaged to be usable
        if (file.corruption_level != CorruptionLevel::None) {
            LOG_FMT(L"[ScanRecover] Skipping %s (corruption level=%u, confidence=%u)",
                     file.file_name.c_str(), static_cast<unsigned>(file.corruption_level),
                     static_cast<unsigned>(file.confidence));
            {
                std::lock_guard lock(progress_mutex_);
                progress_.files_found++;
                progress_.files_failed++;
            }
            return;
        }

        // Get current target from writer
        std::wstring output_dir = writer_.current_target();

        // Immediately recover the file
        bool success = recover_file(file, reader, output_dir);

        {
            std::lock_guard lock(progress_mutex_);
            progress_.files_found++;
            if (success) {
                progress_.files_recovered++;
                progress_.bytes_recovered += file.file_size;
            } else {
                progress_.files_failed++;
            }
        }

        // Log recovery
        if (progress_.files_found % 100 == 0) {
            LOG_FMT(L"[ScanRecover] Files: found=%u, recovered=%u, failed=%u",
                     progress_.files_found, progress_.files_recovered, progress_.files_failed);
        }
    };

    uint32_t total_skipped = 0;
    auto last_progress_time = std::chrono::steady_clock::now();

    auto on_scan_progress = [this, &reader, &total_skipped, &last_progress_time, &config](const ScanProgress& scan_p) {
        // Wait if paused
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (stop_requested_.load()) return;

        uint32_t skip_ahead = reader.get_skip_ahead_count();

        {
            std::lock_guard lock(progress_mutex_);
            progress_.sectors_scanned = scan_p.sectors_scanned;
            progress_.bad_sectors = scan_p.bad_sectors_hit;
            progress_.sectors_skipped = total_skipped + skip_ahead;
            progress_.current_sector = config.start_sector + scan_p.sectors_scanned;

            if (progress_.total_sectors > 0) {
                progress_.percent = static_cast<uint8_t>(
                    100 * progress_.sectors_scanned / progress_.total_sectors);
            }
        }

        // Log progress periodically
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_progress_time).count();
        if (elapsed >= 60 || skip_ahead > 0) {
            LOG_FMT(L"[ScanRecover] Progress: %u%% (%llu/%llu), files=%u, bad=%u, skipped=%u, skip-ahead=%u",
                     progress_.percent, progress_.sectors_scanned, progress_.total_sectors,
                     progress_.files_found, progress_.bad_sectors, progress_.sectors_skipped, skip_ahead);
            last_progress_time = now;
        }

        if (skip_ahead > 0) {
            total_skipped += skip_ahead;
            reader.reset_bad_sector_counter();
        }

        if (config.on_progress) config.on_progress(progress_);
    };

    // --- Scan Mode Dispatch ---
    switch (config.mode) {
    case ScanMode::Quick: {
        LOG_MSG(L"[ScanRecover] Quick scan mode (partition table only)");
        FileSystemType fs_type = detect_filesystem(meta_reader, start_sector);
        LOG_FMT(L"[ScanRecover] Detected file system type: %d", static_cast<int>(fs_type));

        switch (fs_type) {
        case FileSystemType::NTFS: {
            ntfs::MftParser parser;
            if (parser.parse_boot_sector(meta_reader, start_sector)) {
                LOG_MSG(L"[ScanRecover] Parsing NTFS MFT...");
                parser.enumerate_mft(meta_reader, on_file_found, true,
                    [this]() { return stop_requested_.load(); });
                {
                    std::lock_guard lock(progress_mutex_);
                    progress_.sectors_scanned = progress_.total_sectors;
                    progress_.percent = 100;
                }
                LOG_FMT(L"[ScanRecover] NTFS scan done, files_found=%u", progress_.files_found);
            } else {
                LOG_MSG(L"[ScanRecover] Failed to parse NTFS boot sector");
            }
            break;
        }

        case FileSystemType::FAT12:
        case FileSystemType::FAT16:
        case FileSystemType::FAT32: {
            fat::FatParser parser;
            if (parser.parse_boot_sector(meta_reader, start_sector)) {
                LOG_MSG(L"[ScanRecover] Parsing FAT root directory...");
                parser.enumerate_root_dir(meta_reader, on_file_found, true,
                    [this]() { return stop_requested_.load(); });
                {
                    std::lock_guard lock(progress_mutex_);
                    progress_.sectors_scanned = progress_.total_sectors;
                    progress_.percent = 100;
                }
                LOG_FMT(L"[ScanRecover] FAT scan done, files_found=%u", progress_.files_found);
            } else {
                LOG_MSG(L"[ScanRecover] Failed to parse FAT boot sector");
            }
            break;
        }

        case FileSystemType::ExFAT:
            LOG_MSG(L"[ScanRecover] exFAT detected - using RAW scan fallback");
            {
                SignatureScanner scanner;
                SignatureScanner::ScanConfig scan_config{};
                scan_config.start_sector = start_sector;
                scan_config.end_sector = end_sector;
                scan_config.scan_images = config.scan_images;
                scan_config.scan_videos = config.scan_videos;
                scan_config.should_stop = [this]() { return stop_requested_.load(); };
                scanner.scan(reader, scan_config, on_file_found, on_scan_progress);
            }
            break;

        case FileSystemType::Unknown:
            LOG_MSG(L"[ScanRecover] Unknown file system - performing RAW scan");
            {
                SignatureScanner scanner;
                SignatureScanner::ScanConfig scan_config{};
                scan_config.start_sector = start_sector;
                scan_config.end_sector = end_sector;
                scan_config.scan_images = config.scan_images;
                scan_config.scan_videos = config.scan_videos;
                scan_config.should_stop = [this]() { return stop_requested_.load(); };
                scanner.scan(reader, scan_config, on_file_found, on_scan_progress);
            }
            break;
        }
        break;
    }

    case ScanMode::Deep: {
        LOG_MSG(L"[ScanRecover] Deep scan mode (partition table + raw)");
        FileSystemType fs_type = detect_filesystem(meta_reader, start_sector);
        LOG_FMT(L"[ScanRecover] Detected file system type: %d", static_cast<int>(fs_type));
        uint64_t metadata_end_sector = 0;

        // Phase 1: metadata scan
        switch (fs_type) {
        case FileSystemType::NTFS: {
            ntfs::MftParser parser;
            if (parser.parse_boot_sector(meta_reader, start_sector)) {
                LOG_MSG(L"[ScanRecover] Phase 1: Parsing NTFS MFT...");
                parser.enumerate_mft(meta_reader, on_file_found, true,
                    [this]() { return stop_requested_.load(); });
                metadata_end_sector = parser.mft_start_sector() +
                    (geo.total_sectors / parser.mft_record_size());
                {
                    std::lock_guard lock(progress_mutex_);
                    progress_.sectors_scanned = metadata_end_sector;
                    if (progress_.total_sectors > 0) {
                        progress_.percent = static_cast<uint8_t>(
                            100 * progress_.sectors_scanned / progress_.total_sectors);
                    }
                }
                if (config.on_progress) config.on_progress(progress_);
            }
            break;
        }

        case FileSystemType::FAT12:
        case FileSystemType::FAT16:
        case FileSystemType::FAT32: {
            fat::FatParser parser;
            if (parser.parse_boot_sector(meta_reader, start_sector)) {
                LOG_MSG(L"[ScanRecover] Phase 1: Parsing FAT root directory...");
                parser.enumerate_root_dir(meta_reader, on_file_found, true,
                    [this]() { return stop_requested_.load(); });
                metadata_end_sector = parser.data_start_sector();
                {
                    std::lock_guard lock(progress_mutex_);
                    progress_.sectors_scanned = metadata_end_sector;
                    if (progress_.total_sectors > 0) {
                        progress_.percent = static_cast<uint8_t>(
                            100 * progress_.sectors_scanned / progress_.total_sectors);
                    }
                }
                if (config.on_progress) config.on_progress(progress_);
            }
            break;
        }

        case FileSystemType::ExFAT:
        case FileSystemType::Unknown:
            metadata_end_sector = start_sector;
            break;
        }

        // Phase 2: RAW scan
        if (metadata_end_sector < end_sector && !stop_requested_.load()) {
            LOG_FMT(L"[ScanRecover] Phase 2: RAW scan sectors %llu to %llu",
                     metadata_end_sector, end_sector);
            SignatureScanner scanner;
            SignatureScanner::ScanConfig scan_config{};
            scan_config.start_sector = metadata_end_sector;
            scan_config.end_sector = end_sector;
            scan_config.scan_images = config.scan_images;
            scan_config.scan_videos = config.scan_videos;
            scan_config.should_stop = [this]() { return stop_requested_.load(); };
            scanner.scan(reader, scan_config, on_file_found, on_scan_progress);
        }
        break;
    }

    case ScanMode::Full: {
        LOG_MSG(L"[ScanRecover] Full scan mode - entire disk RAW scan");
        SignatureScanner scanner;
        SignatureScanner::ScanConfig scan_config{};
        scan_config.start_sector = start_sector;
        scan_config.end_sector = end_sector;
        scan_config.scan_images = config.scan_images;
        scan_config.scan_videos = config.scan_videos;
        scan_config.should_stop = [this]() { return stop_requested_.load(); };
        scanner.scan(reader, scan_config, on_file_found, on_scan_progress);
        break;
    }
    }

    // Final progress
    {
        std::lock_guard lock(progress_mutex_);
        progress_.is_complete = true;
        progress_.sectors_scanned = progress_.total_sectors;
        progress_.percent = 100;
    }

    LOG_FMT(L"[ScanRecover] Complete: files=%u, recovered=%u, failed=%u, bytes=%llu, bad=%u, skipped=%u",
             progress_.files_found, progress_.files_recovered, progress_.files_failed,
             progress_.bytes_recovered, progress_.bad_sectors, progress_.sectors_skipped);

    running_ = false;
    if (config.on_progress) config.on_progress(progress_);
    if (config.hwnd) PostMessageW(config.hwnd, SRM_WM_SCAN_RECOVER_COMPLETE, 0, 0);
}

} // namespace disk_recover