#include "mft_parser.hpp"
#include "../../common/logger.hpp"
#include <cstring>
#include <algorithm>

namespace disk_recover::ntfs {

void MftParser::apply_usa_fixup(uint8_t* data, uint32_t size, uint32_t sector_size) {
    auto* hdr = reinterpret_cast<const MftRecordHeader*>(data);
    uint16_t usa_offset = hdr->usa_offset;
    uint16_t usa_count = hdr->usa_count;

    if (usa_offset == 0 || usa_count <= 1 || usa_offset + usa_count * 2 > size) return;

    // USA array: entry 0 = sequence number, entries 1..usa_count-1 = saved bytes
    const uint16_t* usa = reinterpret_cast<const uint16_t*>(data + usa_offset);
    uint16_t sequence = usa[0];

    // Each sector i (1-based) has its last 2 bytes overwritten by USA.
    // Restore them from the USA array.
    uint32_t num_sectors = size / sector_size;
    for (uint32_t i = 1; i < usa_count && i <= num_sectors; ++i) {
        uint32_t sector_end = i * sector_size;
        if (sector_end < 2 || sector_end > size) continue;
        uint16_t* last_two = reinterpret_cast<uint16_t*>(data + sector_end - 2);
        // Verify the sequence number matches (if not, record may be corrupt)
        if (*last_two == sequence) {
            *last_two = usa[i];
        }
    }
}

FileType MftParser::detect_file_type(const std::wstring& filename) {
    size_t dot = filename.rfind(L'.');
    if (dot == std::wstring::npos || dot + 1 >= filename.size()) return FileType::Unknown;

    std::wstring ext = filename.substr(dot + 1);
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    static const wchar_t* image_exts[] = {
        L"jpg", L"jpeg", L"png", L"gif", L"bmp", L"tiff", L"tif",
        L"cr2", L"nef", L"arw", L"dng", L"webp", L"ico", L"heic",
        L"raw", L"psd", L"svg", L"jfif"
    };
    static const wchar_t* video_exts[] = {
        L"mp4", L"mov", L"avi", L"mkv", L"webm", L"flv",
        L"wmv", L"m4v", L"3gp", L"mts", L"m2ts", L"vob",
        L"asf", L"rm", L"rmvb"
    };

    for (const auto* e : image_exts) {
        if (ext == e) return FileType::Image;
    }
    for (const auto* e : video_exts) {
        if (ext == e) return FileType::Video;
    }
    return FileType::Unknown;
}

bool MftParser::parse_boot_sector(SectorReader& reader, uint64_t partition_start) {
    partition_start_ = partition_start;
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) {
        LOG_FMT(L"[MftParser] Failed to read boot sector at %llu", partition_start);
        return false;
    }

    auto* boot = reinterpret_cast<const NtfsBootSector*>(buf.data());
    if (boot->bytes_per_sector == 0 || boot->sectors_per_cluster == 0) {
        LOG_FMT(L"[MftParser] Invalid boot sector: bytes_per_sector=%u, sectors_per_cluster=%u",
                 boot->bytes_per_sector, boot->sectors_per_cluster);
        return false;
    }

    sector_size_ = boot->bytes_per_sector;
    sectors_per_cluster_ = boot->sectors_per_cluster;
    cluster_size_ = sector_size_ * sectors_per_cluster_;

    mft_start_sector_ = partition_start + boot->mft_start_cluster * sectors_per_cluster_;

    LOG_FMT(L"[MftParser] Boot sector parsed: sector_size=%u, sectors_per_cluster=%llu, "
             L"cluster_size=%u, mft_start_cluster=%llu, mft_start_sector=%llu",
             sector_size_, sectors_per_cluster_, cluster_size_,
             boot->mft_start_cluster, mft_start_sector_);

    if (boot->clusters_per_mft_record > 0) {
        mft_record_size_ = cluster_size_ * boot->clusters_per_mft_record;
    } else {
        mft_record_size_ = 1u << (-boot->clusters_per_mft_record);
    }

    LOG_FMT(L"[MftParser] mft_record_size=%u, clusters_per_mft_record=%d",
             mft_record_size_, boot->clusters_per_mft_record);

    // Read MFT entry 0 ($MFT) to determine actual MFT size
    uint32_t sectors_per_record = mft_record_size_ / sector_size_;
    AlignedBuffer mft_buf(mft_record_size_, sector_size_);
    if (reader.read_sectors(mft_start_sector_, sectors_per_record, mft_buf)) {
        uint8_t* raw = mft_buf.data();
        apply_usa_fixup(raw, mft_record_size_, sector_size_);

        auto* hdr = reinterpret_cast<const MftRecordHeader*>(raw);
        if (hdr->signature == 0x454C4946) {  // "FILE"
            uint32_t offset = hdr->attribute_offset;
            while (offset + sizeof(AttributeHeader) < mft_record_size_) {
                auto* attr = reinterpret_cast<const AttributeHeader*>(raw + offset);
                if (attr->type == 0xFFFFFFFF || attr->length == 0) break;
                if (attr->type == ATTR_DATA && attr->non_resident == 1) {
                    uint64_t mft_size = *reinterpret_cast<const uint64_t*>(raw + offset + 0x30);
                    mft_entry_count_ = mft_size / mft_record_size_;
                    break;
                }
                offset += attr->length;
            }
        }
    }

    if (mft_entry_count_ == 0) {
        // Fallback: limit to 256K entries (256MB of MFT)
        mft_entry_count_ = 256 * 1024;
    }

    LOG_FMT(L"[MftParser] MFT entry count: %llu", mft_entry_count_);

    return true;
}

bool MftParser::enumerate_mft(SectorReader& reader,
                              std::function<void(RecoverableFile&&)> callback,
                              bool include_deleted,
                              std::function<bool()> should_stop) {
    uint32_t sectors_per_record = mft_record_size_ / sector_size_;

    // Batch read optimization: read multiple MFT records at once
    const uint32_t BATCH_RECORDS = 64;  // Read 64 records per I/O (64KB for 1KB records)
    const uint32_t BATCH_SECTORS = BATCH_RECORDS * sectors_per_record;

    AlignedBuffer batch_buf(BATCH_SECTORS * sector_size_, sector_size_);
    AlignedBuffer single_buf(mft_record_size_, sector_size_);

    LOG_FMT(L"[MftParser] enumerate_mft: entry_count=%llu, sectors_per_record=%u, mft_start=%llu, batch=%u records",
             mft_entry_count_, sectors_per_record, mft_start_sector_, BATCH_RECORDS);

    uint64_t file_count = 0;
    uint64_t dir_count = 0;
    uint64_t parse_fail_count = 0;

    for (uint64_t batch_start = 0; batch_start < mft_entry_count_; batch_start += BATCH_RECORDS) {
        if (should_stop && should_stop()) {
            LOG_FMT(L"[MftParser] Stop requested at batch %llu", batch_start);
            break;
        }

        uint64_t batch_end = (std::min)(batch_start + BATCH_RECORDS, mft_entry_count_);
        uint64_t batch_sector = mft_start_sector_ + batch_start * sectors_per_record;
        uint32_t batch_count = static_cast<uint32_t>((batch_end - batch_start) * sectors_per_record);

        // Try batch read first
        if (!reader.read_sectors(batch_sector, batch_count, batch_buf)) {
            // Fall back to single record reads on batch failure
            for (uint64_t i = batch_start; i < batch_end; ++i) {
                if (should_stop && should_stop()) break;
                uint64_t sector = mft_start_sector_ + i * sectors_per_record;
                if (!reader.read_sectors(sector, sectors_per_record, single_buf)) {
                    parse_fail_count++;
                    continue;
                }
                process_mft_record(single_buf.data(), i, include_deleted, callback,
                                   file_count, dir_count, parse_fail_count);
            }
            continue;
        }

        // Process each record in the batch
        for (uint64_t i = batch_start; i < batch_end; ++i) {
            if (should_stop && should_stop()) break;

            uint32_t offset_in_batch = static_cast<uint32_t>((i - batch_start) * mft_record_size_);
            uint8_t* record_data = batch_buf.data() + offset_in_batch;

            process_mft_record(record_data, i, include_deleted, callback,
                               file_count, dir_count, parse_fail_count);
        }
    }

    LOG_FMT(L"[MftParser] enumerate_mft done: files=%llu, dirs=%llu, parse_fails=%llu",
             file_count, dir_count, parse_fail_count);
    return true;
}

void MftParser::process_mft_record(uint8_t* data, uint64_t entry_index, bool include_deleted,
                                   std::function<void(RecoverableFile&&)> callback,
                                   uint64_t& file_count, uint64_t& dir_count, uint64_t& parse_fail_count) {
    apply_usa_fixup(data, mft_record_size_, sector_size_);

    auto* hdr = reinterpret_cast<const MftRecordHeader*>(data);
    if (hdr->signature != 0x454C4946) return;  // "FILE"

    // Skip directory entries
    if (hdr->flags & MFT_FLAG_DIRECTORY) {
        dir_count++;
        return;
    }

    bool is_deleted = !(hdr->flags & MFT_FLAG_IN_USE);
    if (is_deleted && !include_deleted) return;

    RecoverableFile file{};
    file.mft_id = entry_index;
    if (parse_mft_record(data, mft_record_size_, file, is_deleted)) {
        file.file_type = detect_file_type(file.file_name);
        file.is_corrupted = is_deleted;
        callback(std::move(file));
        file_count++;
    } else {
        parse_fail_count++;
    }
}

bool MftParser::parse_mft_record(const uint8_t* data, uint32_t size,
                                 RecoverableFile& file, bool& is_deleted) {
    auto* hdr = reinterpret_cast<const MftRecordHeader*>(data);
    uint32_t offset = hdr->attribute_offset;

    while (offset + sizeof(AttributeHeader) < size) {
        auto* attr = reinterpret_cast<const AttributeHeader*>(data + offset);
        if (attr->type == 0xFFFFFFFF || attr->length == 0) break;

        if (attr->type == ATTR_FILE_NAME && attr->non_resident == 0) {
            uint16_t content_off = *reinterpret_cast<const uint16_t*>(data + offset + 0x14);
            uint32_t content_size = *reinterpret_cast<const uint32_t*>(data + offset + 0x10);
            uint32_t abs_content = offset + content_off;
            // FILE_NAME content: name_length at +0x40 (1 byte), name at +0x42 (UTF-16LE)
            if (abs_content + 0x42 <= offset + attr->length && content_size >= 0x42) {
                uint8_t name_len = data[abs_content + 0x40];
                if (name_len > 0 && abs_content + 0x42 + name_len * 2 <= offset + attr->length) {
                    file.file_name.assign(
                        reinterpret_cast<const wchar_t*>(data + abs_content + 0x42), name_len);
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