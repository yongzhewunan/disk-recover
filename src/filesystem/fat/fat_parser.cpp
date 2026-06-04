#include "fat_parser.hpp"
#include "../../common/logger.hpp"
#include <cstring>
#include <algorithm>

namespace disk_recover::fat {

FileType FatParser::detect_file_type(const std::wstring& filename) {
    size_t dot = filename.rfind(L'.');
    if (dot == std::wstring::npos || dot + 1 >= filename.size()) return FileType::Unknown;

    std::wstring ext = filename.substr(dot + 1);
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

bool FatParser::parse_boot_sector(SectorReader& reader, uint64_t partition_start) {
    partition_start_ = partition_start;
    AlignedBuffer buf(reader.sector_size(), reader.sector_size());
    if (!reader.read_sectors(partition_start, 1, buf)) {
        LOG_FMT(L"[FatParser] Failed to read boot sector at %llu", partition_start);
        return false;
    }

    auto* boot = reinterpret_cast<const FatBootSector*>(buf.data());
    if (boot->bytes_per_sector == 0 || boot->sectors_per_cluster == 0) {
        LOG_FMT(L"[FatParser] Invalid boot: bytes_per_sector=%u, sectors_per_cluster=%u",
                 boot->bytes_per_sector, boot->sectors_per_cluster);
        return false;
    }

    bytes_per_sector_ = boot->bytes_per_sector;
    sectors_per_cluster_ = boot->sectors_per_cluster;
    cluster_size_ = bytes_per_sector_ * sectors_per_cluster_;
    reserved_sectors_ = boot->reserved_sectors;
    fat_count_ = boot->fat_count;
    root_entry_count_ = boot->root_entry_count;

    uint32_t total_sectors = boot->total_sectors_16;
    if (total_sectors == 0) total_sectors = boot->total_sectors_32;

    root_dir_sector_ = partition_start_ + reserved_sectors_ + fat_count_ * boot->sectors_per_fat_16;
    root_dir_sectors_ = ((root_entry_count_ * 32) + bytes_per_sector_ - 1) / bytes_per_sector_;

    if (boot->sectors_per_fat_16 != 0) {
        sectors_per_fat_ = boot->sectors_per_fat_16;
        data_start_sector_ = root_dir_sector_ + root_dir_sectors_ - partition_start_;
    } else {
        sectors_per_fat_ = boot->ext.fat32_ext.sectors_per_fat_32;
        root_cluster_ = boot->ext.fat32_ext.root_cluster;
        data_start_sector_ = reserved_sectors_ + fat_count_ * sectors_per_fat_;
    }
    data_start_sector_ += partition_start_;

    uint32_t data_sectors = total_sectors - (data_start_sector_ - partition_start_);
    total_clusters_ = data_sectors / sectors_per_cluster_;

    if (total_clusters_ < 4085) fat_type_ = FatType::Fat12;
    else if (total_clusters_ < 65525) fat_type_ = FatType::Fat16;
    else fat_type_ = FatType::Fat32;

    if (fat_type_ == FatType::Fat32) {
        root_dir_sector_ = cluster_to_sector(root_cluster_);
    }

    LOG_FMT(L"[FatParser] Parsed: type=%d, bytes_per_sector=%u, sectors_per_cluster=%u, "
             L"reserved=%u, fat_count=%u, sectors_per_fat=%u, root_cluster=%u, "
             L"root_dir_sector=%u, data_start=%u, total_clusters=%u",
             static_cast<int>(fat_type_), bytes_per_sector_, sectors_per_cluster_,
             reserved_sectors_, fat_count_, sectors_per_fat_, root_cluster_,
             root_dir_sector_, data_start_sector_, total_clusters_);

    return true;
}

uint32_t FatParser::cluster_to_sector(uint32_t cluster) const {
    return partition_start_ + data_start_sector_ - partition_start_
           + (cluster - 2) * sectors_per_cluster_;
}

void FatParser::cache_fat_sector(SectorReader& reader, uint64_t sector) {
    // Check if already cached
    if (fat_cache_.find(sector) != fat_cache_.end()) {
        // Move to front of LRU
        auto it = std::find(fat_cache_order_.begin(), fat_cache_order_.end(), sector);
        if (it != fat_cache_order_.end()) {
            fat_cache_order_.erase(it);
            fat_cache_order_.insert(fat_cache_order_.begin(), sector);
        }
        return;
    }

    // Evict oldest if cache is full
    if (fat_cache_.size() >= FAT_CACHE_SIZE) {
        uint64_t oldest = fat_cache_order_.back();
        fat_cache_order_.pop_back();
        fat_cache_.erase(oldest);
    }

    // Read and cache the sector
    AlignedBuffer buf(bytes_per_sector_, bytes_per_sector_);
    if (reader.read_sectors(sector, 1, buf)) {
        FatCacheEntry entry;
        entry.data.assign(buf.data(), buf.data() + bytes_per_sector_);
        entry.sector = sector;
        fat_cache_[sector] = std::move(entry);
        fat_cache_order_.insert(fat_cache_order_.begin(), sector);
    }
}

const uint8_t* FatParser::get_cached_fat_sector(uint64_t sector) {
    auto it = fat_cache_.find(sector);
    if (it != fat_cache_.end()) {
        return it->second.data.data();
    }
    return nullptr;
}

uint32_t FatParser::get_fat_entry(SectorReader& reader, uint32_t cluster) {
    if (fat_type_ == FatType::Fat12) {
        uint32_t fat_offset = cluster + cluster / 2;
        uint32_t fat_sector = partition_start_ + reserved_sectors_ + fat_offset / bytes_per_sector_;

        cache_fat_sector(reader, fat_sector);
        const uint8_t* data = get_cached_fat_sector(fat_sector);
        if (!data) return 0;

        uint16_t entry = *reinterpret_cast<const uint16_t*>(data + fat_offset % bytes_per_sector_);
        if (cluster & 1) entry >>= 4;
        else entry &= 0x0FFF;
        return entry;
    } else if (fat_type_ == FatType::Fat16) {
        uint32_t fat_offset = cluster * 2;
        uint32_t fat_sector = partition_start_ + reserved_sectors_ + fat_offset / bytes_per_sector_;

        cache_fat_sector(reader, fat_sector);
        const uint8_t* data = get_cached_fat_sector(fat_sector);
        if (!data) return 0;

        return *reinterpret_cast<const uint16_t*>(data + fat_offset % bytes_per_sector_);
    } else {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = partition_start_ + reserved_sectors_ + fat_offset / bytes_per_sector_;

        cache_fat_sector(reader, fat_sector);
        const uint8_t* data = get_cached_fat_sector(fat_sector);
        if (!data) return 0;

        return *reinterpret_cast<const uint32_t*>(data + fat_offset % bytes_per_sector_) & 0x0FFFFFFF;
    }
}

bool FatParser::is_end_of_chain(uint32_t entry) const {
    switch (fat_type_) {
    case FatType::Fat12: return entry >= FAT12_EOC;
    case FatType::Fat16: return entry >= FAT16_EOC;
    case FatType::Fat32: return entry >= FAT32_EOC;
    default: return true;
    }
}

bool FatParser::is_bad_cluster(uint32_t entry) const {
    switch (fat_type_) {
    case FatType::Fat12: return entry == FAT12_BAD;
    case FatType::Fat16: return entry == FAT16_BAD;
    case FatType::Fat32: return entry == FAT32_BAD;
    default: return false;
    }
}

bool FatParser::read_cluster_chain(SectorReader& reader, uint32_t start_cluster,
                                   std::vector<DiskExtent>& extents) {
    uint32_t cluster = start_cluster;
    uint64_t prev_sector = 0;
    uint64_t run_length = 0;

    while (!is_end_of_chain(cluster) && cluster >= 2) {
        if (is_bad_cluster(cluster)) break;

        uint32_t sector = cluster_to_sector(cluster);
        if (sector == prev_sector + run_length) {
            run_length += sectors_per_cluster_;
        } else {
            if (run_length > 0) {
                extents.push_back({prev_sector, run_length});
            }
            prev_sector = sector;
            run_length = sectors_per_cluster_;
        }
        cluster = get_fat_entry(reader, cluster);
    }

    if (run_length > 0) {
        extents.push_back({prev_sector, run_length});
    }
    return !extents.empty();
}

bool FatParser::enumerate_root_dir(SectorReader& reader,
                                   std::function<void(RecoverableFile&&)> callback,
                                   bool include_deleted,
                                   std::function<bool()> should_stop) {
    if (fat_type_ == FatType::Fat32) {
        return enumerate_directory(reader, root_cluster_, callback, include_deleted, 0, should_stop);
    }

    // FAT12/16: root directory is at fixed sectors, not in cluster chain
    uint32_t dir_sector = root_dir_sector_;
    uint32_t dir_sectors = root_dir_sectors_;

    AlignedBuffer buf(dir_sectors * bytes_per_sector_, bytes_per_sector_);
    if (!reader.read_sectors(dir_sector, dir_sectors, buf)) return false;

    auto* entries = reinterpret_cast<const uint8_t*>(buf.data());
    uint32_t entry_count = (dir_sectors * bytes_per_sector_) / 32;

    // LFN assembly state
    std::wstring lfn_name;
    bool lfn_valid = false;

    for (uint32_t i = 0; i < entry_count; ++i) {
        auto* entry = entries + i * 32;
        if (entry[0] == 0x00) break;

        // LFN entry
        if (entry[11] == 0x0F) {
            uint8_t seq = entry[0] & 0x3F;
            if (seq == 0) { lfn_valid = false; continue; }

            // Extract up to 13 wchar_t from LFN entry
            wchar_t lfn_part[13];
            int pos = 0;
            const uint8_t* name_offsets[] = {entry+1, entry+3, entry+5, entry+7, entry+9,
                                              entry+14, entry+16, entry+18, entry+20, entry+22,
                                              entry+24, entry+28, entry+30};
            for (int k = 0; k < 13; ++k) {
                uint16_t ch = *reinterpret_cast<const uint16_t*>(name_offsets[k]);
                if (ch == 0x0000 || ch == 0xFFFF) break;
                lfn_part[pos++] = static_cast<wchar_t>(ch);
            }

            if (seq == 1) {
                // Last LFN entry (stored first) — start of name
                lfn_name.assign(lfn_part, pos);
                lfn_valid = true;
            } else if (lfn_valid) {
                // Prepend this part
                lfn_name = std::wstring(lfn_part, pos) + lfn_name;
            }
            continue;
        }

        if (entry[0] == 0xE5) {
            lfn_valid = false;
            lfn_name.clear();
            if (!include_deleted) continue;
        }

        // Regular entry — build name
        std::wstring file_name;
        if (lfn_valid && !lfn_name.empty()) {
            file_name = lfn_name;
        } else {
            char name[13] = {};
            memcpy(name, entry, 8);
            int pos = 8;
            while (pos > 0 && name[pos-1] == ' ') pos--;
            if (entry[8] != ' ') {
                name[pos++] = '.';
                memcpy(name + pos, entry + 8, 3);
                pos += 3;
            }
            while (pos > 0 && name[pos-1] == ' ') pos--;
            name[pos] = '\0';
            wchar_t wname[13];
            for (int j = 0; j <= pos; ++j) wname[j] = static_cast<wchar_t>(name[j]);
            file_name = wname;
        }
        lfn_valid = false;
        lfn_name.clear();

        uint16_t cluster_hi = *reinterpret_cast<const uint16_t*>(entry + 20);
        uint16_t cluster_lo = *reinterpret_cast<const uint16_t*>(entry + 26);
        uint32_t cluster = (cluster_hi << 16) | cluster_lo;
        uint8_t attr = entry[11];
        uint32_t file_size = *reinterpret_cast<const uint32_t*>(entry + 28);

        // Directory — recurse into it
        if ((attr & 0x10) && cluster >= 2 && !(attr & 0x08)) {
            // Skip . and .. entries
            if (file_name == L"." || file_name == L"..") continue;
            enumerate_directory(reader, cluster, callback, include_deleted, 1, should_stop);
            continue;
        }

        // Regular file
        if (cluster >= 2 && file_size > 0) {
            RecoverableFile file{};
            file.file_name = file_name;
            file.corruption_level = (entry[0] == 0xE5) ? CorruptionLevel::Minor : CorruptionLevel::None;
            file.file_size = file_size;
            file.file_type = detect_file_type(file.file_name);
            read_cluster_chain(reader, cluster, file.fragments);
            callback(std::move(file));
        }
    }
    return true;
}

bool FatParser::enumerate_directory(SectorReader& reader, uint32_t start_cluster,
                                   const std::function<void(RecoverableFile&&)>& callback,
                                   bool include_deleted, int depth,
                                   const std::function<bool()>& should_stop) {
    if (depth > 10) return true;  // Max recursion depth

    std::vector<DiskExtent> extents;
    if (!read_cluster_chain(reader, start_cluster, extents)) return true;

    // Read all clusters into a contiguous buffer
    uint32_t dir_sectors = 0;
    for (auto& ext : extents) dir_sectors += static_cast<uint32_t>(ext.sector_count);

    uint32_t dir_bytes = dir_sectors * bytes_per_sector_;
    AlignedBuffer buf(dir_bytes, bytes_per_sector_);
    uint64_t offset = 0;
    for (auto& ext : extents) {
        AlignedBuffer ext_buf(static_cast<size_t>(ext.sector_count) * bytes_per_sector_, bytes_per_sector_);
        if (!reader.read_sectors(ext.start_sector, static_cast<uint32_t>(ext.sector_count), ext_buf)) break;
        memcpy(buf.data() + offset, ext_buf.data(), static_cast<size_t>(ext.sector_count) * bytes_per_sector_);
        offset += ext.sector_count * bytes_per_sector_;
    }

    auto* entries = reinterpret_cast<const uint8_t*>(buf.data());
    uint32_t entry_count = dir_bytes / 32;

    // LFN assembly state
    std::wstring lfn_name;
    bool lfn_valid = false;

    for (uint32_t i = 0; i < entry_count; ++i) {
        auto* entry = entries + i * 32;
        if (entry[0] == 0x00) break;

        // LFN entry
        if (entry[11] == 0x0F) {
            uint8_t seq = entry[0] & 0x3F;
            if (seq == 0) { lfn_valid = false; continue; }

            wchar_t lfn_part[13];
            int pos = 0;
            const uint8_t* name_offsets[] = {entry+1, entry+3, entry+5, entry+7, entry+9,
                                              entry+14, entry+16, entry+18, entry+20, entry+22,
                                              entry+24, entry+28, entry+30};
            for (int k = 0; k < 13; ++k) {
                uint16_t ch = *reinterpret_cast<const uint16_t*>(name_offsets[k]);
                if (ch == 0x0000 || ch == 0xFFFF) break;
                lfn_part[pos++] = static_cast<wchar_t>(ch);
            }

            if (seq == 1) {
                lfn_name.assign(lfn_part, pos);
                lfn_valid = true;
            } else if (lfn_valid) {
                lfn_name = std::wstring(lfn_part, pos) + lfn_name;
            }
            continue;
        }

        if (entry[0] == 0xE5) {
            lfn_valid = false;
            lfn_name.clear();
            if (!include_deleted) continue;
        }

        // Build name
        std::wstring file_name;
        if (lfn_valid && !lfn_name.empty()) {
            file_name = lfn_name;
        } else {
            char name[13] = {};
            memcpy(name, entry, 8);
            int pos = 8;
            while (pos > 0 && name[pos-1] == ' ') pos--;
            if (entry[8] != ' ') {
                name[pos++] = '.';
                memcpy(name + pos, entry + 8, 3);
                pos += 3;
            }
            while (pos > 0 && name[pos-1] == ' ') pos--;
            name[pos] = '\0';
            wchar_t wname[13];
            for (int j = 0; j <= pos; ++j) wname[j] = static_cast<wchar_t>(name[j]);
            file_name = wname;
        }
        lfn_valid = false;
        lfn_name.clear();

        uint16_t cluster_hi = *reinterpret_cast<const uint16_t*>(entry + 20);
        uint16_t cluster_lo = *reinterpret_cast<const uint16_t*>(entry + 26);
        uint32_t cluster = (cluster_hi << 16) | cluster_lo;
        uint8_t attr = entry[11];
        uint32_t file_size = *reinterpret_cast<const uint32_t*>(entry + 28);

        // Skip . and .. entries
        if (file_name == L"." || file_name == L"..") continue;

        // Directory — recurse
        if ((attr & 0x10) && cluster >= 2 && !(attr & 0x08)) {
            enumerate_directory(reader, cluster, callback, include_deleted, depth + 1, should_stop);
            continue;
        }

        // Regular file
        if (cluster >= 2 && file_size > 0) {
            RecoverableFile file{};
            file.file_name = file_name;
            file.corruption_level = (entry[0] == 0xE5) ? CorruptionLevel::Minor : CorruptionLevel::None;
            file.file_size = file_size;
            file.file_type = detect_file_type(file.file_name);
            read_cluster_chain(reader, cluster, file.fragments);
            callback(std::move(file));
        }
    }
    return true;
}

} // namespace disk_recover::fat
