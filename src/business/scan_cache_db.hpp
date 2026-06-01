#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_set>

struct sqlite3;

namespace disk_recover {

struct RecoveryProgress {
    std::string session_id;
    int last_file_index = 0;          // Last recovered file offset
    std::string save_dirs_json;       // JSON array of save directories
    uint64_t files_recovered = 0;
    uint64_t bytes_recovered = 0;
    bool is_paused = false;
    std::string ext_counters_json;    // JSON: {"D:\\Rec\\":{"jpg":2,"png":1}}
};

class ScanCacheDB {
public:
    ~ScanCacheDB();
    bool open(const std::wstring& db_path);
    void close();

    bool create_session(const std::string& session_id);
    bool insert_file(const std::string& session_id, const RecoverableFile& file);
    bool insert_files_bulk(const std::string& session_id,
                           const std::vector<RecoverableFile>& files);

    uint32_t query_file_count(const std::string& session_id);
    std::vector<RecoverableFile> query_files(const std::string& session_id,
                                              uint32_t limit, uint32_t offset);

    bool save_progress(const std::string& session_id, const ScanProgress& progress);
    bool load_progress(const std::string& session_id, ScanProgress& progress);

    bool save_recovery_progress(const RecoveryProgress& progress);
    bool load_recovery_progress(const std::string& session_id, RecoveryProgress& progress);
    bool clear_recovery_progress(const std::string& session_id);

    bool save_bad_sectors(const std::string& session_id,
                          const std::vector<uint64_t>& sectors);
    std::vector<uint64_t> load_bad_sectors(const std::string& session_id);

    std::unordered_set<uint64_t> load_file_keys(const std::string& session_id);

private:
    sqlite3* db_ = nullptr;
    bool ensure_tables();
};

} // namespace disk_recover
