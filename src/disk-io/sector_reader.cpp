#include "sector_reader.hpp"
#include "../common/logger.hpp"
#include <windows.h>
#include <cstring>

namespace disk_recover {

SectorReader::SectorReader(DiskHandle& handle, uint32_t sector_size)
    : handle_(handle), sector_size_(sector_size) {}

// Overlapped I/O read with timeout support
bool SectorReader::read_sectors_with_timeout(uint64_t start_sector, uint32_t count,
                                              AlignedBuffer& buffer, bool& timed_out) {
    timed_out = false;

    // If timeout disabled or zero, use synchronous read
    if (!timeout_config_.enabled || timeout_config_.timeout_ms == 0) {
        return read_sectors(start_sector, count, buffer);
    }

    HANDLE hFile = handle_.native_handle();
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(start_sector) * sector_size_;

    // Create manual-reset event for overlapped I/O
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hEvent) {
        LOG_FMT(L"[SectorReader] Failed to create event for overlapped I/O, error=%d", GetLastError());
        return read_sectors(start_sector, count, buffer);  // Fallback to sync
    }

    OVERLAPPED overlapped = {};
    overlapped.Offset = offset.LowPart;
    overlapped.OffsetHigh = offset.HighPart;
    overlapped.hEvent = hEvent;

    DWORD bytes_to_read = count * sector_size_;
    DWORD bytes_read = 0;

    // Initiate asynchronous read
    BOOL ok = ReadFile(hFile, buffer.data(), bytes_to_read, &bytes_read, &overlapped);

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        // Immediate failure (not pending)
        CloseHandle(hEvent);
        if (bad_sectors_) bad_sectors_->record(start_sector, count);
        return false;
    }

    // Wait with timeout
    DWORD wait_result = WaitForSingleObject(hEvent, timeout_config_.timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        // Timeout - cancel the pending I/O
        CancelIo(hFile);
        // Wait for cancellation to complete
        GetOverlappedResult(hFile, &overlapped, &bytes_read, TRUE);
        CloseHandle(hEvent);
        timed_out = true;
        LOG_FMT(L"[SectorReader] Read timeout at sector %llu (count=%u, timeout=%u ms)",
                 start_sector, count, timeout_config_.timeout_ms);
        if (bad_sectors_) bad_sectors_->record(start_sector, count);
        return false;
    }

    if (wait_result != WAIT_OBJECT_0) {
        // Other error
        CloseHandle(hEvent);
        if (bad_sectors_) bad_sectors_->record(start_sector, count);
        return false;
    }

    // Get final result
    ok = GetOverlappedResult(hFile, &overlapped, &bytes_read, FALSE);
    CloseHandle(hEvent);

    if (!ok || bytes_read != bytes_to_read) {
        if (bad_sectors_) bad_sectors_->record(start_sector, count);
        return false;
    }

    return true;
}

bool SectorReader::read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer) {
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(start_sector) * sector_size_;

    LONG high = offset.HighPart;
    DWORD result = ::SetFilePointer(handle_.native_handle(), offset.LowPart, &high, FILE_BEGIN);

    // Check if SetFilePointer failed
    // INVALID_SET_FILE_POINTER is returned on failure, but we must also check GetLastError
    // because INVALID_SET_FILE_POINTER could be a valid return value on 64-bit systems
    if (result == INVALID_SET_FILE_POINTER) {
        DWORD error = GetLastError();
        if (error != NO_ERROR) {
            LOG_FMT(L"[SectorReader] SetFilePointer failed at sector %llu, error=%d",
                     start_sector, error);
            if (bad_sectors_) {
                bad_sectors_->record(start_sector, count);
            }
            return false;
        }
    }

    DWORD bytes_to_read = count * sector_size_;
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(handle_.native_handle(), buffer.data(), bytes_to_read, &bytes_read, nullptr);

    if (!ok || bytes_read != bytes_to_read) {
        if (bad_sectors_) {
            bad_sectors_->record(start_sector, count);
        }
        return false;
    }
    return true;
}

bool SectorReader::read_sectors_checked(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer) {
    // Check if this sector is already known to be bad
    bool is_known_bad = bad_sectors_ && bad_sectors_->is_bad(start_sector);

    // Apply policy for known bad sectors
    if (is_known_bad) {
        switch (policy_) {
            case BadSectorPolicy::Skip:
                return false;
            case BadSectorPolicy::Retry:
                // Fall through to retry logic below
                break;
            case BadSectorPolicy::ForceRead:
                // Attempt to read anyway
                break;
        }
    }

    // Try initial read
    if (read_sectors(start_sector, count, buffer)) {
        return true;
    }

    // Read failed - apply Retry policy if configured
    if (policy_ == BadSectorPolicy::Retry) {
        for (int i = 0; i < 3; ++i) {
            if (read_sectors(start_sector, count, buffer)) {
                // Success on retry - clear bad sector record if it was marked
                return true;
            }
        }
    }

    return false;
}

bool SectorReader::read_sectors_split(uint64_t start_sector, uint32_t count,
                                       AlignedBuffer& buffer,
                                       uint32_t& out_bad_count,
                                       uint32_t& out_skip_ahead,
                                       std::function<bool()> should_stop) {
    out_bad_count = 0;
    out_skip_ahead = 0;
    if (count == 0) return false;
    AlignedBuffer scratch_buf(static_cast<size_t>(count) * sector_size_, sector_size_);
    return read_sectors_split_impl(start_sector, count, buffer.data(), out_bad_count, scratch_buf, should_stop, 0);
}

bool SectorReader::read_sectors_split_impl(uint64_t start_sector, uint32_t count,
                                            uint8_t* out_ptr, uint32_t& out_bad_count,
                                            AlignedBuffer& scratch_buf,
                                            std::function<bool()>& should_stop,
                                            int depth) {
    out_bad_count = 0;
    if (count == 0) return false;

    // Check stop BEFORE any I/O
    if (should_stop && should_stop()) return false;

    // Stack depth limit to prevent overflow on severely damaged disks
    // 2^20 = 1M sectors minimum granularity
    constexpr int MAX_DEPTH = 20;
    if (depth > MAX_DEPTH) {
        LOG_FMT(L"[SectorReader] Binary split depth limit reached at sector %llu, marking %u sectors as bad",
                 start_sector, count);
        if (bad_sectors_) bad_sectors_->record(start_sector, count);
        memset(out_ptr, 0, static_cast<size_t>(count) * sector_size_);
        out_bad_count = count;
        return false;
    }

    // Check if this range contains known bad sectors
    bool has_known_bad = false;
    if (bad_sectors_) {
        for (uint32_t i = 0; i < count; ++i) {
            if (bad_sectors_->is_bad(start_sector + i)) {
                has_known_bad = true;
                break;
            }
        }
    }

    // Apply Skip policy: if range contains known bad sectors, skip entirely
    if (has_known_bad && policy_ == BadSectorPolicy::Skip) {
        memset(out_ptr, 0, static_cast<size_t>(count) * sector_size_);
        out_bad_count = count;
        return false;
    }

    // Try read with timeout
    bool timed_out = false;
    if (read_sectors_with_timeout(start_sector, count, scratch_buf, timed_out)) {
        memcpy(out_ptr, scratch_buf.data(), static_cast<size_t>(count) * sector_size_);
        return true;
    }

    // On timeout, mark entire range as bad without further splitting
    if (timed_out) {
        memset(out_ptr, 0, static_cast<size_t>(count) * sector_size_);
        out_bad_count = count;
        return false;
    }

    // Read failed - apply Retry policy before splitting
    if (policy_ == BadSectorPolicy::Retry) {
        for (int retry = 0; retry < static_cast<int>(timeout_config_.retry_count); ++retry) {
            bool retry_timed_out = false;
            if (read_sectors_with_timeout(start_sector, count, scratch_buf, retry_timed_out)) {
                memcpy(out_ptr, scratch_buf.data(), static_cast<size_t>(count) * sector_size_);
                return true;
            }
            if (retry_timed_out) {
                // Timeout on retry - don't continue retrying
                memset(out_ptr, 0, static_cast<size_t>(count) * sector_size_);
                out_bad_count = count;
                return false;
            }
        }
    }

    // ForceRead policy or retries exhausted - split and continue

    // Single sector case - mark as bad and fill with zeros
    if (count == 1) {
        if (bad_sectors_) bad_sectors_->record(start_sector, 1);
        memset(out_ptr, 0, sector_size_);
        out_bad_count = 1;
        return false;
    }

    // Split into two halves and recurse
    uint32_t left_count = count / 2;
    uint32_t right_count = count - left_count;
    uint32_t left_bad = 0, right_bad = 0;

    // Check stop before first half
    if (should_stop && should_stop()) return false;

    read_sectors_split_impl(start_sector, left_count, out_ptr, left_bad, scratch_buf, should_stop, depth + 1);

    // Check stop before second half
    if (should_stop && should_stop()) {
        out_bad_count = left_bad;
        return false;
    }

    read_sectors_split_impl(start_sector + left_count, right_count,
                            out_ptr + static_cast<size_t>(left_count) * sector_size_,
                            right_bad, scratch_buf, should_stop, depth + 1);

    out_bad_count = left_bad + right_bad;
    return out_bad_count == 0;
}

} // namespace disk_recover