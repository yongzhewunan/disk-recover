#include "bad_sector_manager.hpp"
#include <fstream>

namespace disk_recover {

BadSectorManager::~BadSectorManager() {
    close();
}

void BadSectorManager::open(const std::wstring& db_path) {
    db_path_ = db_path;
    std::ifstream in(db_path, std::ios::binary);
    if (!in) return;
    uint64_t sector;
    while (in.read(reinterpret_cast<char*>(&sector), sizeof(sector))) {
        bad_sectors_.insert(sector);
    }
}

void BadSectorManager::close() {
    if (db_path_.empty()) return;
    std::ofstream out(db_path_, std::ios::binary | std::ios::trunc);
    for (uint64_t sector : bad_sectors_) {
        out.write(reinterpret_cast<const char*>(&sector), sizeof(sector));
    }
}

void BadSectorManager::record(uint64_t start_sector, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        bad_sectors_.insert(start_sector + i);
    }
}

bool BadSectorManager::is_bad(uint64_t sector) const {
    return bad_sectors_.count(sector) > 0;
}

} // namespace disk_recover