#pragma once
#include "fat_types.hpp"
#include "../../disk-io/sector_reader.hpp"
#include "../../common/types.hpp"
#include <vector>
#include <functional>

namespace disk_recover::fat {

class FatParser {
public:
    bool parse_boot_sector(SectorReader& reader, uint64_t partition_start);
    FatType fat_type() const { return fat_type_; }
    uint32_t cluster_size() const { return cluster_size_; }
    uint32_t bytes_per_sector() const { return bytes_per_sector_; }
    uint32_t root_dir_sector() const { return root_dir_sector_; }
    uint32_t data_start_sector() const { return data_start_sector_; }
    uint32_t total_clusters() const { return total_clusters_; }

    bool enumerate_root_dir(SectorReader& reader,
                           std::function<void(RecoverableFile&&)> callback,
                           bool include_deleted = true,
                           std::function<bool()> should_stop = {});

    bool read_cluster_chain(SectorReader& reader, uint32_t start_cluster,
                           std::vector<DiskExtent>& extents);

private:
    uint32_t get_fat_entry(SectorReader& reader, uint32_t cluster);
    uint32_t cluster_to_sector(uint32_t cluster) const;
    bool is_end_of_chain(uint32_t entry) const;
    bool is_bad_cluster(uint32_t entry) const;
    static FileType detect_file_type(const std::wstring& filename);
    bool enumerate_directory(SectorReader& reader, uint32_t start_cluster,
                             const std::function<void(RecoverableFile&&)>& callback,
                             bool include_deleted, int depth,
                             const std::function<bool()>& should_stop);

    FatType fat_type_ = FatType::Unknown;
    uint64_t partition_start_ = 0;
    uint32_t bytes_per_sector_ = 512;
    uint32_t sectors_per_cluster_ = 1;
    uint32_t cluster_size_ = 512;
    uint32_t reserved_sectors_ = 0;
    uint32_t fat_count_ = 2;
    uint32_t sectors_per_fat_ = 0;
    uint32_t root_entry_count_ = 0;
    uint32_t root_dir_sector_ = 0;
    uint32_t root_dir_sectors_ = 0;
    uint32_t data_start_sector_ = 0;
    uint32_t total_clusters_ = 0;
    uint32_t root_cluster_ = 2;  // FAT32 only
};

} // namespace disk_recover::fat
