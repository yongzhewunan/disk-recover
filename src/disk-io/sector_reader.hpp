#pragma once
#include "disk_handle.hpp"
#include "aligned_buffer.hpp"
#include "bad_sector_manager.hpp"
#include "types.hpp"

namespace disk_recover {

class SectorReader {
public:
    explicit SectorReader(DiskHandle& handle, uint32_t sector_size = 512);

    bool read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);
    bool read_sectors_checked(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);

    void set_bad_sector_manager(BadSectorManager* manager) { bad_sectors_ = manager; }
    void set_bad_sector_policy(BadSectorPolicy policy) { policy_ = policy; }

    uint32_t sector_size() const { return sector_size_; }

private:
    DiskHandle& handle_;
    uint32_t sector_size_;
    BadSectorManager* bad_sectors_ = nullptr;
    BadSectorPolicy policy_ = BadSectorPolicy::Skip;
};

} // namespace disk_recover