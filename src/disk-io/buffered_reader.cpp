#include "buffered_reader.hpp"
#include "../common/logger.hpp"
#include <algorithm>
#include <cstring>

namespace disk_recover {

BufferedSectorReader::BufferedSectorReader(DiskHandle& handle, uint32_t sector_size,
                                            size_t buffer_size)
    : handle_(handle)
    , sector_size_(sector_size)
    , buffer_(buffer_size, sector_size)
    , raw_reader_(handle, sector_size) {
    LOG_FMT(L"[BufferedReader] Created with buffer_size=%zu MB, sector_size=%u",
             buffer_size / (1024 * 1024), sector_size);
}

bool BufferedSectorReader::read_into_buffer(uint64_t start_sector) {
    // Read in 4MB chunks to avoid blocking on bad sectors
    const uint32_t CHUNK_SECTORS = 8192;  // 4MB per chunk
    uint32_t total_sectors = static_cast<uint32_t>(buffer_.size() / sector_size_);
    uint32_t sectors_read = 0;

    for (uint64_t chunk_start = start_sector;
         sectors_read < total_sectors;
         chunk_start += CHUNK_SECTORS, sectors_read += CHUNK_SECTORS) {
        uint32_t to_read = (std::min)(CHUNK_SECTORS, total_sectors - sectors_read);
        uint8_t* dst = buffer_.data() + static_cast<size_t>(sectors_read) * sector_size_;

        // Use raw_reader_ which has timeout control
        AlignedBuffer chunk_buf(to_read * sector_size_, sector_size_);
        if (!raw_reader_.read_sectors(chunk_start, to_read, chunk_buf)) {
            // Failed/timeout - fill remaining with zeros and stop
            size_t remaining = static_cast<size_t>(total_sectors - sectors_read) * sector_size_;
            memset(dst, 0, remaining);
            LOG_FMT(L"[BufferedReader] Chunk read failed at sector %llu, stopping prefetch (read %u/%u sectors)",
                     chunk_start, sectors_read, total_sectors);
            break;
        }
        memcpy(dst, chunk_buf.data(), static_cast<size_t>(to_read) * sector_size_);
    }

    buffer_start_sector_ = start_sector;
    buffer_sector_count_ = total_sectors;
    buffer_valid_ = (sectors_read > 0);

    if (buffer_valid_) {
        LOG_FMT(L"[BufferedReader] Prefetched %u sectors starting at %llu",
                 buffer_sector_count_, buffer_start_sector_);
    }

    return buffer_valid_;
}

bool BufferedSectorReader::prefetch(uint64_t start_sector) {
    return read_into_buffer(start_sector);
}

void BufferedSectorReader::invalidate_buffer() {
    buffer_valid_ = false;
    buffer_start_sector_ = 0;
    buffer_sector_count_ = 0;
}

bool BufferedSectorReader::read_sectors(uint64_t start_sector, uint32_t count, void* buffer) {
    total_reads_++;

    uint64_t buffer_end_sector = buffer_start_sector_ + buffer_sector_count_;

    // Check if requested range is entirely within the buffer
    if (buffer_valid_ &&
        start_sector >= buffer_start_sector_ &&
        start_sector + count <= buffer_end_sector) {
        // Cache hit - copy from buffer
        size_t offset = static_cast<size_t>(start_sector - buffer_start_sector_) * sector_size_;
        size_t bytes = static_cast<size_t>(count) * sector_size_;
        memcpy(buffer, buffer_.data() + offset, bytes);
        cache_hits_++;

        // Log every 10000 cache hits
        if (cache_hits_ % 10000 == 0) {
            LOG_FMT(L"[BufferedReader] Cache hit rate: %.1f%% (%llu/%llu)",
                     100.0 * cache_hits_ / total_reads_, cache_hits_, total_reads_);
        }
        return true;
    }

    // Cache miss - need to prefetch
    cache_misses_++;

    // Prefetch starting from the requested sector
    if (!read_into_buffer(start_sector)) {
        return false;
    }

    // Try again after prefetch
    buffer_end_sector = buffer_start_sector_ + buffer_sector_count_;
    if (start_sector >= buffer_start_sector_ &&
        start_sector + count <= buffer_end_sector) {
        size_t offset = static_cast<size_t>(start_sector - buffer_start_sector_) * sector_size_;
        size_t bytes = static_cast<size_t>(count) * sector_size_;
        memcpy(buffer, buffer_.data() + offset, bytes);
        return true;
    }

    // Request larger than buffer - this shouldn't happen with proper usage
    LOG_FMT(L"[BufferedReader] Request too large: %u sectors > buffer capacity %u",
             count, buffer_sector_count_);
    return false;
}

bool BufferedSectorReader::read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer) {
    if (buffer.size() < count * sector_size_) {
        return false;
    }
    return read_sectors(start_sector, count, buffer.data());
}

bool BufferedSectorReader::read_sectors_checked(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer) {
    // For checked reads, use the raw reader which has bad sector handling
    return raw_reader_.read_sectors_checked(start_sector, count, buffer);
}

bool BufferedSectorReader::read_sectors_split(uint64_t start_sector, uint32_t count,
                                               AlignedBuffer& buffer,
                                               uint32_t& out_bad_count,
                                               uint32_t& out_skip_ahead,
                                               std::function<bool()> should_stop) {
    out_bad_count = 0;
    out_skip_ahead = 0;

    // Fast path: check if data is already in buffer
    if (buffer_valid_) {
        uint64_t buffer_end_sector = buffer_start_sector_ + buffer_sector_count_;
        if (start_sector >= buffer_start_sector_ &&
            start_sector + count <= buffer_end_sector) {
            size_t offset = static_cast<size_t>(start_sector - buffer_start_sector_) * sector_size_;
            size_t bytes = static_cast<size_t>(count) * sector_size_;
            memcpy(buffer.data(), buffer_.data() + offset, bytes);
            cache_hits_++;
            total_reads_++;
            return true;
        }
    }

    // Cache miss - delegate to raw reader for proper bad sector handling
    total_reads_++;
    cache_misses_++;

    bool result = raw_reader_.read_sectors_split(start_sector, count, buffer, out_bad_count, out_skip_ahead, should_stop);

    // After a successful read with no bad sectors, try to prefetch next 128MB block
    // This is async-friendly: if the next read is in this block, it will be a cache hit
    if (result && out_bad_count == 0 && !buffer_valid_) {
        // Kick off prefetch for the current position
        read_into_buffer(start_sector);
    }

    return result;
}

} // namespace disk_recover