#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

namespace disk_recover {

class BadSectorManager {
public:
    BadSectorManager() = default;
    ~BadSectorManager();  // Destructor calls close() for RAII

    void open(const std::wstring& db_path);
    void close();

    void record(uint64_t start_sector, uint64_t count);
    bool is_bad(uint64_t sector) const;

    uint64_t total_bad_sectors() const { return bad_sectors_.size(); }
    const std::unordered_set<uint64_t>& bad_sectors() const { return bad_sectors_; }

private:
    std::unordered_set<uint64_t> bad_sectors_;
    std::wstring db_path_;
};

} // namespace disk_recover