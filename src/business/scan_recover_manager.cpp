#include "scan_recover_manager.hpp"
#include "disk_info.hpp"
#include "signature_scanner.hpp"
#include "../disk-io/buffered_reader.hpp"
#include "../common/logger.hpp"
#include <algorithm>
#include <filesystem>

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
    if (worker_.joinable()) {
        worker_.join();
    }
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
        return false;
    }

    LOG_FMT(L"[ScanRecover] Recovered: %s (%llu bytes)", output_path.c_str(), file_buffer.size());
    return true;
}

void ScanAndRecoverManager::worker_thread(Config config) {
    LOG_MSG(L"[ScanRecover] Starting scan-recover thread");

    DiskHandle handle;
    if (!handle.open(config.device_path)) {
        LOG_FMT(L"[ScanRecover] Failed to open disk: %s", config.device_path.c_str());
        running_ = false;
        Progress p{};
        p.is_complete = true;
        if (on_progress_) on_progress_(p);
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

    // Handle resume from previous position
    uint64_t resume_sector = config.resume_from_sector;
    if (resume_sector > start_sector && resume_sector < end_sector) {
        // Adjust start sector for resume, with overlap for safety
        start_sector = (resume_sector > 64) ? (resume_sector - 64) : start_sector;
        LOG_FMT(L"[ScanRecover] Resuming from sector %llu (adjusted start: %llu)",
                 resume_sector, start_sector);
    }

    {
        std::lock_guard lock(progress_mutex_);
        progress_.total_sectors = end_sector - config.start_sector;  // Total from original start
        if (resume_sector > config.start_sector) {
            // Set initial progress for resume
            progress_.sectors_scanned = resume_sector - config.start_sector;
            progress_.current_sector = resume_sector;
        }
    }

    // Create sector reader with optimizations (using 128MB buffer)
    BufferedSectorReader reader(handle, geo.sector_size, 16 * 1024 * 1024);
    reader.set_bad_sector_policy(config.bad_sector_policy);
    reader.set_skip_ahead_config(config.skip_config);
    reader.set_timeout_config(config.timeout_config);

    // Prepare output directory
    if (config.output_dirs.empty()) {
        LOG_MSG(L"[ScanRecover] No output directories specified");
        running_ = false;
        return;
    }

    // Create output directories
    for (const auto& dir : config.output_dirs) {
        std::filesystem::create_directories(dir);
        LOG_FMT(L"[ScanRecover] Output directory: %s", dir.c_str());
    }

    const std::wstring& primary_output = config.output_dirs[0];

    // Configure signature scanner
    SignatureScanner::ScanConfig scan_config{};
    scan_config.start_sector = start_sector;
    scan_config.end_sector = end_sector;
    scan_config.scan_images = config.scan_images;
    scan_config.scan_videos = config.scan_videos;
    scan_config.should_stop = [this]() { return stop_requested_.load(); };
    scan_config.resume_from_sector = resume_sector;  // Pass resume position

    SignatureScanner scanner;

    uint32_t total_skipped = 0;
    auto last_progress_time = std::chrono::steady_clock::now();

    auto on_file_found = [this, &reader, &primary_output, &config](RecoverableFile&& file) {
        // Wait if paused
        while (paused_.load() && !stop_requested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (stop_requested_.load()) return;

        // Immediately recover the file
        bool success = recover_file(file, reader, primary_output);

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
            // Use original config.start_sector for accurate position tracking
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

        if (on_progress_) on_progress_(progress_);
    };

    LOG_FMT(L"[ScanRecover] Starting RAW scan: sector %llu to %llu",
             start_sector, end_sector);

    scanner.scan(reader, scan_config, on_file_found, on_scan_progress);

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
    if (on_progress_) on_progress_(progress_);
}

} // namespace disk_recover