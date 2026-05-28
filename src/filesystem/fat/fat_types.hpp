#pragma once
#include <cstdint>

namespace disk_recover::fat {

enum class FatType { Unknown, Fat12, Fat16, Fat32 };

struct FatBootSector {
    uint8_t  jump[3];
    uint8_t  oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    union {
        struct {
            uint8_t  drive_number;
            uint8_t  reserved1;
            uint8_t  extended_boot_signature;
            uint32_t volume_serial;
            uint8_t  volume_label[11];
            uint8_t  fs_type[8];
        } fat16_ext;
        struct {
            uint32_t sectors_per_fat_32;
            uint16_t ext_flags;
            uint16_t fs_version;
            uint32_t root_cluster;
            uint16_t fs_info_sector;
            uint16_t backup_boot_sector;
            uint8_t  reserved[12];
            uint8_t  drive_number;
            uint8_t  reserved1;
            uint8_t  extended_boot_signature;
            uint32_t volume_serial;
            uint8_t  volume_label[11];
            uint8_t  fs_type[8];
        } fat32_ext;
    } ext;
};

constexpr uint32_t FAT12_EOC = 0x0FF8;
constexpr uint32_t FAT16_EOC = 0xFFF8;
constexpr uint32_t FAT32_EOC = 0x0FFFFFF8;
constexpr uint32_t FAT12_BAD = 0x0FF7;
constexpr uint32_t FAT16_BAD = 0xFFF7;
constexpr uint32_t FAT32_BAD = 0x0FFFFFF7;

} // namespace disk_recover::fat
