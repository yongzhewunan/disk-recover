#pragma once
#include <cstdint>

namespace disk_recover::ntfs {

#pragma pack(push, 1)

struct NtfsBootSector {
    uint8_t  jump[3];
    uint8_t  oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  reserved1[3];
    uint16_t unused1;
    uint8_t  media_descriptor;
    uint16_t unused2;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t hidden_sectors_high;
    uint32_t unused3;
    uint64_t total_sectors;
    uint64_t mft_start_cluster;
    uint64_t mft_mirror_cluster;
    int8_t   clusters_per_mft_record;
    int8_t   clusters_per_index_record;
    uint8_t  reserved2[2];
    uint64_t volume_serial_number;
    uint32_t checksum;
};

struct MftRecordHeader {
    uint32_t signature;
    uint16_t usa_offset;
    uint16_t usa_count;
    uint64_t log_sequence;
    uint16_t sequence_number;
    uint16_t hard_link_count;
    uint16_t attribute_offset;
    uint16_t flags;
    uint32_t used_size;
    uint32_t allocated_size;
};

struct AttributeHeader {
    uint32_t type;
    uint32_t length;
    uint8_t  non_resident;
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attribute_id;
};

constexpr uint32_t ATTR_STANDARD_INFORMATION = 0x10;
constexpr uint32_t ATTR_FILE_NAME           = 0x30;
constexpr uint32_t ATTR_DATA                = 0x80;
constexpr uint32_t ATTR_INDEX_ROOT          = 0x90;
constexpr uint32_t ATTR_INDEX_ALLOCATION    = 0xA0;
constexpr uint32_t ATTR_BITMAP              = 0xB0;

constexpr uint16_t MFT_FLAG_IN_USE    = 0x0001;
constexpr uint16_t MFT_FLAG_DIRECTORY = 0x0002;

#pragma pack(pop)

} // namespace disk_recover::ntfs
