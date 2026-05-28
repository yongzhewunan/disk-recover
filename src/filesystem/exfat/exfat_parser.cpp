#include "exfat_parser.hpp"
#include <cstring>

namespace disk_recover::exfat {

bool ExfatParser::parse_boot_sector(SectorReader& reader, uint64_t partition_start) {
    partition_start_ = partition_start;
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) return false;

    auto* boot = reinterpret_cast<const ExfatBootSector*>(buf.data());

    // Check signature
    if (boot->signature != 0xAA55) return false;

    // Check FS name "EXFAT   "
    const char expected_fs_name[] = "EXFAT   ";
    if (std::memcmp(boot->fs_name, expected_fs_name, 8) != 0) return false;

    sector_size_ = 1u << boot->bytes_per_sector_shift;
    sectors_per_cluster_ = 1u << boot->sectors_per_cluster_shift;
    cluster_size_ = sector_size_ * sectors_per_cluster_;

    fat_start_ = boot->fat_offset;
    fat_length_ = boot->fat_length;
    cluster_heap_start_ = boot->cluster_heap_offset;
    total_clusters_ = boot->cluster_count;
    root_cluster_ = boot->root_directory;
    number_of_fats_ = boot->number_of_fats;

    return true;
}

} // namespace disk_recover::exfat
