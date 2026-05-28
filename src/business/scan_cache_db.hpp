#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <cstdint>

struct sqlite3;

namespace disk_recover {

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

    bool save_bad_sectors(const std::string& session_id,
                          const std::vector<uint64_t>& sectors);
    std::vector<uint64_t> load_bad_sectors(const std::string& session_id);

private:
    sqlite3* db_ = nullptr;
    bool ensure_tables();
};

} // namespace disk_recover
