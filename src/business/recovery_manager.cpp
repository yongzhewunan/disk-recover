#define NOMINMAX
#include "recovery_manager.hpp"
#include "disk-io/disk_handle.hpp"
#include "disk-io/sector_reader.hpp"
#include "disk-io/aligned_buffer.hpp"
#include "disk-io/disk_info.hpp"
#include "common/logger.hpp"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <chrono>

namespace disk_recover {

// ---------------------------------------------------------------------------
// Helpers for manual JSON building (no external JSON library needed)
// ---------------------------------------------------------------------------

static std::string build_save_dirs_json(const std::vector<SaveDirEntry>& dirs) {
    std::string json = "[";
    for (size_t i = 0; i < dirs.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"";
        for (wchar_t wc : dirs[i].path) {
            if (wc == L'\\') {
                json += "\\\\";
            } else if (wc == L'"') {
                json += "\\\"";
            } else if (wc >= 32 && wc < 127) {
                json += static_cast<char>(wc);
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(wc));
                json += buf;
            }
        }
        json += "\"";
    }
    json += "]";
    return json;
}

static std::string build_ext_counters_json(const std::unordered_map<std::wstring, uint32_t>& counters,
                                           const std::unordered_map<std::wstring, uint32_t>& subfolders) {
    std::string json = "{";
    bool first = true;
    for (const auto& [key, count] : counters) {
        if (!first) json += ",";
        first = false;

        json += "\"";
        for (wchar_t wc : key) {
            if (wc == L'\\') {
                json += "\\\\";
            } else if (wc == L'"') {
                json += "\\\"";
            } else if (wc >= 32 && wc < 127) {
                json += static_cast<char>(wc);
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(wc));
                json += buf;
            }
        }
        json += "\":";

        // Value is an object: {"count":N,"sub":M}
        json += "{\"count\":";
        json += std::to_string(count);
        json += ",\"sub\":";
        auto it = subfolders.find(key);
        json += std::to_string(it != subfolders.end() ? it->second : 1);
        json += "}";
    }
    json += "}";
    return json;
}

// Simple JSON parser for the ext_counters format:
// {"key1":{"count":N,"sub":M}, "key2":{"count":N,"sub":M}}
static void parse_ext_counters_json(const std::string& json,
                                    std::unordered_map<std::wstring, uint32_t>& counters,
                                    std::unordered_map<std::wstring, uint32_t>& subfolders) {
    counters.clear();
    subfolders.clear();
    if (json.empty() || json[0] != '{') return;

    size_t pos = 1; // skip '{'
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size() || json[pos] == '}') break;

        // Parse key (quoted string)
        if (json[pos] != '"') break;
        pos++;
        std::wstring key;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                if (json[pos] == '\\') key += L'\\';
                else if (json[pos] == '"') key += L'"';
                else if (json[pos] == 'u' && pos + 4 < json.size()) {
                    unsigned val = 0;
                    for (int i = 1; i <= 4; ++i) {
                        char c = json[pos + i];
                        val <<= 4;
                        if (c >= '0' && c <= '9') val |= (c - '0');
                        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
                    }
                    key += static_cast<wchar_t>(val);
                    pos += 4;
                }
            } else {
                key += static_cast<wchar_t>(static_cast<unsigned char>(json[pos]));
            }
            pos++;
        }
        if (pos >= json.size()) break;
        pos++; // skip closing quote

        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':')) pos++;

        // Parse value object {"count":N,"sub":M}
        if (pos >= json.size() || json[pos] != '{') break;
        pos++;

        uint32_t count_val = 0;
        uint32_t sub_val = 1;

        while (pos < json.size() && json[pos] != '}') {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ',')) pos++;
            if (pos >= json.size() || json[pos] == '}') break;

            if (json[pos] != '"') break;
            pos++;
            std::string inner_key;
            while (pos < json.size() && json[pos] != '"') {
                inner_key += json[pos];
                pos++;
            }
            if (pos >= json.size()) break;
            pos++;

            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':')) pos++;

            uint32_t num = 0;
            while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
                num = num * 10 + static_cast<uint32_t>(json[pos] - '0');
                pos++;
            }

            if (inner_key == "count") count_val = num;
            else if (inner_key == "sub") sub_val = num;
        }
        if (pos < json.size() && json[pos] == '}') pos++;

        counters[key] = count_val;
        subfolders[key] = sub_val;

        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ',')) pos++;
    }
}

static std::wstring extract_extension(const std::wstring& filename) {
    size_t dot = filename.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0) return L"";

    std::wstring ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

// ---------------------------------------------------------------------------
// RecoveryManager public API
// ---------------------------------------------------------------------------

RecoveryManager::~RecoveryManager() {
    stop_recovery();
}

bool RecoveryManager::start_recovery(const RecoveryConfig& config) {
    if (recovering_.load()) return false;

    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }

    stop_requested_ = false;
    paused_ = false;
    recovering_ = true;
    current_session_id_ = config.session_id;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = {};
    }
    ext_counters_.clear();
    ext_subfolder_.clear();
    last_file_index_ = 0;

    recovery_thread_ = std::thread(&RecoveryManager::recovery_thread_func, this, config);
    return true;
}

void RecoveryManager::stop_recovery() {
    stop_requested_ = true;
    if (paused_.load()) {
        paused_ = false;
    }
    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }
    recovering_ = false;
}

void RecoveryManager::pause_recovery() {
    if (!recovering_.load()) return;
    paused_ = true;
    save_recovery_progress_state();
}

void RecoveryManager::resume_recovery() {
    if (!recovering_.load()) return;
    paused_ = false;
    writer_.refresh_space_info();
}

RecoveryManager::RecoveryStats RecoveryManager::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// ---------------------------------------------------------------------------
// Recovery thread
// ---------------------------------------------------------------------------

void RecoveryManager::recovery_thread_func(RecoveryConfig config) {
    LOG_MSG(L"[RecoveryManager] Recovery thread started");

    // 1. Open cache DB
    if (!cache_db_.open(config.db_path)) {
        LOG_FMT(L"[RecoveryManager] Failed to open cache DB: %s", config.db_path.c_str());
        if (config.hwnd) {
            PostMessageW(config.hwnd, WM_RECOVERY_COMPLETE, 1, 0);
        }
        recovering_ = false;
        return;
    }

    // 2. Load recovery progress if exists (for resume)
    bool is_resuming = load_recovery_progress_state(config.session_id);
    if (is_resuming) {
        LOG_FMT(L"[RecoveryManager] Resuming from file index %d", last_file_index_);
    }

    // 3. Query total file count
    uint32_t total_files = cache_db_.query_file_count(config.session_id);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_files = total_files;
    }

    LOG_FMT(L"[RecoveryManager] Total files to recover: %u", total_files);

    if (total_files == 0) {
        cache_db_.close();
        if (config.hwnd) {
            PostMessageW(config.hwnd, WM_RECOVERY_COMPLETE, 0, 0);
        }
        recovering_ = false;
        return;
    }

    // 4. Create MultiTargetWriter with all save_dirs
    for (const auto& dir : config.save_dirs) {
        writer_.add_target(dir.path);
    }
    writer_.set_auto_switch(false);

    // 5. Open source disk
    DiskHandle disk_handle;
    if (!disk_handle.open(config.source_disk_path)) {
        LOG_FMT(L"[RecoveryManager] Failed to open source disk: %s", config.source_disk_path.c_str());
        cache_db_.close();
        if (config.hwnd) {
            PostMessageW(config.hwnd, WM_RECOVERY_COMPLETE, 1, 0);
        }
        recovering_ = false;
        return;
    }

    // Query disk geometry for sector size if not specified
    if (config.sector_size == 0) {
        DiskGeometry geo{};
        DiskInfoQuery::QueryDiskGeometry(disk_handle, geo);
        config.sector_size = geo.sector_size ? geo.sector_size : 512;
    }

    SectorReader sector_reader(disk_handle, config.sector_size);

    // 6. Loop: query files in pages of 100
    const uint32_t PAGE_SIZE = 100;
    uint32_t offset = static_cast<uint32_t>(last_file_index_);

    auto last_progress_time = std::chrono::steady_clock::now();
    uint32_t files_since_progress = 0;

    while (offset < total_files && !stop_requested_.load()) {
        auto files = cache_db_.query_files(config.session_id, PAGE_SIZE, offset);

        for (size_t i = 0; i < files.size(); ++i) {
            // a. Check stop
            if (stop_requested_.load()) break;

            // b. While paused, sleep
            while (paused_.load() && !stop_requested_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stop_requested_.load()) break;

            // c. Check space and switch target if needed
            if (!check_space_and_switch(config)) {
                // Paused due to no space on any target
                while (paused_.load() && !stop_requested_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (stop_requested_.load()) break;

                // Re-check space after resume
                if (!check_space_and_switch(config)) {
                    LOG_FMT(L"[RecoveryManager] No space for file: %s, skipping",
                             files[i].file_name.c_str());
                    {
                        std::lock_guard<std::mutex> lock(stats_mutex_);
                        stats_.files_failed++;
                    }
                    last_file_index_++;
                    continue;
                }
            }

            // d. Recover single file using the shared SectorReader
            bool success = recover_single_file(files[i], sector_reader);

            // e. Update stats
            if (success) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.files_recovered++;
                stats_.bytes_recovered += files[i].file_size;
            } else {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.files_failed++;
            }
            last_file_index_++;
            files_since_progress++;

            // f. PostMessage WM_RECOVERY_PROGRESS every 10 files or every 2 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_progress_time).count();

            if (config.hwnd && (files_since_progress >= 10 || elapsed >= 2000)) {
                RecoveryProgress progress;
                progress.session_id = config.session_id;
                progress.last_file_index = last_file_index_;
                progress.files_recovered = stats_.files_recovered;
                progress.bytes_recovered = stats_.bytes_recovered;
                progress.is_paused = false;

                RecoveryProgress* p = new RecoveryProgress(progress);
                if (!PostMessageW(config.hwnd, WM_RECOVERY_PROGRESS, 0,
                                  reinterpret_cast<LPARAM>(p))) {
                    delete p;
                }

                files_since_progress = 0;
                last_progress_time = now;
            }
        }

        if (stop_requested_.load()) break;

        offset += PAGE_SIZE;
    }

    // 7. Save final progress state
    save_recovery_progress_state();

    // 8. Cleanup
    cache_db_.close();
    disk_handle.close();

    LOG_FMT(L"[RecoveryManager] Recovery done: recovered=%llu, failed=%llu, stopped=%d",
             stats_.files_recovered, stats_.files_failed, stop_requested_.load());

    // 9. Notify completion
    if (config.hwnd) {
        PostMessageW(config.hwnd, WM_RECOVERY_COMPLETE, 0, 0);
    }

    recovering_ = false;
}

// ---------------------------------------------------------------------------
// recover_single_file
// ---------------------------------------------------------------------------

bool RecoveryManager::recover_single_file(const RecoverableFile& file,
                                           SectorReader& reader) {
    if (file.fragments.empty()) {
        LOG_FMT(L"[RecoveryManager] File has no fragments: %s", file.file_name.c_str());
        return false;
    }

    // 1. Get file name and extension
    std::wstring extension = extract_extension(file.file_name);

    // 2. Build output path with extension grouping
    std::wstring base_dir = writer_.current_target();
    if (base_dir.empty()) {
        LOG_MSG(L"[RecoveryManager] No current target directory");
        return false;
    }

    std::wstring output_path = build_output_path(base_dir, file.file_name, extension);

    // 3. Create the output file
    HANDLE hOut = CreateFileW(output_path.c_str(), GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
        LOG_FMT(L"[RecoveryManager] Failed to create output file: %s (error=%u)",
                 output_path.c_str(), GetLastError());
        return false;
    }

    // 4. Read file data from disk sectors and write to output
    AlignedBuffer buf(reader.sector_size() * 64, reader.sector_size());

    bool success = true;
    for (const auto& frag : file.fragments) {
        uint64_t sector = frag.start_sector;
        uint64_t remaining = frag.sector_count;

        while (remaining > 0) {
            if (stop_requested_.load()) {
                success = false;
                break;
            }

            uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(remaining, 64));
            if (!reader.read_sectors(sector, count, buf)) {
                LOG_FMT(L"[RecoveryManager] Failed to read sector %llu (count=%u) for file: %s",
                         sector, count, file.file_name.c_str());
                success = false;
                break;
            }

            DWORD written = 0;
            if (!WriteFile(hOut, buf.data(), static_cast<DWORD>(count * reader.sector_size()),
                           &written, nullptr)) {
                LOG_FMT(L"[RecoveryManager] Failed to write to output file: %s (error=%u)",
                         output_path.c_str(), GetLastError());
                success = false;
                break;
            }

            sector += count;
            remaining -= count;
        }

        if (!success) break;
    }

    CloseHandle(hOut);

    if (!success) {
        DeleteFileW(output_path.c_str());
    }

    return success;
}

// ---------------------------------------------------------------------------
// build_output_path - extension grouping with subfolder limit of 1000 files
// ---------------------------------------------------------------------------

std::wstring RecoveryManager::build_output_path(const std::wstring& base_dir,
                                                  const std::wstring& filename,
                                                  const std::wstring& extension) {
    std::wstring ext = extension;
    if (ext.empty()) {
        ext = L"other";
    }

    // Counter key = base_dir + L"/" + ext
    std::wstring key = base_dir + L"/" + ext;

    uint32_t count = ext_counters_[key];
    uint32_t sub_idx = ext_subfolder_[key];

    if (sub_idx == 0) sub_idx = 1;  // First subfolder index

    // Determine subfolder name
    std::wstring subfolder;
    if (count < 1000) {
        subfolder = ext;
    } else {
        // Move to next subfolder
        sub_idx++;
        ext_subfolder_[key] = sub_idx;
        ext_counters_[key] = 0;
        count = 0;

        if (sub_idx == 2) {
            subfolder = ext + L"(2)";
        } else {
            subfolder = ext + L"(" + std::to_wstring(sub_idx) + L")";
        }
    }

    // Create the subdirectory if it doesn't exist
    std::wstring sub_dir = base_dir + L"\\" + subfolder;
    CreateDirectoryW(sub_dir.c_str(), nullptr);

    // Increment counter and ensure subfolder index is set
    ext_counters_[key] = count + 1;
    if (ext_subfolder_[key] == 0) {
        ext_subfolder_[key] = 1;
    }

    return sub_dir + L"\\" + filename;
}

// ---------------------------------------------------------------------------
// check_space_and_switch
// ---------------------------------------------------------------------------

bool RecoveryManager::check_space_and_switch(const RecoveryConfig& config) {
    writer_.refresh_space_info();

    if (writer_.has_space(config.min_free_bytes)) {
        return true;  // Enough space on current target
    }

    // Try to switch to next target
    if (writer_.switch_to_next_target()) {
        LOG_FMT(L"[RecoveryManager] Switched to target: %s", writer_.current_target().c_str());
        return true;
    }

    // No space on any target - pause
    LOG_MSG(L"[RecoveryManager] No space on any target, pausing recovery");
    paused_ = true;
    save_recovery_progress_state();

    if (config.hwnd) {
        PostMessageW(config.hwnd, WM_RECOVERY_PAUSED, 0, 0);
    }

    return false;
}

// ---------------------------------------------------------------------------
// save / load recovery progress state
// ---------------------------------------------------------------------------

void RecoveryManager::save_recovery_progress_state() {
    RecoveryProgress progress;
    progress.session_id = current_session_id_;
    progress.last_file_index = last_file_index_;
    progress.files_recovered = stats_.files_recovered;
    progress.bytes_recovered = stats_.bytes_recovered;
    progress.is_paused = paused_.load();

    // Build save_dirs JSON from current writer targets
    std::vector<SaveDirEntry> current_dirs;
    for (const auto& td : writer_.targets()) {
        SaveDirEntry entry;
        entry.path = td.path;
        entry.free_bytes = td.free_bytes;
        current_dirs.push_back(entry);
    }
    progress.save_dirs_json = build_save_dirs_json(current_dirs);

    // Build ext_counters JSON
    progress.ext_counters_json = build_ext_counters_json(ext_counters_, ext_subfolder_);

    if (!cache_db_.save_recovery_progress(progress)) {
        LOG_MSG(L"[RecoveryManager] Failed to save recovery progress");
    }
}

bool RecoveryManager::load_recovery_progress_state(const std::string& session_id) {
    RecoveryProgress progress;
    if (!cache_db_.load_recovery_progress(session_id, progress)) {
        return false;
    }

    last_file_index_ = progress.last_file_index;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.files_recovered = progress.files_recovered;
        stats_.bytes_recovered = progress.bytes_recovered;
    }

    // Parse ext_counters JSON to restore extension grouping state
    parse_ext_counters_json(progress.ext_counters_json, ext_counters_, ext_subfolder_);

    LOG_FMT(L"[RecoveryManager] Loaded recovery state: index=%d, recovered=%llu",
             last_file_index_, progress.files_recovered);

    return true;
}

} // namespace disk_recover
