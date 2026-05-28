#include "mft_parser.hpp"
#include <cstring>

namespace disk_recover::ntfs {

bool MftParser::parse_boot_sector(SectorReader& reader, uint64_t partition_start) {
    partition_start_ = partition_start;
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) return false;

    auto* boot = reinterpret_cast<const NtfsBootSector*>(buf.data());
    if (boot->bytes_per_sector == 0 || boot->sectors_per_cluster == 0) return false;

    sector_size_ = boot->bytes_per_sector;
    sectors_per_cluster_ = boot->sectors_per_cluster;
    cluster_size_ = sector_size_ * sectors_per_cluster_;

    mft_start_sector_ = partition_start + boot->mft_start_cluster * sectors_per_cluster_;

    if (boot->clusters_per_mft_record > 0) {
        mft_record_size_ = cluster_size_ * boot->clusters_per_mft_record;
    } else {
        mft_record_size_ = 1u << (-boot->clusters_per_mft_record);
    }
    return true;
}

bool MftParser::enumerate_mft(SectorReader& reader,
                              std::function<void(RecoverableFile&&)> callback,
                              bool include_deleted) {
    uint32_t sectors_per_record = mft_record_size_ / sector_size_;
    AlignedBuffer buf(mft_record_size_, sector_size_);

    for (uint64_t i = 0; ; ++i) {
        uint64_t sector = mft_start_sector_ + i * sectors_per_record;
        if (!reader.read_sectors(sector, sectors_per_record, buf)) break;

        auto* hdr = reinterpret_cast<const MftRecordHeader*>(buf.data());
        if (hdr->signature != 0x454C4946) continue;  // "FILE"

        bool is_deleted = !(hdr->flags & MFT_FLAG_IN_USE);
        if (is_deleted && !include_deleted) continue;

        RecoverableFile file{};
        file.mft_id = i;
        if (parse_mft_record(buf.data(), mft_record_size_, file, is_deleted)) {
            file.is_corrupted = is_deleted;
            callback(std::move(file));
        }
    }
    return true;
}

bool MftParser::parse_mft_record(const uint8_t* data, uint32_t size,
                                 RecoverableFile& file, bool& is_deleted) {
    auto* hdr = reinterpret_cast<const MftRecordHeader*>(data);
    uint32_t offset = hdr->attribute_offset;

    while (offset + sizeof(AttributeHeader) < size) {
        auto* attr = reinterpret_cast<const AttributeHeader*>(data + offset);
        if (attr->type == 0xFFFFFFFF || attr->length == 0) break;

        if (attr->type == ATTR_FILE_NAME && attr->non_resident == 0) {
            uint32_t content_offset = offset + 0x18;
            if (content_offset + 0x42 < offset + attr->length) {
                const uint8_t* name_ptr = data + content_offset + 0x40;
                uint8_t name_len = data[content_offset + 0x40 - 2];
                name_len *= 2;
                if (content_offset + 0x40 + name_len <= offset + attr->length) {
                    file.file_name.assign(
                        reinterpret_cast<const wchar_t*>(name_ptr), name_len / 2);
                }
            }
        }

        if (attr->type == ATTR_DATA && attr->non_resident == 1) {
            uint32_t data_runs_offset = offset + 0x40;
            decode_data_runs(data + data_runs_offset,
                           attr->length - 0x40, file.fragments);
            uint64_t file_size = *reinterpret_cast<const uint64_t*>(data + offset + 0x30);
            file.file_size = file_size;
        }

        offset += attr->length;
    }

    if (!file.file_name.empty() && !file.fragments.empty()) {
        file.file_type = FileType::Unknown;
        return true;
    }
    return false;
}

bool MftParser::decode_data_runs(const uint8_t* data, uint32_t length,
                                 std::vector<DiskExtent>& extents) {
    uint32_t offset = 0;
    int64_t current_cluster = 0;

    while (offset < length) {
        uint8_t header = data[offset++];
        if (header == 0) break;

        uint8_t len_size = header & 0x0F;
        uint8_t offset_size = (header >> 4) & 0x0F;
        if (len_size == 0 || offset_size == 0) break;
        if (offset + len_size + offset_size > length) break;

        uint64_t run_length = 0;
        for (uint8_t i = 0; i < len_size; ++i) {
            run_length |= static_cast<uint64_t>(data[offset++]) << (i * 8);
        }

        int64_t run_offset = 0;
        for (uint8_t i = 0; i < offset_size; ++i) {
            run_offset |= static_cast<uint64_t>(data[offset++]) << (i * 8);
        }
        if (data[offset - 1] & 0x80) {
            run_offset -= 1LL << (offset_size * 8);
        }

        current_cluster += run_offset;
        if (current_cluster > 0) {
            DiskExtent ext;
            ext.start_sector = partition_start_ + current_cluster * sectors_per_cluster_;
            ext.sector_count = run_length * sectors_per_cluster_;
            extents.push_back(ext);
        }
    }
    return !extents.empty();
}

} // namespace disk_recover::ntfs
