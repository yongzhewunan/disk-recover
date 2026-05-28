#include "scan_manager.hpp"
#include "disk_handle.hpp"
#include "disk_info.hpp"
#include "signature_scanner.hpp"
#include <thread>

namespace disk_recover {

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
        scanning_ = false;
        cache_db_.close();
        return;
    }

    DiskGeometry geo{};
    DiskInfoQuery::QueryDiskGeometry(handle, geo);

    SectorReader reader(handle, geo.sector_size);
    BadSectorManager bad_mgr;
    reader.set_bad_sector_manager(&bad_mgr);
    reader.set_bad_sector_policy(config.bad_sector_policy);

    SignatureScanner scanner;
    SignatureScanner::ScanConfig scan_config{};
    scan_config.start_sector = config.start_sector;
    scan_config.end_sector = (config.end_sector > 0) ? config.end_sector : geo.total_sectors;
    scan_config.scan_images = config.scan_images;
    scan_config.scan_videos = config.scan_videos;

    progress_.total_sectors = scan_config.end_sector - scan_config.start_sector;

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

    scanner.scan(reader, scan_config, on_file, on_scan_progress);

    flush_cache(current_session_id_);
    progress_.is_complete = true;
    cache_db_.save_progress(current_session_id_, progress_);
    cache_db_.close();
    scanning_ = false;
}

void ScanManager::flush_cache(const std::string& session_id) {
    if (pending_files_.empty()) return;
    cache_db_.insert_files_bulk(session_id, pending_files_);
    pending_files_.clear();
}

} // namespace disk_recover
