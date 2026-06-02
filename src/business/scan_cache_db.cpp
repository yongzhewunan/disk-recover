#include "business/scan_cache_db.hpp"
#include <sqlite3.h>
#include <windows.h>
#include <cstring>
#include <sstream>

namespace disk_recover {

namespace {

// Convert wstring to UTF-8 string for SQLite
std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                          static_cast<int>(wstr.length()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                       static_cast<int>(wstr.length()),
                       &result[0], size_needed, nullptr, nullptr);
    return result;
}

// Convert UTF-8 string to wstring
std::wstring utf8_to_wstring(const char* str, int len) {
    if (len == 0) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, len, nullptr, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, len, &result[0], size_needed);
    return result;
}

// Serialize fragments to BLOB (16 bytes each: 8 for start_sector + 8 for sector_count)
std::vector<uint8_t> serialize_fragments(const std::vector<DiskExtent>& fragments) {
    std::vector<uint8_t> blob;
    blob.reserve(fragments.size() * 16);
    for (const auto& frag : fragments) {
        uint8_t* start = reinterpret_cast<uint8_t*>(const_cast<uint64_t*>(&frag.start_sector));
        uint8_t* count = reinterpret_cast<uint8_t*>(const_cast<uint64_t*>(&frag.sector_count));
        blob.insert(blob.end(), start, start + 8);
        blob.insert(blob.end(), count, count + 8);
    }
    return blob;
}

// Deserialize fragments from BLOB
std::vector<DiskExtent> deserialize_fragments(const void* data, int size) {
    std::vector<DiskExtent> fragments;
    if (size == 0 || size % 16 != 0) return fragments;

    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t count = size / 16;
    fragments.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        DiskExtent ext;
        std::memcpy(&ext.start_sector, ptr + i * 16, 8);
        std::memcpy(&ext.sector_count, ptr + i * 16 + 8, 8);
        fragments.push_back(ext);
    }
    return fragments;
}

} // anonymous namespace

ScanCacheDB::~ScanCacheDB() {
    close();
}

bool ScanCacheDB::open(const std::wstring& db_path) {
    if (db_) close();

    std::string utf8_path = wstring_to_utf8(db_path);
    int rc = sqlite3_open(utf8_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Enable WAL journal mode for better concurrency
    char* errmsg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    if (rc != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Enable foreign keys
    rc = sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);

    return ensure_tables();
}

void ScanCacheDB::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool ScanCacheDB::ensure_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS scan_result (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            file_name TEXT NOT NULL,
            file_size INTEGER NOT NULL,
            file_type INTEGER NOT NULL,
            is_corrupted INTEGER NOT NULL,
            mft_id INTEGER,
            fragments BLOB,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_scan_result_session
            ON scan_result(session_id);

        CREATE TABLE IF NOT EXISTS scan_progress (
            session_id TEXT PRIMARY KEY,
            sectors_scanned INTEGER NOT NULL,
            total_sectors INTEGER NOT NULL,
            files_found INTEGER NOT NULL,
            bad_sectors_hit INTEGER NOT NULL,
            is_paused INTEGER NOT NULL,
            is_complete INTEGER NOT NULL,
            scan_phase INTEGER NOT NULL DEFAULT 0,
            raw_resume_sector INTEGER NOT NULL DEFAULT 0,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS bad_sectors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            sector_lba INTEGER NOT NULL,
            UNIQUE(session_id, sector_lba)
        );

        CREATE INDEX IF NOT EXISTS idx_bad_sectors_session
            ON bad_sectors(session_id);

        CREATE TABLE IF NOT EXISTS recovery_progress (
            session_id TEXT PRIMARY KEY,
            last_file_index INTEGER NOT NULL,
            save_dirs TEXT NOT NULL,
            files_recovered INTEGER NOT NULL,
            bytes_recovered INTEGER NOT NULL,
            is_paused INTEGER NOT NULL DEFAULT 0,
            ext_counters TEXT NOT NULL DEFAULT '{}'
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
    }
    if (rc != SQLITE_OK) return false;

    // Migrate: add scan_phase and raw_resume_sector columns if missing
    sqlite3_exec(db_, "ALTER TABLE scan_progress ADD COLUMN scan_phase INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE scan_progress ADD COLUMN raw_resume_sector INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

    return true;
}

bool ScanCacheDB::create_session(const std::string& session_id) {
    // Insert or replace progress record to establish session
    const char* sql = R"(
        INSERT OR REPLACE INTO scan_progress
            (session_id, sectors_scanned, total_sectors, files_found,
             bad_sectors_hit, is_paused, is_complete)
        VALUES (?, 0, 0, 0, 0, 0, 0)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool ScanCacheDB::insert_file(const std::string& session_id, const RecoverableFile& file) {
    const char* sql = R"(
        INSERT INTO scan_result
            (session_id, file_name, file_size, file_type, is_corrupted, mft_id, fragments)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    std::string file_name_utf8 = wstring_to_utf8(file.file_name);
    auto fragments_blob = serialize_fragments(file.fragments);

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file_name_utf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(file.file_size));
    sqlite3_bind_int(stmt, 4, static_cast<int>(file.file_type));
    sqlite3_bind_int(stmt, 5, file.is_corrupted ? 1 : 0);

    if (file.mft_id.has_value()) {
        sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(*file.mft_id));
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    if (!fragments_blob.empty()) {
        sqlite3_bind_blob(stmt, 7, fragments_blob.data(),
                         static_cast<int>(fragments_blob.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool ScanCacheDB::insert_files_bulk(const std::string& session_id,
                                    const std::vector<RecoverableFile>& files) {
    if (files.empty()) return true;

    // Use transaction for bulk insert
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    if (rc != SQLITE_OK) return false;

    const char* sql = R"(
        INSERT INTO scan_result
            (session_id, file_name, file_size, file_type, is_corrupted, mft_id, fragments)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& file : files) {
        std::string file_name_utf8 = wstring_to_utf8(file.file_name);
        auto fragments_blob = serialize_fragments(file.fragments);

        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, file_name_utf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(file.file_size));
        sqlite3_bind_int(stmt, 4, static_cast<int>(file.file_type));
        sqlite3_bind_int(stmt, 5, file.is_corrupted ? 1 : 0);

        if (file.mft_id.has_value()) {
            sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(*file.mft_id));
        } else {
            sqlite3_bind_null(stmt, 6);
        }

        if (!fragments_blob.empty()) {
            sqlite3_bind_blob(stmt, 7, fragments_blob.data(),
                             static_cast<int>(fragments_blob.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 7);
        }

        rc = sqlite3_step(stmt);
        sqlite3_reset(stmt);

        if (rc != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (success) {
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) sqlite3_free(errmsg);
        success = (rc == SQLITE_OK);
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    return success;
}

uint32_t ScanCacheDB::query_file_count(const std::string& session_id) {
    const char* sql = "SELECT COUNT(*) FROM scan_result WHERE session_id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    uint32_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return count;
}

std::vector<RecoverableFile> ScanCacheDB::query_files(const std::string& session_id,
                                                       uint32_t limit, uint32_t offset) {
    std::vector<RecoverableFile> files;

    const char* sql = R"(
        SELECT file_name, file_size, file_type, is_corrupted, mft_id, fragments
        FROM scan_result
        WHERE session_id = ?
        ORDER BY id
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return files;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));
    sqlite3_bind_int(stmt, 3, static_cast<int>(offset));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RecoverableFile file;

        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int name_len = sqlite3_column_bytes(stmt, 0);
        file.file_name = utf8_to_wstring(name, name_len);

        file.file_size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        file.file_type = static_cast<FileType>(sqlite3_column_int(stmt, 2));
        file.is_corrupted = sqlite3_column_int(stmt, 3) != 0;

        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            file.mft_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        }

        const void* frag_data = sqlite3_column_blob(stmt, 5);
        int frag_size = sqlite3_column_bytes(stmt, 5);
        file.fragments = deserialize_fragments(frag_data, frag_size);

        files.push_back(std::move(file));
    }

    sqlite3_finalize(stmt);
    return files;
}

std::vector<RecoverableFile> ScanCacheDB::query_files_after_id(const std::string& session_id,
                                                                uint32_t limit, uint64_t last_id) {
    std::vector<RecoverableFile> files;

    // Use WHERE id > last_id for efficient pagination
    const char* sql = R"(
        SELECT id, file_name, file_size, file_type, is_corrupted, mft_id, fragments
        FROM scan_result
        WHERE session_id = ? AND id > ?
        ORDER BY id
        LIMIT ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return files;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(last_id));
    sqlite3_bind_int(stmt, 3, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RecoverableFile file;

        // Store the id for next pagination
        file.db_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));

        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int name_len = sqlite3_column_bytes(stmt, 1);
        file.file_name = utf8_to_wstring(name, name_len);

        file.file_size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        file.file_type = static_cast<FileType>(sqlite3_column_int(stmt, 3));
        file.is_corrupted = sqlite3_column_int(stmt, 4) != 0;

        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            file.mft_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        }

        const void* frag_data = sqlite3_column_blob(stmt, 6);
        int frag_size = sqlite3_column_bytes(stmt, 6);
        file.fragments = deserialize_fragments(frag_data, frag_size);

        files.push_back(std::move(file));
    }

    sqlite3_finalize(stmt);
    return files;
}

bool ScanCacheDB::save_progress(const std::string& session_id, const ScanProgress& progress) {
    const char* sql = R"(
        INSERT OR REPLACE INTO scan_progress
            (session_id, sectors_scanned, total_sectors, files_found,
             bad_sectors_hit, is_paused, is_complete, scan_phase, raw_resume_sector)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(progress.sectors_scanned));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(progress.total_sectors));
    sqlite3_bind_int(stmt, 4, static_cast<int>(progress.files_found));
    sqlite3_bind_int(stmt, 5, static_cast<int>(progress.bad_sectors_hit));
    sqlite3_bind_int(stmt, 6, progress.is_paused ? 1 : 0);
    sqlite3_bind_int(stmt, 7, progress.is_complete ? 1 : 0);
    sqlite3_bind_int(stmt, 8, static_cast<int>(progress.scan_phase));
    sqlite3_bind_int64(stmt, 9, static_cast<sqlite3_int64>(progress.raw_resume_sector));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool ScanCacheDB::load_progress(const std::string& session_id, ScanProgress& progress) {
    const char* sql = R"(
        SELECT sectors_scanned, total_sectors, files_found, bad_sectors_hit,
               is_paused, is_complete, scan_phase, raw_resume_sector
        FROM scan_progress
        WHERE session_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        progress.sectors_scanned = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        progress.total_sectors = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        progress.files_found = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
        progress.bad_sectors_hit = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));
        progress.is_paused = sqlite3_column_int(stmt, 4) != 0;
        progress.is_complete = sqlite3_column_int(stmt, 5) != 0;
        progress.scan_phase = static_cast<uint8_t>(sqlite3_column_int(stmt, 6));
        progress.raw_resume_sector = static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
    }

    sqlite3_finalize(stmt);
    return found;
}

bool ScanCacheDB::save_bad_sectors(const std::string& session_id,
                                   const std::vector<uint64_t>& sectors) {
    if (sectors.empty()) return true;

    // Use transaction for bulk insert
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    if (rc != SQLITE_OK) return false;

    const char* sql = R"(
        INSERT OR IGNORE INTO bad_sectors (session_id, sector_lba)
        VALUES (?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (uint64_t sector : sectors) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(sector));

        rc = sqlite3_step(stmt);
        sqlite3_reset(stmt);

        if (rc != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);

    if (success) {
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) sqlite3_free(errmsg);
        success = (rc == SQLITE_OK);
    } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    return success;
}

std::vector<uint64_t> ScanCacheDB::load_bad_sectors(const std::string& session_id) {
    std::vector<uint64_t> sectors;

    const char* sql = "SELECT sector_lba FROM bad_sectors WHERE session_id = ? ORDER BY sector_lba";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return sectors;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        sectors.push_back(static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return sectors;
}

std::unordered_set<uint64_t> ScanCacheDB::load_file_keys(const std::string& session_id) {
    std::unordered_set<uint64_t> keys;

    // Load mft_id values (for NTFS files)
    const char* sql_mft = "SELECT mft_id FROM scan_result WHERE session_id = ? AND mft_id IS NOT NULL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_mft, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            keys.insert(static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    // Load start_sector from fragments BLOB (for RAW files)
    const char* sql_frag = "SELECT fragments FROM scan_result WHERE session_id = ? AND mft_id IS NULL AND fragments IS NOT NULL";
    if (sqlite3_prepare_v2(db_, sql_frag, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* frag_data = sqlite3_column_blob(stmt, 0);
            int frag_size = sqlite3_column_bytes(stmt, 0);
            auto fragments = deserialize_fragments(frag_data, frag_size);
            if (!fragments.empty()) {
                keys.insert(fragments[0].start_sector);
            }
        }
        sqlite3_finalize(stmt);
    }

    return keys;
}

bool ScanCacheDB::save_recovery_progress(const RecoveryProgress& progress) {
    const char* sql = R"(
        INSERT OR REPLACE INTO recovery_progress
            (session_id, last_file_index, save_dirs, files_recovered,
             bytes_recovered, is_paused, ext_counters)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, progress.session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, progress.last_file_index);
    sqlite3_bind_text(stmt, 3, progress.save_dirs_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(progress.files_recovered));
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(progress.bytes_recovered));
    sqlite3_bind_int(stmt, 6, progress.is_paused ? 1 : 0);
    sqlite3_bind_text(stmt, 7, progress.ext_counters_json.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool ScanCacheDB::load_recovery_progress(const std::string& session_id, RecoveryProgress& progress) {
    const char* sql = R"(
        SELECT last_file_index, save_dirs, files_recovered,
               bytes_recovered, is_paused, ext_counters
        FROM recovery_progress
        WHERE session_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        progress.session_id = session_id;
        progress.last_file_index = sqlite3_column_int(stmt, 0);

        const char* save_dirs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        progress.save_dirs_json = save_dirs ? save_dirs : "";

        progress.files_recovered = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        progress.bytes_recovered = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        progress.is_paused = sqlite3_column_int(stmt, 4) != 0;

        const char* ext_counters = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        progress.ext_counters_json = ext_counters ? ext_counters : "{}";
    }

    sqlite3_finalize(stmt);
    return found;
}

bool ScanCacheDB::clear_recovery_progress(const std::string& session_id) {
    const char* sql = "DELETE FROM recovery_progress WHERE session_id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

} // namespace disk_recover
