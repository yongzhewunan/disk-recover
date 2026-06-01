#include "sector_reader.hpp"
#include <windows.h>
#include <cstring>

namespace disk_recover {

SectorReader::SectorReader(DiskHandle& handle, uint32_t sector_size)
    : handle_(handle), sector_size_(sector_size) {}

bool SectorReader::read_sectors(uint64_t start_sector, uint32_t count, AlignedBuffer& buffer) {
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(start_sector) * sector_size_;

    LONG high = offset.HighPart;
    ::SetFilePointer(handle_.native_handle(), offset.LowPart, &high, FILE_BEGIN);

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
    if (bad_sectors_ && bad_sectors_->is_bad(start_sector)) {
        switch (policy_) {
            case BadSectorPolicy::Skip:
                return false;
            case BadSectorPolicy::Retry: {
                for (int i = 0; i < 3; ++i) {
                    if (read_sectors(start_sector, count, buffer)) return true;
                }
                return false;
            }
            case BadSectorPolicy::ForceRead:
                break;
        }
    }
    return read_sectors(start_sector, count, buffer);
}

bool SectorReader::read_sectors_split(uint64_t start_sector, uint32_t count,
                                       AlignedBuffer& buffer,
                                       uint32_t& out_bad_count,
                                       std::function<bool()> should_stop) {
    out_bad_count = 0;
    if (count == 0) return false;
    AlignedBuffer scratch_buf(static_cast<size_t>(count) * sector_size_, sector_size_);
    return read_sectors_split_impl(start_sector, count, buffer.data(), out_bad_count, scratch_buf, should_stop);
}

bool SectorReader::read_sectors_split_impl(uint64_t start_sector, uint32_t count,
                                            uint8_t* out_ptr, uint32_t& out_bad_count,
                                            AlignedBuffer& scratch_buf,
                                            std::function<bool()>& should_stop) {
    out_bad_count = 0;
    if (count == 0) return false;
    if (should_stop && should_stop()) return false;

    if (read_sectors(start_sector, count, scratch_buf)) {
        memcpy(out_ptr, scratch_buf.data(), static_cast<size_t>(count) * sector_size_);
        return true;
    }

    if (count == 1) {
        if (bad_sectors_) bad_sectors_->record(start_sector, 1);
        memset(out_ptr, 0, sector_size_);
        out_bad_count = 1;
        return false;
    }

    uint32_t left_count = count / 2;
    uint32_t right_count = count - left_count;
    uint32_t left_bad = 0, right_bad = 0;

    read_sectors_split_impl(start_sector, left_count, out_ptr, left_bad, scratch_buf, should_stop);
    read_sectors_split_impl(start_sector + left_count, right_count,
                            out_ptr + static_cast<size_t>(left_count) * sector_size_,
                            right_bad, scratch_buf, should_stop);

    out_bad_count = left_bad + right_bad;
    return out_bad_count == 0;
}

} // namespace disk_recover