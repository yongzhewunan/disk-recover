#pragma once
#include "disk_handle.hpp"
#include "aligned_buffer.hpp"
#include "bad_sector_manager.hpp"
#include "types.hpp"
#include <functional>

namespace disk_recover {

class SectorReader {
public:
    explicit SectorReader(DiskHandle& handle, uint32_t sector_size = 512);

    bool read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);
    bool read_sectors_checked(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);

    // Binary-split fallback: on batch read failure, recursively splits in half
    // to isolate bad sectors. Uses scratch buffer to avoid per-recursion heap allocation.
    // Returns true if at least some sectors were read successfully.
    bool read_sectors_split(uint64_t start_sector, uint32_t count,
                            AlignedBuffer& buffer,
                            uint32_t& out_bad_count,
                            std::function<bool()> should_stop = nullptr);

    void set_bad_sector_manager(BadSectorManager* manager) { bad_sectors_ = manager; }
    void set_bad_sector_policy(BadSectorPolicy policy) { policy_ = policy; }

    uint32_t sector_size() const { return sector_size_; }

private:
    bool read_sectors_split_impl(uint64_t start_sector, uint32_t count,
                                 uint8_t* out_ptr, uint32_t& out_bad_count,
                                 AlignedBuffer& scratch_buf,
                                 std::function<bool()>& should_stop);

private:
    DiskHandle& handle_;
    uint32_t sector_size_;
    BadSectorManager* bad_sectors_ = nullptr;
    BadSectorPolicy policy_ = BadSectorPolicy::Skip;
};

} // namespace disk_recover