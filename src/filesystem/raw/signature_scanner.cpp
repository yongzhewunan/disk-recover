#define NOMINMAX
#include "signature_scanner.hpp"
#include "file_signatures.hpp"
#include "../common/logger.hpp"
#include <algorithm>

namespace disk_recover {

// Parse actual file size from file header bytes
static uint64_t parse_file_size(FileType file_type, const uint8_t* data, size_t data_len) {
    if (data_len < 16) return 0;

    switch (file_type) {
    case FileType::Image:
        // JPEG: scan for SOF marker to get dimensions, then estimate size
        if (data[0] == 0xFF && data[1] == 0xD8) {
            size_t pos = 2;
            while (pos + 9 < data_len) {
                if (data[pos] != 0xFF) break;
                uint8_t marker = data[pos + 1];
                if (marker == 0xDA) break;  // SOS - start of scan
                if (marker == 0xC0 || marker == 0xC2) {
                    int height = (data[pos + 5] << 8) | data[pos + 6];
                    int width = (data[pos + 7] << 8) | data[pos + 8];
                    if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                        // JPEG ~10:1 compression ratio
                        uint64_t sz = static_cast<uint64_t>(width) * height * 3 / 10;
                        return std::min(sz, 50ULL * 1024 * 1024);  // Cap at 50MB
                    }
                    break;
                }
                uint16_t seg_len = (data[pos + 2] << 8) | data[pos + 3];
                pos += 2 + seg_len;
            }
        }
        // PNG: width/height in IHDR chunk
        if (data_len >= 24 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
            int width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
            int height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
            if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                uint64_t sz = static_cast<uint64_t>(width) * height * 4 / 2;
                return std::min(sz, 50ULL * 1024 * 1024);
            }
        }
        // BMP: validate header thoroughly to avoid false positives
        if (data[0] == 'B' && data[1] == 'M' && data_len >= 30) {
            uint32_t file_sz = *reinterpret_cast<const uint32_t*>(data + 2);
            uint32_t dib_sz = *reinterpret_cast<const uint32_t*>(data + 14);
            int32_t width = *reinterpret_cast<const int32_t*>(data + 18);
            int32_t height = *reinterpret_cast<const int32_t*>(data + 22);
            uint16_t planes = *reinterpret_cast<const uint16_t*>(data + 26);
            uint16_t bpp = *reinterpret_cast<const uint16_t*>(data + 28);
            // Validate: planes must be 1, bpp must be 1/4/8/16/24/32
            if (planes != 1 || (bpp != 1 && bpp != 4 && bpp != 8 &&
                                bpp != 16 && bpp != 24 && bpp != 32)) {
                return 0;  // Not a real BMP
            }
            // Validate dimensions
            if (width <= 0 || height == 0 || labs(height) > 65535 || width > 65535) {
                return 0;  // Invalid dimensions
            }
            // Validate DIB header size (must be 12, 40, 52, 56, 108, or 124)
            if (dib_sz != 12 && dib_sz != 40 && dib_sz != 52 &&
                dib_sz != 56 && dib_sz != 108 && dib_sz != 124) {
                return 0;  // Invalid DIB header
            }
            // Use header file size if it looks reasonable
            uint64_t calc_sz = static_cast<uint64_t>(width) * labs(height) * ((bpp + 7) / 8) + 54;
            // Cap at 50MB - real BMP files are rarely larger
            if (file_sz > 0 && file_sz <= 50 * 1024 * 1024) {
                return file_sz;
            }
            return std::min(calc_sz, 50ULL * 1024 * 1024);
        }
        // WebP: RIFF size
        if (data_len >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
            uint32_t riff_sz = *reinterpret_cast<const uint32_t*>(data + 4);
            if (riff_sz > 8 && riff_sz <= 50 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        break;

    case FileType::Video:
        // AVI: RIFF size
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data_len >= 8) {
            uint32_t riff_sz = *reinterpret_cast<const uint32_t*>(data + 4);
            if (riff_sz > 8 && riff_sz <= 2ULL * 1024 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        // MP4/MOV: ftyp box
        if (data_len >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
            uint32_t ftyp_sz = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            if (ftyp_sz >= 8 && ftyp_sz <= 1024) {
                // Valid ftyp, but total file size unknown from header
                return 100 * 1024 * 1024;  // 100MB estimate
            }
        }
        // MKV/WebM: EBML header
        if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) {
            return 100 * 1024 * 1024;  // 100MB estimate
        }
        // FLV
        if (data[0] == 'F' && data[1] == 'L' && data[2] == 'V') {
            return 50 * 1024 * 1024;  // 50MB estimate
        }
        break;

    default:
        break;
    }
    return 0;
}

void SignatureScanner::scan(SectorReader& reader, const ScanConfig& config,
                            std::function<void(RecoverableFile&&)> on_file_found,
                            std::function<void(const ScanProgress&)> on_progress) {
    ScanProgress progress{};
    progress.total_sectors = config.end_sector - config.start_sector;

    LOG_FMT(L"[SigScanner] Starting RAW scan: sector %llu to %llu, total=%llu",
             config.start_sector, config.end_sector, progress.total_sectors);

    const uint32_t sector_size = reader.sector_size();
    const uint32_t sectors_per_chunk = 64;
    AlignedBuffer buf(sectors_per_chunk * sector_size, sector_size);

    if (buf.empty()) {
        LOG_MSG(L"[SigScanner] Failed to allocate scan buffer");
        progress.is_complete = true;
        if (on_progress) on_progress(progress);
        return;
    }

    uint32_t sig_count = 0;
    uint64_t last_report_sector = config.start_sector;
    uint64_t claimed_end_sector = 0;  // Sectors already claimed by a previous file

    for (uint64_t sector = config.start_sector;
         sector < config.end_sector;
         sector += sectors_per_chunk) {

        if (config.should_stop && config.should_stop()) {
            LOG_MSG(L"[SigScanner] Stop requested, exiting scan loop");
            break;
        }

        uint32_t count = std::min(sectors_per_chunk,
            static_cast<uint32_t>(config.end_sector - sector));

        if (!reader.read_sectors_checked(sector, count, buf)) {
            progress.sectors_scanned += count;
            progress.bad_sectors_hit++;
            if (sector - last_report_sector >= 20480) {
                last_report_sector = sector;
                if (on_progress) on_progress(progress);
            }
            continue;
        }

        for (uint32_t offset = 0; offset < count * sector_size;
             offset += sector_size) {
            uint64_t cur_sector = sector + offset / sector_size;

            // Skip sectors already claimed by a previous file
            if (cur_sector < claimed_end_sector) continue;

            auto sig = FileSignatures::match(buf.data() + offset, sector_size);
            if (!sig) continue;

            if (sig->file_type == FileType::Image && !config.scan_images) continue;
            if (sig->file_type == FileType::Video && !config.scan_videos) continue;

            sig_count++;
            RecoverableFile file{};
            if (try_recover_file(reader, cur_sector, *sig, file)) {
                progress.files_found++;

                // Mark sectors as claimed so we don't create duplicate entries
                uint64_t file_end = cur_sector;
                for (const auto& frag : file.fragments) {
                    file_end = std::max(file_end, frag.start_sector + frag.sector_count);
                }
                claimed_end_sector = file_end;

                if (sig_count <= 10) {
                    LOG_FMT(L"[SigScanner] Found %s at sector %llu, size=%llu",
                             sig->description.c_str(), cur_sector, file.file_size);
                }
                if (on_file_found) on_file_found(std::move(file));
            }
        }

        progress.sectors_scanned += count;
        if (sector - last_report_sector >= 20480) {
            last_report_sector = sector;
            if (on_progress) on_progress(progress);
        }
    }

    LOG_FMT(L"[SigScanner] RAW scan complete: signatures=%u, files_found=%llu, sectors_scanned=%llu",
             sig_count, progress.files_found, progress.sectors_scanned);

    progress.is_complete = true;
    if (on_progress) on_progress(progress);
}

bool SignatureScanner::try_recover_file(SectorReader& reader, uint64_t start_sector,
                                         const FileSignature& sig, RecoverableFile& file) {
    const uint32_t sector_size = reader.sector_size();

    file.file_type = sig.file_type;
    file.file_name = sig.description + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + sig.extension;
    file.is_corrupted = true;

    // Read a larger chunk starting from this sector to parse header for actual size
    // Read up to 4 sectors (2KB) for header parsing
    const uint32_t HEADER_SECTORS = 4;
    AlignedBuffer headerBuf(HEADER_SECTORS * sector_size, sector_size);
    bool header_ok = reader.read_sectors_checked(start_sector, HEADER_SECTORS, headerBuf);

    uint64_t estimated_size = 0;
    if (header_ok) {
        estimated_size = parse_file_size(sig.file_type, headerBuf.data(),
                                          HEADER_SECTORS * sector_size);
    }

    // If we couldn't parse size from header, use type-based defaults
    // with reasonable caps to avoid absurd sizes
    if (estimated_size == 0) {
        if (sig.file_type == FileType::Video) {
            estimated_size = 10 * 1024 * 1024;  // 10MB default for video
        } else if (sig.file_type == FileType::Image) {
            estimated_size = 500 * 1024;         // 500KB default for image
        } else {
            estimated_size = 1 * 1024 * 1024;   // 1MB default
        }
    }

    // Cap estimated sizes to reasonable limits
    if (sig.file_type == FileType::Image && estimated_size > 50 * 1024 * 1024) {
        estimated_size = 50 * 1024 * 1024;  // Max 50MB for images
    }
    if (sig.file_type == FileType::Video && estimated_size > 2ULL * 1024 * 1024 * 1024) {
        estimated_size = 2ULL * 1024 * 1024 * 1024;  // Max 2GB for video
    }

    // Calculate sector count from estimated size
    uint64_t file_sectors = (estimated_size + sector_size - 1) / sector_size;
    if (file_sectors < 1) file_sectors = 1;

    // For video files, try to read ahead and find the actual end marker
    // by scanning for the next file signature
    if (sig.file_type == FileType::Video && file_sectors > 64) {
        // Scan in large jumps looking for the next signature or end of data
        const uint32_t PROBE_CHUNK = 64;  // 32KB jumps
        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);

        uint64_t max_sector = start_sector + file_sectors;
        // Cap the search at a reasonable limit (1GB)
        uint64_t search_limit = start_sector + (1024 * 1024 * 1024 / sector_size);

        for (uint64_t probe = start_sector + file_sectors / 2;
             probe < max_sector && probe < search_limit;
             probe += PROBE_CHUNK) {

            if (!reader.read_sectors_checked(probe, PROBE_CHUNK, probeBuf)) {
                // Bad sector - this might be the end
                file_sectors = probe - start_sector;
                break;
            }

            // Check if any sector in this chunk starts a new file
            bool found_next = false;
            for (uint32_t off = 0; off < PROBE_CHUNK * sector_size; off += sector_size) {
                auto next_sig = FileSignatures::match(probeBuf.data() + off, sector_size);
                if (next_sig) {
                    // Found another file header - our file ends before this sector
                    uint64_t end_sector = probe + off / sector_size;
                    if (end_sector > start_sector) {
                        file_sectors = end_sector - start_sector;
                        found_next = true;
                    }
                    break;
                }
            }
            if (found_next) break;
        }
    }

    file.file_size = file_sectors * sector_size;

    // For large files, split into fragments (simulate fragmentation)
    // Real files on damaged disks may be fragmented, but for RAW carving
    // we assume contiguous allocation
    file.fragments.push_back({start_sector, file_sectors});

    return true;
}

} // namespace disk_recover