#define NOMINMAX
#include "signature_scanner.hpp"
#include "file_signatures.hpp"
#include "../common/logger.hpp"
#include <algorithm>
#include <chrono>

namespace disk_recover {

// Walk the top-level MP4/MOV atom tree to calculate total file size.
// MP4/MOV files are structured as a sequence of "atoms" (boxes):
// Each atom: [4-byte big-endian size][4-byte type][payload]
// If size==1: next 8 bytes are 64-bit extended size
// If size==0: atom extends to end of file (can't determine total size)
// Returns 0 if parsing fails (caller falls back to probe-based approach).
static uint64_t determine_mp4_size(SectorReader& reader, uint64_t start_sector, uint32_t sector_size) {
    const uint32_t READ_SECTORS = 4;  // Read 4 sectors at a time (sliding window)
    const uint64_t MAX_HEADER_SCAN = 4ULL * 1024 * 1024;  // Scan up to 4MB of atom headers
    const uint64_t ATOM_SANITY_LIMIT = 100ULL * 1024 * 1024 * 1024;  // 100GB sanity cap per atom

    AlignedBuffer buf(READ_SECTORS * sector_size, sector_size);

    uint64_t total_size = 0;       // Accumulated total file size from atoms
    uint64_t file_offset = 0;      // Current byte offset within the file (from start)
    uint64_t buf_start = 0;        // File offset of the first byte in buf
    uint64_t buf_end = 0;          // File offset one past the last byte in buf
    bool buf_valid = false;

    while (file_offset < MAX_HEADER_SCAN) {
        // Ensure the buffer covers the atom header at file_offset.
        // We need at least 8 bytes for a standard atom header.
        if (!buf_valid || file_offset + 8 > buf_end) {
            uint64_t read_offset = file_offset;
            // Align to sector boundary for reading
            uint64_t sector_num = start_sector + read_offset / sector_size;
            if (!reader.read_sectors_checked(sector_num, READ_SECTORS, buf)) {
                return 0;  // Read failure - can't determine size
            }
            buf_start = (sector_num - start_sector) * sector_size;
            buf_end = buf_start + READ_SECTORS * sector_size;
            buf_valid = true;

            // If after reading, the atom header still isn't covered, bail out
            if (file_offset + 8 > buf_end) {
                return 0;
            }
        }

        // Parse atom header at file_offset
        size_t buf_pos = static_cast<size_t>(file_offset - buf_start);
        const uint8_t* hdr = buf.data() + buf_pos;

        uint32_t atom_size32 = (static_cast<uint32_t>(hdr[0]) << 24) |
                                (static_cast<uint32_t>(hdr[1]) << 16) |
                                (static_cast<uint32_t>(hdr[2]) << 8) |
                                static_cast<uint32_t>(hdr[3]);

        uint64_t atom_size = atom_size32;

        if (atom_size32 == 1) {
            // 64-bit extended size: need 16 bytes total for the header
            if (file_offset + 16 > buf_end) {
                // Re-read to cover the extended header
                uint64_t sector_num = start_sector + file_offset / sector_size;
                if (!reader.read_sectors_checked(sector_num, READ_SECTORS, buf)) {
                    return 0;
                }
                buf_start = (sector_num - start_sector) * sector_size;
                buf_end = buf_start + READ_SECTORS * sector_size;
                buf_valid = true;

                if (file_offset + 16 > buf_end) {
                    return 0;  // Can't read extended size
                }
                hdr = buf.data() + static_cast<size_t>(file_offset - buf_start);
            }
            atom_size = (static_cast<uint64_t>(hdr[8]) << 56) |
                        (static_cast<uint64_t>(hdr[9]) << 48) |
                        (static_cast<uint64_t>(hdr[10]) << 40) |
                        (static_cast<uint64_t>(hdr[11]) << 32) |
                        (static_cast<uint64_t>(hdr[12]) << 24) |
                        (static_cast<uint64_t>(hdr[13]) << 16) |
                        (static_cast<uint64_t>(hdr[14]) << 8) |
                        static_cast<uint64_t>(hdr[15]);
        } else if (atom_size32 == 0) {
            // Atom extends to end of file - we can't determine total size
            return 0;
        }

        // Sanity check: individual atom shouldn't exceed 100GB
        if (atom_size > ATOM_SANITY_LIMIT) {
            return 0;
        }

        // Minimum atom size is 8 bytes (header only), or 16 for extended size
        uint64_t min_size = (atom_size32 == 1) ? 16 : 8;
        if (atom_size < min_size) {
            return 0;  // Invalid atom
        }

        total_size += atom_size;
        file_offset += atom_size;

        // If atom_size is so large that file_offset exceeds our scan limit, stop
        if (file_offset > MAX_HEADER_SCAN) {
            // We've parsed enough atoms to know the file is large.
            // Return what we have so far as a lower bound.
            break;
        }
    }

    return total_size;
}

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
                // Valid ftyp but total file size requires atom tree walking
                return 0;
            }
        }
        // MKV/WebM: EBML header
        if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) {
            return 0;  // Size requires full container parsing
        }
        // FLV
        if (data[0] == 'F' && data[1] == 'L' && data[2] == 'V') {
            return 0;  // Size requires full container parsing
        }
        break;

    default:
        break;
    }
    return 0;
}

std::vector<RecoverableFile> SignatureScanner::merge_video_fragments(
    std::vector<RecoverableFile>& files, uint32_t sector_size) {

    if (files.size() <= 1) return std::move(files);

    // Sort by start sector (first fragment's start_sector)
    std::sort(files.begin(), files.end(),
        [](const RecoverableFile& a, const RecoverableFile& b) {
            if (a.fragments.empty() || b.fragments.empty()) return false;
            return a.fragments[0].start_sector < b.fragments[0].start_sector;
        });

    // Maximum gap between fragments to consider merging (1MB / sector_size)
    const uint64_t MAX_GAP_SECTORS = (1024ULL * 1024) / sector_size;

    // Helper to extract file extension (part after last '.')
    auto get_extension = [](const std::wstring& name) -> std::wstring {
        auto pos = name.rfind(L'.');
        if (pos == std::wstring::npos) return L"";
        return name.substr(pos);
    };

    std::vector<RecoverableFile> result;
    result.push_back(std::move(files[0]));

    for (size_t i = 1; i < files.size(); ++i) {
        RecoverableFile& last = result.back();
        RecoverableFile& current = files[i];

        // Check if same file type and same extension
        if (last.file_type == current.file_type &&
            get_extension(last.file_name) == get_extension(current.file_name)) {

            // Get end sector of last file
            uint64_t last_end = 0;
            for (const auto& frag : last.fragments) {
                last_end = std::max(last_end, frag.start_sector + frag.sector_count);
            }

            uint64_t current_start = current.fragments[0].start_sector;
            uint64_t gap = (current_start > last_end) ? (current_start - last_end) : 0;

            if (gap <= MAX_GAP_SECTORS) {
                // Merge: add gap as a fragment, then add current's fragments
                if (gap > 0) {
                    last.fragments.push_back({last_end, gap});
                }
                for (auto& frag : current.fragments) {
                    last.fragments.push_back(std::move(frag));
                }
                // Recalculate total size: sum all fragment sector counts
                uint64_t total_sectors = 0;
                for (const auto& frag : last.fragments) {
                    total_sectors += frag.sector_count;
                }
                last.file_size = total_sectors * sector_size;
                last.is_corrupted = true;
                continue;
            }
        }

        result.push_back(std::move(current));
    }

    return result;
}

void SignatureScanner::scan(SectorReader& reader, const ScanConfig& config,
                            std::function<void(RecoverableFile&&)> on_file_found,
                            std::function<void(const ScanProgress&)> on_progress) {
    ScanProgress progress{};
    progress.total_sectors = config.end_sector - config.start_sector;

    const uint32_t sector_size = reader.sector_size();
    const uint32_t BATCH_SECTORS = 256;

    // Compute scan start with overlap for resume
    uint64_t scan_start = config.start_sector;
    uint64_t initial_scanned = 0;

    if (config.resume_from_sector > config.start_sector) {
        if (config.resume_from_sector >= 64) {
            scan_start = std::max(config.start_sector, config.resume_from_sector - 64);
        } else {
            scan_start = config.start_sector;
        }
        uint64_t offset = config.resume_from_sector - config.start_sector;
        if (offset < 64) {
            initial_scanned = 0;
        } else {
            initial_scanned = offset - 64;
        }
    }

    progress.sectors_scanned = initial_scanned;

    LOG_FMT(L"[SigScanner] Starting RAW scan: sector %llu to %llu, total=%llu, resume_from=%llu, scan_start=%llu",
             config.start_sector, config.end_sector, progress.total_sectors,
             config.resume_from_sector, scan_start);

    AlignedBuffer batch_buf(BATCH_SECTORS * sector_size, sector_size);
    if (batch_buf.empty()) {
        LOG_MSG(L"[SigScanner] Failed to allocate scan buffer");
        progress.is_complete = true;
        if (on_progress) on_progress(progress);
        return;
    }

    uint32_t sig_count = 0;
    uint64_t claimed_end_sector = 0;
    std::vector<RecoverableFile> video_files;

    // Time + distance dual threshold for progress save
    auto last_save_time = std::chrono::steady_clock::now();
    uint64_t last_save_sector = scan_start;
    uint32_t consecutive_bad_batches = 0;

    for (uint64_t sector = scan_start; sector < config.end_sector; sector += BATCH_SECTORS) {
        if (config.should_stop && config.should_stop()) {
            LOG_MSG(L"[SigScanner] Stop requested, exiting scan loop");
            break;
        }

        uint32_t batch_count = std::min(BATCH_SECTORS, static_cast<uint32_t>(config.end_sector - sector));
        uint32_t bad_count = 0;

        bool ok = reader.read_sectors_split(sector, batch_count, batch_buf, bad_count, config.should_stop);

        if (bad_count == batch_count) {
            consecutive_bad_batches++;
            if (consecutive_bad_batches >= 4) {
                uint64_t actual_skip = std::min<uint64_t>(1024, config.end_sector - sector);
                progress.sectors_scanned += actual_skip;
                progress.bad_sectors_hit += actual_skip;
                if (actual_skip >= BATCH_SECTORS) {
                    sector += (actual_skip - BATCH_SECTORS);
                } else {
                    sector -= (BATCH_SECTORS - actual_skip);
                }
                consecutive_bad_batches = 0;
                LOG_FMT(L"[SigScanner] Adaptive skip: jumping past bad cluster to sector %llu", sector + BATCH_SECTORS);
                continue;
            }
        } else {
            consecutive_bad_batches = 0;
        }

        // Scan batch buffer for signatures
        for (uint32_t i = 0; i < batch_count; ++i) {
            const uint8_t* data = batch_buf.data() + i * sector_size;
            uint64_t cur_sector = sector + i;

            if (cur_sector < claimed_end_sector) continue;

            auto sig = FileSignatures::match(data, sector_size);
            if (!sig) continue;

            if (sig->file_type == FileType::Image && !config.scan_images) continue;
            if (sig->file_type == FileType::Video && !config.scan_videos) continue;

            sig_count++;
            RecoverableFile file{};
            if (try_recover_file(reader, cur_sector, *sig, file)) {
                progress.files_found++;

                uint64_t file_end = cur_sector;
                for (const auto& frag : file.fragments) {
                    file_end = std::max(file_end, frag.start_sector + frag.sector_count);
                }
                claimed_end_sector = file_end;

                if (sig_count <= 10) {
                    LOG_FMT(L"[SigScanner] Found %s at sector %llu, size=%llu",
                             sig->description.c_str(), cur_sector, file.file_size);
                }
                if (file.file_type == FileType::Video) {
                    video_files.push_back(std::move(file));
                } else {
                    if (on_file_found) on_file_found(std::move(file));
                }
            }
        }

        progress.sectors_scanned += batch_count;
        progress.bad_sectors_hit += bad_count;

        // Time + distance dual threshold for progress callback
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_save_time).count();
        uint64_t sectors_since_save = sector - last_save_sector;

        if ((elapsed >= 5) || (sectors_since_save >= 204800 && elapsed >= 1)) {
            last_save_time = now;
            last_save_sector = sector;
            if (on_progress) on_progress(progress);
        }
    }

    LOG_FMT(L"[SigScanner] RAW scan complete: signatures=%u, files_found=%llu, sectors_scanned=%llu",
             sig_count, progress.files_found, progress.sectors_scanned);

    auto merged = merge_video_fragments(video_files, sector_size);
    for (auto& file : merged) {
        if (on_file_found) on_file_found(std::move(file));
    }

    progress.is_complete = true;
    if (on_progress) on_progress(progress);
}

bool SignatureScanner::try_recover_file(SectorReader& reader, uint64_t start_sector,
                                         const FileSignature& sig, RecoverableFile& file) {
    const uint32_t sector_size = reader.sector_size();

    file.file_type = sig.file_type;
    file.file_name = sig.description + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + sig.extension;

    // Read a larger chunk starting from this sector to parse header for actual size
    // Read up to 4 sectors (2KB) for header parsing
    const uint32_t HEADER_SECTORS = 4;
    AlignedBuffer headerBuf(HEADER_SECTORS * sector_size, sector_size);
    bool header_ok = reader.read_sectors_checked(start_sector, HEADER_SECTORS, headerBuf);

    // If we can read the header and parse a valid size, the file is likely intact.
    // If header read fails, the file is corrupted.
    uint64_t estimated_size = 0;
    if (header_ok) {
        estimated_size = parse_file_size(sig.file_type, headerBuf.data(),
                                          HEADER_SECTORS * sector_size);
    }
    file.is_corrupted = !header_ok;

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

    // For MP4/MOV, try atom tree walking for accurate size determination
    if (sig.file_type == FileType::Video &&
        (sig.extension == L"mp4" || sig.extension == L"mov")) {
        uint64_t atom_size = determine_mp4_size(reader, start_sector, sector_size);
        if (atom_size > 0) {
            estimated_size = atom_size;
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
    if (sig.file_type == FileType::Video) {
        // Scan in large jumps looking for the next signature or end of data
        const uint32_t PROBE_CHUNK = 64;  // 32KB jumps
        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);

        // Cap the search at a reasonable limit (2GB)
        uint64_t search_limit = start_sector + (2ULL * 1024 * 1024 * 1024 / sector_size);

        // Start probing from near the beginning (start_sector + 64) to find
        // the actual end even for small files. The old approach started at
        // file_sectors/2 which skipped past the end of small files.
        for (uint64_t probe = start_sector + 64;
             probe < search_limit;
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