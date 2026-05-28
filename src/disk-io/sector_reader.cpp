#include "sector_reader.hpp"
#include <windows.h>

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

} // namespace disk_recover