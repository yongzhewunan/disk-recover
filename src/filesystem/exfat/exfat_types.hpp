#pragma once
#include <cstdint>

namespace disk_recover::exfat {

struct ExfatBootSector {
    uint8_t  jump[3];
    uint8_t  fs_name[8];
    uint8_t  reserved1[53];
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_directory;
    uint32_t volume_serial;
    uint8_t  fs_revision[2];
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift;
    uint8_t  sectors_per_cluster_shift;
    uint8_t  number_of_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved2[7];
    uint8_t  boot_code[390];
    uint16_t signature;
};

struct ExfatFileEntry {
    uint8_t  entry_type;
    uint8_t  secondary_count;
    uint16_t checksum;
    uint16_t attributes;
    uint16_t reserved1;
    uint32_t create_time;
    uint32_t create_date;
    uint32_t modify_time;
    uint32_t modify_date;
    uint32_t access_time;
    uint8_t  create_time_tenth;
    uint8_t  modify_time_tenth;
    uint8_t  access_time_tenth;
    uint8_t  reserved2[9];
};

struct ExfatStreamEntry {
    uint8_t  entry_type;
    uint8_t  general_secondary_flags;
    uint8_t  reserved1;
    uint8_t  name_length;
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_length;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
};

struct ExfatNameEntry {
    uint8_t  entry_type;
    uint8_t  general_secondary_flags;
    uint8_t  reserved[2];
    uint16_t name[30];  // up to 15 UTF-16 characters
};

constexpr uint8_t ENTRY_TYPE_FILE        = 0x85;
constexpr uint8_t ENTRY_TYPE_STREAM      = 0xC0;
constexpr uint8_t ENTRY_TYPE_NAME        = 0xC1;
constexpr uint8_t ENTRY_TYPE_DELETED     = 0x05;
constexpr uint8_t ENTRY_TYPE_DELETED_EXT = 0x40;

} // namespace disk_recover::exfat
