#pragma once
#include "exfat_types.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <vector>
#include <functional>

namespace disk_recover::exfat {

class ExfatParser {
public:
    bool parse_boot_sector(SectorReader& reader, uint64_t partition_start);

    uint32_t sector_size() const { return sector_size_; }
    uint32_t cluster_size() const { return cluster_size_; }
    uint32_t fat_start() const { return fat_start_; }
    uint32_t cluster_heap_start() const { return cluster_heap_start_; }
    uint32_t root_cluster() const { return root_cluster_; }
    uint32_t total_clusters() const { return total_clusters_; }

private:
    uint64_t partition_start_ = 0;
    uint32_t sector_size_ = 512;
    uint32_t sectors_per_cluster_ = 1;
    uint32_t cluster_size_ = 512;
    uint32_t fat_start_ = 0;
    uint32_t fat_length_ = 0;
    uint32_t cluster_heap_start_ = 0;
    uint32_t root_cluster_ = 0;
    uint32_t total_clusters_ = 0;
    uint8_t  number_of_fats_ = 1;
};

} // namespace disk_recover::exfat
