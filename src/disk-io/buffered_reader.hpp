#pragma once
#include "disk_handle.hpp"
#include "aligned_buffer.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace disk_recover {

// Buffered sector reader with large prefetch buffer (default 128MB)
// Reduces system calls by caching disk sectors in memory
// Compatible with SectorReader interface for use in scanning
class BufferedSectorReader {
public:
    // Default buffer size: 16MB
    static constexpr size_t DEFAULT_BUFFER_SIZE = 16 * 1024 * 1024;

    BufferedSectorReader(DiskHandle& handle, uint32_t sector_size,
                         size_t buffer_size = DEFAULT_BUFFER_SIZE);
    ~BufferedSectorReader() = default;

    // Non-copyable
    BufferedSectorReader(const BufferedSectorReader&) = delete;
    BufferedSectorReader& operator=(const BufferedSectorReader&) = delete;

    // Read sectors - returns data from buffer if available, otherwise prefetchs
    // Returns true on success, false on read error or timeout
    bool read_sectors(uint64_t start_sector, uint32_t count, void* buffer);

    // Read into AlignedBuffer (compatible with SectorReader)
    bool read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);

    // Read with error handling (compatible with SectorReader)
    bool read_sectors_checked(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer);

    // Binary-split fallback (compatible with SectorReader)
    // Delegates to internal SectorReader for bad sector handling
    bool read_sectors_split(uint64_t start_sector, uint32_t count,
                            AlignedBuffer& buffer,
                            uint32_t& out_bad_count,
                            uint32_t& out_skip_ahead,
                            std::function<bool()> should_stop = nullptr);

    // Prefetch a region into the buffer (can be called ahead of time)
    // Returns true on success
    bool prefetch(uint64_t start_sector);

    // Invalidate current buffer (e.g., after disk state change)
    void invalidate_buffer();

    // Accessors
    uint32_t sector_size() const { return sector_size_; }
    size_t buffer_size() const { return buffer_.size(); }
    uint64_t buffer_start_sector() const { return buffer_start_sector_; }
    uint32_t buffer_sector_count() const { return buffer_sector_count_; }
    bool buffer_valid() const { return buffer_valid_; }

    // Statistics
    uint64_t total_reads() const { return total_reads_; }
    uint64_t cache_hits() const { return cache_hits_; }
    uint64_t cache_misses() const { return cache_misses_; }

    // Bad sector configuration (delegated to internal reader)
    void set_bad_sector_manager(BadSectorManager* manager) {
        raw_reader_.set_bad_sector_manager(manager);
    }
    void set_bad_sector_policy(BadSectorPolicy policy) {
        raw_reader_.set_bad_sector_policy(policy);
    }
    void set_skip_ahead_config(const SkipAheadConfig& config) {
        raw_reader_.set_skip_ahead_config(config);
    }
    void set_timeout_config(const ReadTimeoutConfig& config) {
        raw_reader_.set_timeout_config(config);
    }
    uint32_t get_skip_ahead_count() const {
        return raw_reader_.get_skip_ahead_count();
    }
    void reset_bad_sector_counter() {
        raw_reader_.reset_bad_sector_counter();
    }

private:
    bool read_into_buffer(uint64_t start_sector);

    DiskHandle& handle_;
    uint32_t sector_size_;
    AlignedBuffer buffer_;
    uint64_t buffer_start_sector_ = 0;
    uint32_t buffer_sector_count_ = 0;
    bool buffer_valid_ = false;

    // Internal SectorReader for bad sector handling
    SectorReader raw_reader_;

    // Statistics
    uint64_t total_reads_ = 0;
    uint64_t cache_hits_ = 0;
    uint64_t cache_misses_ = 0;
};

} // namespace disk_recover
