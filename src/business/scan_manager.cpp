#include "scan_manager.hpp"
#include "disk_handle.hpp"
#include "disk_info.hpp"
#include "signature_scanner.hpp"
#include "../filesystem/ntfs/mft_parser.hpp"
#include "../filesystem/fat/fat_parser.hpp"
#include "../filesystem/exfat/exfat_parser.hpp"
#include "../common/logger.hpp"
#include <thread>
#include <cstring>

namespace disk_recover {

static FileSystemType detect_filesystem(SectorReader& reader, uint64_t partition_start) {
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) {
        return FileSystemType::Unknown;
    }

    const uint8_t* data = buf.data();

    uint16_t signature = *reinterpret_cast<const uint16_t*>(data + 510);
    if (signature != 0xAA55) {
        return FileSystemType::Unknown;
    }

    const char exfat_id[] = "EXFAT   ";
    if (std::memcmp(data + 3, exfat_id, 8) == 0) {
        return FileSystemType::ExFAT;
    }

    const char ntfs_id[] = "NTFS    ";
    if (std::memcmp(data + 3, ntfs_id, 8) == 0) {
        return FileSystemType::NTFS;
    }

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

bool ScanManager::start_scan(const Config& config) {
    if (scanning_.load()) return false;

    // Join any previous scan thread before starting a new one
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }

    scanning_ = true;
    paused_ = false;
    stop_requested_ = false;
    // Reset atomic progress values
    progress_.sectors_scanned.store(0, std::memory_order_relaxed);
    progress_.total_sectors.store(0, std::memory_order_relaxed);
    progress_.files_found.store(0, std::memory_order_relaxed);
    progress_.bad_sectors_hit.store(0, std::memory_order_relaxed);
    progress_.is_paused.store(false, std::memory_order_relaxed);
    progress_.is_complete.store(false, std::memory_order_relaxed);
    progress_.scan_phase.store(0, std::memory_order_relaxed);
    progress_.raw_resume_sector.store(0, std::memory_order_relaxed);
    current_session_id_ = config.session_id;

    if (!cache_db_.open(config.db_path)) {
        scanning_ = false;
        return false;
    }
    cache_db_.create_session(config.session_id);

    scan_thread_ = std::thread(&ScanManager::scan_thread_func, this, config);
    return true;
}

bool ScanManager::resume_scan(const Config& config) {
    if (scanning_.load()) return false;

    // Join any previous scan thread before starting a new one
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }

    if (!cache_db_.open(config.db_path)) {
        return false;
    }

    ScanProgress saved_progress;
    if (!cache_db_.load_progress(config.session_id, saved_progress)) {
        cache_db_.close();
        return false;
    }

    auto snap = saved_progress.snapshot();
    if (snap.is_complete) {
        cache_db_.close();
        return false;
    }

    scanning_ = true;
    paused_ = false;
    stop_requested_ = false;
    progress_.load_from(snap);  // Load atomics from snapshot
    current_session_id_ = config.session_id;

    // scan_thread_func will read saved_progress internally via cache_db_
    // and use scan_phase + raw_resume_sector for phase-based resume
    scan_thread_ = std::thread(&ScanManager::scan_thread_func, this, config);
    return true;
}

void ScanManager::pause_scan() { paused_ = true; }
void ScanManager::resume_from_pause() { paused_ = false; }

// stop_scan() sets the stop flag and waits for the scan thread to finish.
// This ensures the ScanManager is in a clean state before a new scan can start.
void ScanManager::stop_scan() {
    stop_requested_ = true;
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
}

std::vector<RecoverableFile> ScanManager::take_found_files() {
    std::lock_guard lock(files_mutex_);
    std::vector<RecoverableFile> result;
    result.swap(ui_files_);
    return result;
}

void ScanManager::scan_thread_func(Config config) {
    DiskHandle handle;
    if (!handle.open(config.device_path)) {
        LOG_FMT(L"[ScanManager] Failed to open disk: %s", config.device_path.c_str());
        scanning_ = false;
        cache_db_.close();
        if (on_progress_) {
            ScanProgress p{};
            p.is_complete = true;
            on_progress_(p);
        }
        return;
    }

    LOG_FMT(L"[ScanManager] Opened disk: %s", config.device_path.c_str());

    DiskGeometry geo{};
    DiskInfoQuery::QueryDiskGeometry(handle, geo);
    LOG_FMT(L"[ScanManager] Disk geometry: sector_size=%u, total_sectors=%llu", geo.sector_size, geo.total_sectors);

    SectorReader reader(handle, geo.sector_size);
    BadSectorManager bad_mgr;
    reader.set_bad_sector_manager(&bad_mgr);
    reader.set_bad_sector_policy(config.bad_sector_policy);

    uint64_t start_sector = config.start_sector;
    uint64_t end_sector = (config.end_sector > 0) ? config.end_sector : geo.total_sectors;

    // Load saved progress for phase-based resume
    ScanProgress saved_progress{};
    bool has_saved = cache_db_.load_progress(config.session_id, saved_progress);

    // Load existing file keys for dedup
    auto existing_keys = cache_db_.load_file_keys(config.session_id);

    // If resuming and metadata phase already done, skip to RAW
    auto saved_snap = saved_progress.snapshot();
    bool skip_metadata = has_saved && saved_snap.scan_phase >= 1;

    if (skip_metadata) {
        // Restore progress counters from saved state
        progress_.sectors_scanned.store(saved_snap.sectors_scanned, std::memory_order_relaxed);
        progress_.files_found.store(saved_snap.files_found, std::memory_order_relaxed);
        progress_.bad_sectors_hit.store(saved_snap.bad_sectors_hit, std::memory_order_relaxed);
    }

    progress_.total_sectors.store(end_sector - start_sector, std::memory_order_relaxed);

    LOG_FMT(L"[ScanManager] Scan range: sector %llu to %llu (%llu sectors)",
             start_sector, end_sector, progress_.total_sectors.load());

    auto on_file = [this, &existing_keys](RecoverableFile&& file) {
        if (stop_requested_.load()) return;

        // Dedup: skip files already found in previous scan
        uint64_t key = file.mft_id.has_value() ? file.mft_id.value() :
                       (file.fragments.empty() ? 0 : file.fragments[0].start_sector);
        if (key && existing_keys.count(key)) return;
        existing_keys.insert(key);

        bool need_flush = false;
        bool need_progress = false;
        {
            std::lock_guard lock(files_mutex_);
            ui_files_.push_back(file);  // Copy for UI
            pending_files_.push_back(std::move(file));
            if (pending_files_.size() >= FLUSH_THRESHOLD) {
                need_flush = true;
            }
            need_progress = (ui_files_.size() % 20 == 0);
        }
        progress_.files_found.fetch_add(1, std::memory_order_relaxed);
        if (need_flush) {
            flush_cache(current_session_id_);
        }
        if (need_progress && on_progress_) {
            on_progress_(progress_);
        }
    };

    auto on_scan_progress = [this](const ScanProgress& p) {
        if (stop_requested_.load()) return;
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        auto snap = p.snapshot();
        progress_.sectors_scanned.store(snap.sectors_scanned, std::memory_order_relaxed);
        progress_.bad_sectors_hit.store(snap.bad_sectors_hit, std::memory_order_relaxed);
        progress_.raw_resume_sector.store(snap.sectors_scanned, std::memory_order_relaxed);
        cache_db_.save_progress(current_session_id_, progress_);
        if (on_progress_) on_progress_(progress_);
    };

    LOG_FMT(L"[ScanManager] Starting scan mode=%d", static_cast<int>(config.mode));

    if (!stop_requested_.load()) {
        switch (config.mode) {
        case ScanMode::Quick:
            {
                FileSystemType fs_type = detect_filesystem(reader, start_sector);
                LOG_FMT(L"[ScanManager] Detected filesystem: %d", static_cast<int>(fs_type));

                switch (fs_type) {
                case FileSystemType::NTFS:
                    {
                        ntfs::MftParser parser;
                        if (parser.parse_boot_sector(reader, start_sector)) {
                            LOG_MSG(L"[ScanManager] Parsing NTFS MFT...");
                            parser.enumerate_mft(reader, on_file, true,
                                [this]() { return stop_requested_.load(); });
                            progress_.sectors_scanned.store(
                                parser.mft_start_sector() + (geo.total_sectors / parser.mft_record_size()),
                                std::memory_order_relaxed);
                            LOG_FMT(L"[ScanManager] NTFS scan done, files_found=%llu",
                                     progress_.files_found.load());
                        } else {
                            LOG_MSG(L"[ScanManager] Failed to parse NTFS boot sector");
                        }
                    }
                    break;

                case FileSystemType::FAT12:
                case FileSystemType::FAT16:
                case FileSystemType::FAT32:
                    {
                        fat::FatParser parser;
                        if (parser.parse_boot_sector(reader, start_sector)) {
                            LOG_MSG(L"[ScanManager] Parsing FAT root directory...");
                            parser.enumerate_root_dir(reader, on_file, true,
                                [this]() { return stop_requested_.load(); });
                            progress_.sectors_scanned.store(parser.data_start_sector(),
                                                             std::memory_order_relaxed);
                            LOG_FMT(L"[ScanManager] FAT scan done, files_found=%llu",
                                     progress_.files_found.load());
                        } else {
                            LOG_MSG(L"[ScanManager] Failed to parse FAT boot sector");
                        }
                    }
                    break;

                case FileSystemType::ExFAT:
                    LOG_MSG(L"[ScanManager] exFAT detected - using RAW scan fallback");
                    {
                        SignatureScanner scanner;
                        SignatureScanner::ScanConfig scan_config{};
                        scan_config.start_sector = start_sector;
                        scan_config.end_sector = end_sector;
                        scan_config.scan_images = config.scan_images;
                        scan_config.scan_videos = config.scan_videos;
                        scan_config.scan_audio = config.scan_audio;
                        scan_config.scan_documents = config.scan_documents;
                        scan_config.scan_archives = config.scan_archives;
                        scan_config.should_stop = [this]() { return stop_requested_.load(); };
                        scanner.scan(reader, scan_config, on_file, on_scan_progress);
                    }
                    break;

                case FileSystemType::Unknown:
                    LOG_MSG(L"[ScanManager] Unknown file system - performing RAW scan");
                    {
                        SignatureScanner scanner;
                        SignatureScanner::ScanConfig scan_config{};
                        scan_config.start_sector = start_sector;
                        scan_config.end_sector = end_sector;
                        scan_config.scan_images = config.scan_images;
                        scan_config.scan_videos = config.scan_videos;
                        scan_config.scan_audio = config.scan_audio;
                        scan_config.scan_documents = config.scan_documents;
                        scan_config.scan_archives = config.scan_archives;
                        scan_config.should_stop = [this]() { return stop_requested_.load(); };
                        scanner.scan(reader, scan_config, on_file, on_scan_progress);
                    }
                    break;
                }
            }
            break;

        case ScanMode::Deep:
            {
                FileSystemType fs_type = detect_filesystem(reader, start_sector);
                LOG_FMT(L"[ScanManager] Detected filesystem: %d", static_cast<int>(fs_type));
                uint64_t metadata_end_sector = 0;

                // Phase 1: metadata scan (skip if already done from resume)
                if (!skip_metadata) {
                    switch (fs_type) {
                    case FileSystemType::NTFS:
                        {
                            ntfs::MftParser parser;
                            if (parser.parse_boot_sector(reader, start_sector)) {
                                LOG_MSG(L"[ScanManager] Phase 1: Parsing NTFS MFT...");
                                parser.enumerate_mft(reader, on_file, true,
                                    [this]() { return stop_requested_.load(); });
                                metadata_end_sector = parser.mft_start_sector() +
                                    (geo.total_sectors / parser.mft_record_size());
                                progress_.sectors_scanned.store(metadata_end_sector,
                                                                std::memory_order_relaxed);
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
                                LOG_MSG(L"[ScanManager] Phase 1: Parsing FAT root directory...");
                                parser.enumerate_root_dir(reader, on_file, true,
                                    [this]() { return stop_requested_.load(); });
                                metadata_end_sector = parser.data_start_sector();
                                progress_.sectors_scanned.store(metadata_end_sector,
                                                                std::memory_order_relaxed);
                                if (on_progress_) on_progress_(progress_);
                            }
                        }
                        break;

                    case FileSystemType::ExFAT:
                    case FileSystemType::Unknown:
                        metadata_end_sector = start_sector;
                        break;
                    }

                    // Mark phase 1 complete and save
                    progress_.scan_phase.store(1, std::memory_order_relaxed);
                    cache_db_.save_progress(current_session_id_, progress_);
                }

                // Phase 2: RAW scan
                progress_.scan_phase.store(2, std::memory_order_relaxed);

                if (metadata_end_sector < end_sector && !stop_requested_.load()) {
                    LOG_FMT(L"[ScanManager] Phase 2: RAW scan sectors %llu to %llu",
                             metadata_end_sector, end_sector);
                    SignatureScanner scanner;
                    SignatureScanner::ScanConfig scan_config{};
                    scan_config.start_sector = metadata_end_sector;
                    scan_config.end_sector = end_sector;
                    scan_config.scan_images = config.scan_images;
                    scan_config.scan_videos = config.scan_videos;
                    scan_config.scan_audio = config.scan_audio;
                    scan_config.scan_documents = config.scan_documents;
                    scan_config.scan_archives = config.scan_archives;
                    scan_config.should_stop = [this]() { return stop_requested_.load(); };

                    // Set resume point if resuming from phase 2
                    if (has_saved && saved_snap.scan_phase >= 2 && saved_snap.raw_resume_sector > metadata_end_sector) {
                        scan_config.resume_from_sector = saved_snap.raw_resume_sector;
                    }

                    scanner.scan(reader, scan_config, on_file, on_scan_progress);
                }
            }
            break;

        case ScanMode::Full:
            {
                SignatureScanner scanner;
                SignatureScanner::ScanConfig scan_config{};
                scan_config.start_sector = start_sector;
                scan_config.end_sector = end_sector;
                scan_config.scan_images = config.scan_images;
                scan_config.scan_videos = config.scan_videos;
                scan_config.scan_audio = config.scan_audio;
                scan_config.scan_documents = config.scan_documents;
                scan_config.scan_archives = config.scan_archives;
                scan_config.should_stop = [this]() { return stop_requested_.load(); };

                // Set resume point if resuming
                if (has_saved && saved_snap.raw_resume_sector > start_sector) {
                    scan_config.resume_from_sector = saved_snap.raw_resume_sector;
                }

                scanner.scan(reader, scan_config, on_file, on_scan_progress);
            }
            break;
        }
    }

    // All cleanup happens HERE in the scan thread, not in stop_scan()
    flush_cache(current_session_id_);

    progress_.is_complete.store(true, std::memory_order_relaxed);

    cache_db_.save_progress(current_session_id_, progress_);
    cache_db_.close();
    scanning_ = false;

    LOG_FMT(L"[ScanManager] Scan done: files_found=%llu, sectors=%llu, bad=%llu",
             progress_.files_found.load(), progress_.sectors_scanned.load(),
             progress_.bad_sectors_hit.load());

    // Final notification to UI (thread-safe via PostMessage)
    if (on_progress_) on_progress_(progress_);
}

void ScanManager::flush_cache(const std::string& session_id) {
    std::vector<RecoverableFile> to_write;
    {
        std::lock_guard lock(files_mutex_);
        if (pending_files_.empty()) return;
        to_write.swap(pending_files_);
    }
    cache_db_.insert_files_bulk(session_id, to_write);
}

} // namespace disk_recover