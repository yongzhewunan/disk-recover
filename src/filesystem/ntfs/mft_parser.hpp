#pragma once
#include "ntfs_types.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <vector>
#include <functional>

namespace disk_recover::ntfs {

class MftParser {
public:
    bool parse_boot_sector(SectorReader& reader, uint64_t partition_start);
    bool enumerate_mft(SectorReader& reader,
                       std::function<void(RecoverableFile&&)> callback,
                       bool include_deleted = true);

    uint64_t mft_start_sector() const { return mft_start_sector_; }
    uint32_t mft_record_size() const { return mft_record_size_; }
    uint32_t cluster_size() const { return cluster_size_; }

private:
    bool parse_mft_record(const uint8_t* data, uint32_t size,
                          RecoverableFile& file, bool& is_deleted);
    bool decode_data_runs(const uint8_t* data, uint32_t length,
                          std::vector<DiskExtent>& extents);

    uint64_t partition_start_ = 0;
    uint64_t mft_start_sector_ = 0;
    uint32_t mft_record_size_ = 1024;
    uint32_t cluster_size_ = 4096;
    uint32_t sector_size_ = 512;
    uint64_t sectors_per_cluster_ = 8;
};

} // namespace disk_recover::ntfs
