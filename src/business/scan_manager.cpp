#include "scan_manager.hpp"
#include "disk_handle.hpp"
#include "disk_info.hpp"
#include "signature_scanner.hpp"
#include "../filesystem/ntfs/mft_parser.hpp"
#include "../filesystem/fat/fat_parser.hpp"
#include "../filesystem/exfat/exfat_parser.hpp"
#include <thread>
#include <cstring>

namespace disk_recover {

// Helper function to detect file system type from boot sector
static FileSystemType detect_filesystem(SectorReader& reader, uint64_t partition_start) {
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) {
        return FileSystemType::Unknown;
    }

    const uint8_t* data = buf.data();

    // Check boot sector signature
    uint16_t signature = *reinterpret_cast<const uint16_t*>(data + 510);
    if (signature != 0xAA55) {
        return FileSystemType::Unknown;
    }

    // Check for exFAT: "EXFAT   " at offset 3
    const char exfat_id[] = "EXFAT   ";
    if (std::memcmp(data + 3, exfat_id, 8) == 0) {
        return FileSystemType::ExFAT;
    }

    // Check for NTFS: "NTFS    " at offset 3
    const char ntfs_id[] = "NTFS    ";
    if (std::memcmp(data + 3, ntfs_id, 8) == 0) {
        return FileSystemType::NTFS;
    }

    // Check for FAT file system
    // FAT has BPB (BIOS Parameter Block) structure
    uint16_t bytes_per_sector = *reinterpret_cast<const uint16_t*>(data + 11);
    uint8_t sectors_per_cluster = data[13];

    if (bytes_per_sector == 0 || bytes_per_sector % 512 != 0 ||
        sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        return FileSystemType::Unknown;
    }

    // Determine FAT type based on cluster count
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

bool ScanManager::start_scan(const Config& config) {
    if (scanning_.load()) return false;

    scanning_ = true;
    paused_ = false;
    stop_requested_ = false;
    progress_ = {};
    current_session_id_ = config.session_id;

    if (!cache_db_.open(config.db_path)) {
        scanning_ = false;
        return false;
    }
    cache_db_.create_session(config.session_id);

    std::thread(&ScanManager::scan_thread_func, this, config).detach();
    return true;
}

void ScanManager::pause_scan() { paused_ = true; }
void ScanManager::resume_scan() { paused_ = false; }

void ScanManager::stop_scan() {
    stop_requested_ = true;
    scanning_ = false;
    flush_cache(current_session_id_);
    cache_db_.close();
}

ScanProgress ScanManager::progress() const {
    std::lock_guard lock(progress_mutex_);
    return progress_;
}

void ScanManager::scan_thread_func(Config config) {
    DiskHandle handle;
    if (!handle.open(config.device_path)) {
        OutputDebugStringW(L"[ScanManager] Failed to open disk\n");
        scanning_ = false;
        cache_db_.close();
        // Notify completion even on failure
        if (on_progress_) {
            ScanProgress p{};
            p.is_complete = true;
            on_progress_(p);
        }
        return;
    }

    DiskGeometry geo{};
    DiskInfoQuery::QueryDiskGeometry(handle, geo);

    SectorReader reader(handle, geo.sector_size);
    BadSectorManager bad_mgr;
    reader.set_bad_sector_manager(&bad_mgr);
    reader.set_bad_sector_policy(config.bad_sector_policy);

    uint64_t start_sector = config.start_sector;
    uint64_t end_sector = (config.end_sector > 0) ? config.end_sector : geo.total_sectors;
    progress_.total_sectors = end_sector - start_sector;

    auto on_file = [this](RecoverableFile&& file) {
        if (stop_requested_.load()) return;
        {
            std::lock_guard lock(progress_mutex_);
            progress_.files_found++;
        }
        pending_files_.push_back(std::move(file));
        if (pending_files_.size() >= FLUSH_THRESHOLD) {
            flush_cache(current_session_id_);
        }
        if (on_file_found_) on_file_found_(pending_files_.back());
    };

    auto on_scan_progress = [this](const ScanProgress& p) {
        if (stop_requested_.load()) return;
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        {
            std::lock_guard lock(progress_mutex_);
            progress_.sectors_scanned = p.sectors_scanned;
            progress_.bad_sectors_hit = p.bad_sectors_hit;
        }
        cache_db_.save_progress(current_session_id_, progress_);
        if (on_progress_) on_progress_(progress_);
    };

    OutputDebugStringW(L"[ScanManager] Starting scan...\n");

    switch (config.mode) {
    case ScanMode::Quick:
        // Quick: Only file system metadata, no RAW scanning
        OutputDebugStringW(L"[ScanManager] Quick scan mode - file system metadata only\n");
        {
            FileSystemType fs_type = detect_filesystem(reader, start_sector);
            OutputDebugStringW(L"[ScanManager] Detected file system type\n");

            switch (fs_type) {
            case FileSystemType::NTFS:
                {
                    ntfs::MftParser parser;
                    if (parser.parse_boot_sector(reader, start_sector)) {
                        OutputDebugStringW(L"[ScanManager] Parsing NTFS MFT...\n");
                        parser.enumerate_mft(reader, on_file, true);
                        {
                            std::lock_guard lock(progress_mutex_);
                            progress_.sectors_scanned = parser.mft_start_sector() +
                                (geo.total_sectors / parser.mft_record_size());
                        }
                    }
                }
                break;

            case FileSystemType::FAT12:
            case FileSystemType::FAT16:
            case FileSystemType::FAT32:
                {
                    fat::FatParser parser;
                    if (parser.parse_boot_sector(reader, start_sector)) {
                        OutputDebugStringW(L"[ScanManager] Parsing FAT root directory...\n");
                        parser.enumerate_root_dir(reader, on_file, true);
                        {
                            std::lock_guard lock(progress_mutex_);
                            progress_.sectors_scanned = parser.data_start_sector();
                        }
                    }
                }
                break;

            case FileSystemType::ExFAT:
                // exFAT parser doesn't have enumerate method yet
                // Fall through to full scan for now
                OutputDebugStringW(L"[ScanManager] exFAT detected - using RAW scan fallback\n");
                {
                    SignatureScanner scanner;
                    SignatureScanner::ScanConfig scan_config{};
                    scan_config.start_sector = start_sector;
                    scan_config.end_sector = end_sector;
                    scan_config.scan_images = config.scan_images;
                    scan_config.scan_videos = config.scan_videos;
                    scanner.scan(reader, scan_config, on_file, on_scan_progress);
                }
                break;

            case FileSystemType::Unknown:
                OutputDebugStringW(L"[ScanManager] Unknown file system - performing RAW scan\n");
                {
                    SignatureScanner scanner;
                    SignatureScanner::ScanConfig scan_config{};
                    scan_config.start_sector = start_sector;
                    scan_config.end_sector = end_sector;
                    scan_config.scan_images = config.scan_images;
                    scan_config.scan_videos = config.scan_videos;
                    scanner.scan(reader, scan_config, on_file, on_scan_progress);
                }
                break;
            }
        }
        break;

    case ScanMode::Deep:
        // Deep: Quick scan first (file system metadata), then RAW on free space
        OutputDebugStringW(L"[ScanManager] Deep scan mode - metadata + RAW on free space\n");
        {
            // Phase 1: Quick scan (file system metadata)
            FileSystemType fs_type = detect_filesystem(reader, start_sector);
            uint64_t metadata_end_sector = 0;

            switch (fs_type) {
            case FileSystemType::NTFS:
                {
                    ntfs::MftParser parser;
                    if (parser.parse_boot_sector(reader, start_sector)) {
                        OutputDebugStringW(L"[ScanManager] Phase 1: Parsing NTFS MFT...\n");
                        parser.enumerate_mft(reader, on_file, true);
                        metadata_end_sector = parser.mft_start_sector() +
                            (geo.total_sectors / parser.mft_record_size());
                        {
                            std::lock_guard lock(progress_mutex_);
                            progress_.sectors_scanned = metadata_end_sector;
                        }
                        if (on_progress_) on_progress_(progress_);
                    }
                }
                break;

            case FileSystemType::FAT12:
            case FileSystemType::FAT16:
            case FileSystemType::FAT32:
                {
                    fat::FatParser parser;
                    if (parser.parse_boot_sector(reader, start_sector)) {
                        OutputDebugStringW(L"[ScanManager] Phase 1: Parsing FAT root directory...\n");
                        parser.enumerate_root_dir(reader, on_file, true);
                        metadata_end_sector = parser.data_start_sector();
                        {
                            std::lock_guard lock(progress_mutex_);
                            progress_.sectors_scanned = metadata_end_sector;
                        }
                        if (on_progress_) on_progress_(progress_);
                    }
                }
                break;

            case FileSystemType::ExFAT:
            case FileSystemType::Unknown:
                OutputDebugStringW(L"[ScanManager] Unknown/exFAT file system - proceeding to RAW scan\n");
                metadata_end_sector = start_sector;
                break;
            }

            // Phase 2: RAW signature scan on remaining sectors (free space approximation)
            // For simplicity, we scan from metadata_end_sector to end_sector
            // A more sophisticated implementation would track allocated clusters
            if (metadata_end_sector < end_sector && !stop_requested_.load()) {
                OutputDebugStringW(L"[ScanManager] Phase 2: RAW scan on remaining sectors...\n");
                SignatureScanner scanner;
                SignatureScanner::ScanConfig scan_config{};
                scan_config.start_sector = metadata_end_sector;
                scan_config.end_sector = end_sector;
                scan_config.scan_images = config.scan_images;
                scan_config.scan_videos = config.scan_videos;
                scanner.scan(reader, scan_config, on_file, on_scan_progress);
            }
        }
        break;

    case ScanMode::Full:
        // Full: Entire disk RAW sector-by-sector signature scanning
        OutputDebugStringW(L"[ScanManager] Full scan mode - entire disk RAW scan\n");
        {
            SignatureScanner scanner;
            SignatureScanner::ScanConfig scan_config{};
            scan_config.start_sector = start_sector;
            scan_config.end_sector = end_sector;
            scan_config.scan_images = config.scan_images;
            scan_config.scan_videos = config.scan_videos;
            scanner.scan(reader, scan_config, on_file, on_scan_progress);
        }
        break;
    }

    flush_cache(current_session_id_);
    progress_.is_complete = true;
    cache_db_.save_progress(current_session_id_, progress_);
    cache_db_.close();
    scanning_ = false;

    OutputDebugStringW(L"[ScanManager] Scan complete\n");
    // Final progress update to signal completion
    if (on_progress_) on_progress_(progress_);
}

void ScanManager::flush_cache(const std::string& session_id) {
    if (pending_files_.empty()) return;
    cache_db_.insert_files_bulk(session_id, pending_files_);
    pending_files_.clear();
}

} // namespace disk_recover
