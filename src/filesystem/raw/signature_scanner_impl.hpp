#pragma once
// Implementation file for signature_scanner.hpp template methods
// This file is included by signature_scanner.hpp and should not be included directly

#ifndef SIGNATURE_SCANNER_IMPL_HPP
#define SIGNATURE_SCANNER_IMPL_HPP

// Ensure NOMINMAX is defined before including Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "binary_reader.hpp"
#include "../common/logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace disk_recover {

// ── Format-aware gap thresholds for video fragment merging ──
static constexpr uint64_t VIDEO_MERGE_GAP_MP4  = 4ULL * 1024 * 1024;   // 4MB
static constexpr uint64_t VIDEO_MERGE_GAP_AVI  = 2ULL * 1024 * 1024;   // 2MB
static constexpr uint64_t VIDEO_MERGE_GAP_MKV  = 2ULL * 1024 * 1024;   // 2MB
static constexpr uint64_t VIDEO_MERGE_GAP_DEFAULT = 1ULL * 1024 * 1024; // 1MB

// ── Footer search ──

// Search for JPEG EOI marker (FF D9) — returns byte offset after EOI, or 0 if not found
static uint64_t find_jpeg_eoi(const uint8_t* data, size_t length) {
    if (length < 4) return 0;
    // Search forward for FFD9, but skip the SOI at position 0
    // In carving, the last FFD9 is the true EOI (handles embedded thumbnails)
    uint64_t last_eoi = 0;
    for (size_t i = 2; i + 1 < length; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            last_eoi = i + 2; // Position after EOI marker
        }
    }
    return last_eoi;
}

// Search for PNG IEND chunk — returns byte offset after IEND, or 0 if not found
static uint64_t find_png_iend(const uint8_t* data, size_t length) {
    // IEND chunk: 00 00 00 00 49 45 4E 44 AE 42 60 82 (12 bytes)
    static const uint8_t IEND[] = {0x00,0x00,0x00,0x00, 0x49,0x45,0x4E,0x44, 0xAE,0x42,0x60,0x82};
    if (length < sizeof(IEND)) return 0;
    for (size_t i = 8; i + sizeof(IEND) <= length; ++i) { // Skip PNG signature (8 bytes)
        if (std::memcmp(data + i, IEND, sizeof(IEND)) == 0) {
            return i + sizeof(IEND);
        }
    }
    return 0;
}

// Search for GIF trailer (0x3B) — returns byte offset after trailer, or 0 if not found
static uint64_t find_gif_trailer(const uint8_t* data, size_t length) {
    if (length < 2) return 0;
    // Search from end for 0x3B (semicolon = GIF trailer)
    for (size_t i = length - 1; i > 0; --i) {
        if (data[i] == 0x3B) {
            return i + 1;
        }
    }
    return 0;
}

// Search for format-specific footer in a data buffer
// Returns byte offset of file end (after footer), or 0 if not found
static uint64_t find_image_footer(FileType type, const uint8_t* data, size_t length) {
    switch (type) {
    case FileType::Image:
        // Determine sub-format from header bytes
        if (length >= 2 && data[0] == 0xFF && data[1] == 0xD8) return find_jpeg_eoi(data, length);
        if (length >= 8 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return find_png_iend(data, length);
        if (length >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') return find_gif_trailer(data, length);
        break;
    default:
        break;
    }
    return 0;
}

// ── Walk the top-level MP4/MOV atom tree to calculate total file size ──

template<typename ReaderType>
static uint64_t determine_mp4_size_impl(ReaderType& reader, uint64_t start_sector, uint32_t sector_size) {
    const uint32_t READ_SECTORS = 4;
    const uint64_t MAX_HEADER_SCAN = 4ULL * 1024 * 1024;
    const uint64_t ATOM_SANITY_LIMIT = 100ULL * 1024 * 1024 * 1024;

    AlignedBuffer buf(READ_SECTORS * sector_size, sector_size);

    uint64_t total_size = 0;
    uint64_t file_offset = 0;
    uint64_t buf_start = 0;
    uint64_t buf_end = 0;
    bool buf_valid = false;

    while (file_offset < MAX_HEADER_SCAN) {
        if (!buf_valid || file_offset + 8 > buf_end) {
            uint64_t read_offset = file_offset;
            uint64_t sector_num = start_sector + read_offset / sector_size;
            if (!reader.read_sectors_checked(sector_num, READ_SECTORS, buf)) {
                return 0;
            }
            buf_start = (sector_num - start_sector) * sector_size;
            buf_end = buf_start + READ_SECTORS * sector_size;
            buf_valid = true;

            if (file_offset + 8 > buf_end) {
                return 0;
            }
        }

        size_t buf_pos = static_cast<size_t>(file_offset - buf_start);
        const uint8_t* hdr = buf.data() + buf_pos;

        uint32_t atom_size32 = read_be32(hdr);

        uint64_t atom_size = atom_size32;

        if (atom_size32 == 1) {
            if (file_offset + 16 > buf_end) {
                uint64_t sector_num = start_sector + file_offset / sector_size;
                if (!reader.read_sectors_checked(sector_num, READ_SECTORS, buf)) {
                    return 0;
                }
                buf_start = (sector_num - start_sector) * sector_size;
                buf_end = buf_start + READ_SECTORS * sector_size;
                buf_valid = true;

                if (file_offset + 16 > buf_end) {
                    return 0;
                }
                hdr = buf.data() + static_cast<size_t>(file_offset - buf_start);
            }
            atom_size = read_be64(hdr + 8);
        } else if (atom_size32 == 0) {
            return 0;
        }

        if (atom_size > ATOM_SANITY_LIMIT) {
            return 0;
        }

        uint64_t min_size = (atom_size32 == 1) ? 16 : 8;
        if (atom_size < min_size) {
            return 0;
        }

        total_size += atom_size;
        file_offset += atom_size;

        if (file_offset > MAX_HEADER_SCAN) {
            break;
        }
    }

    return total_size;
}

// ── Parse actual file size from file header bytes ──
// Fixed: Uses read_le32/read_be32 instead of reinterpret_cast (was UB)

static uint64_t parse_file_size(FileType file_type, const uint8_t* data, size_t data_len) {
    if (data_len < 16) return 0;

    switch (file_type) {
    case FileType::Image:
        // JPEG — heuristic from SOF dimensions (fallback; footer search is primary)
        if (data[0] == 0xFF && data[1] == 0xD8) {
            size_t pos = 2;
            while (pos + 9 < data_len) {
                if (data[pos] != 0xFF) break;
                uint8_t marker = data[pos + 1];
                if (marker == 0xDA) break;
                if (marker == 0xC0 || marker == 0xC2) {
                    uint16_t height = read_be16(data + pos + 5);
                    uint16_t width  = read_be16(data + pos + 7);
                    if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                        uint64_t sz = static_cast<uint64_t>(width) * height * 3 / 10;
                        return (std::min)(sz, 50ULL * 1024 * 1024);
                    }
                    break;
                }
                uint16_t seg_len = read_be16(data + pos + 2);
                if (seg_len < 2) break;
                pos += 2 + seg_len;
            }
        }
        // PNG — heuristic from IHDR dimensions (fallback; footer search is primary)
        if (data_len >= 24 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
            uint32_t width  = read_be32(data + 16);
            uint32_t height = read_be32(data + 20);
            if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                uint64_t sz = static_cast<uint64_t>(width) * height * 4 / 2;
                return (std::min)(sz, 50ULL * 1024 * 1024);
            }
        }
        // BMP — embedded file size (FIXED: was UB with reinterpret_cast)
        if (data[0] == 'B' && data[1] == 'M' && data_len >= 30) {
            uint32_t file_sz = read_le32(data + 2);
            if (file_sz > 0 && file_sz <= 50 * 1024 * 1024) {
                return file_sz;
            }
        }
        // WebP — RIFF size (FIXED: was UB with reinterpret_cast)
        if (data_len >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
            uint32_t riff_sz = read_le32(data + 4);
            if (riff_sz > 8 && riff_sz <= 50 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        break;

    case FileType::Video:
        // AVI — RIFF size (FIXED: was UB with reinterpret_cast)
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data_len >= 8) {
            uint32_t riff_sz = read_le32(data + 4);
            if (riff_sz > 8 && riff_sz <= 2ULL * 1024 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        // MP4/MOV — requires atom tree walking (handled separately)
        if (data_len >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
            return 0;
        }
        // MKV/WebM — no inline size
        if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) {
            return 0;
        }
        // FLV — no inline size
        if (data[0] == 'F' && data[1] == 'L' && data[2] == 'V') {
            return 0;
        }
        break;

    default:
        break;
    }
    return 0;
}

// ── Footer-based boundary search for image files ──
// Reads sectors from disk and searches for format-specific footer markers.
// Returns the file size in bytes if footer found, or 0 if not found.

template<typename ReaderType>
static uint64_t search_image_footer(ReaderType& reader, uint64_t start_sector,
                                     FileType type, const std::wstring& extension,
                                     uint64_t estimated_size, uint32_t sector_size,
                                     bool& found_footer) {
    found_footer = false;
    if (type != FileType::Image) return 0;

    // Determine search range
    uint64_t search_bytes = (std::min)(estimated_size * 2, 50ULL * 1024 * 1024);
    if (search_bytes < sector_size) search_bytes = sector_size * 4; // At least 4 sectors
    uint64_t search_sectors = (search_bytes + sector_size - 1) / sector_size;

    // Read data in chunks and search for footer
    const uint32_t CHUNK_SECTORS = 256; // 128KB per read
    std::vector<uint8_t> accumulated;
    accumulated.reserve(static_cast<size_t>((std::min)(search_bytes, 50ULL * 1024 * 1024)));

    for (uint64_t sec = start_sector; sec < start_sector + search_sectors; sec += CHUNK_SECTORS) {
        uint32_t to_read = (std::min)(CHUNK_SECTORS,
            static_cast<uint32_t>(start_sector + search_sectors - sec));
        AlignedBuffer chunkBuf(to_read * sector_size, sector_size);
        if (!reader.read_sectors_checked(sec, to_read, chunkBuf)) {
            break; // Read error — stop searching
        }
        accumulated.insert(accumulated.end(),
            chunkBuf.data(), chunkBuf.data() + to_read * sector_size);
    }

    if (accumulated.empty()) return 0;

    // Search for footer
    uint64_t footer_end = find_image_footer(type, accumulated.data(), accumulated.size());
    if (footer_end > 0) {
        found_footer = true;
        return footer_end; // Exact file size in bytes
    }

    return 0; // Footer not found
}

// ── Assess corruption level from match result and recovery status ──

static CorruptionLevel assess_corruption(const MatchResult& mr, bool header_ok, bool footer_found) {
    bool has_header = has_flag(mr.flags, MatchFlags::HasHeader);
    bool has_footer = has_flag(mr.flags, MatchFlags::HasFooter) || footer_found;
    bool partial    = has_flag(mr.flags, MatchFlags::PartialMatch);

    if (!header_ok) return CorruptionLevel::Severe;

    // Header + Footer + high confidence -> intact
    if (has_header && has_footer && mr.confidence >= 60) return CorruptionLevel::None;

    // Header + Footer but lower confidence -> minor issue
    if (has_header && has_footer && mr.confidence < 60) return CorruptionLevel::Minor;

    // Header only with high confidence -> likely truncated (no footer found)
    if (has_header && !has_footer && mr.confidence >= 60) return CorruptionLevel::Minor;

    // Header only with moderate confidence -> likely damaged
    if (has_header && !has_footer && mr.confidence >= 30) return CorruptionLevel::Moderate;

    // Low confidence or partial match -> severely damaged
    if (partial || mr.confidence < 30) return CorruptionLevel::Severe;

    return CorruptionLevel::Moderate;
}

// ── Format-aware gap threshold for video merging ──

static uint64_t video_merge_gap_bytes(const std::wstring& ext) {
    if (ext == L"mp4" || ext == L"mov" || ext == L"m4v" || ext == L"heic")
        return VIDEO_MERGE_GAP_MP4;
    if (ext == L"avi") return VIDEO_MERGE_GAP_AVI;
    if (ext == L"mkv" || ext == L"webm") return VIDEO_MERGE_GAP_MKV;
    return VIDEO_MERGE_GAP_DEFAULT;
}

// ════════════════════════════════════════════════════════════════
// Template implementation: scan()
// ════════════════════════════════════════════════════════════════

template<typename ReaderType>
void SignatureScanner::scan(ReaderType& reader, const ScanConfig& config,
                            std::function<void(RecoverableFile&&)> on_file_found,
                            std::function<void(const ScanProgress&)> on_progress) {
    ScanProgress progress{};
    progress.total_sectors = config.end_sector - config.start_sector;

    const uint32_t sector_size = reader.sector_size();
    const uint32_t BATCH_SECTORS = 8192;

    uint64_t scan_start = config.start_sector;
    uint64_t initial_scanned = 0;

    if (config.resume_from_sector > config.start_sector) {
        if (config.resume_from_sector >= 64) {
            scan_start = (std::max)(config.start_sector, config.resume_from_sector - 64);
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

    auto last_save_time = std::chrono::steady_clock::now();
    uint64_t last_save_sector = scan_start;
    uint32_t consecutive_bad_batches = 0;

    // Exponential backoff skip configuration
    const SkipAheadConfig& skip_cfg = reader.skip_ahead_config();
    uint64_t current_skip_distance = skip_cfg.skip_distance_sectors;
    const uint64_t max_skip_distance = 65536;  // Cap at 32MB (65536 sectors * 512 bytes)

    for (uint64_t sector = scan_start; sector < config.end_sector; sector += BATCH_SECTORS) {
        if (config.should_stop && config.should_stop()) {
            LOG_MSG(L"[SigScanner] Stop requested, exiting scan loop");
            break;
        }

        uint32_t batch_count = (std::min)(BATCH_SECTORS, static_cast<uint32_t>(config.end_sector - sector));
        uint32_t bad_count = 0;
        uint32_t skip_ahead = 0;

        bool ok = reader.read_sectors_split(sector, batch_count, batch_buf, bad_count, skip_ahead, config.should_stop);

        if (bad_count == batch_count) {
            consecutive_bad_batches++;
            // Use configured threshold with exponential backoff
            if (skip_cfg.enabled && consecutive_bad_batches >= skip_cfg.consecutive_bad_threshold) {
                uint64_t actual_skip = (std::min)(current_skip_distance, config.end_sector - sector);
                progress.sectors_scanned += actual_skip;
                progress.bad_sectors_hit += actual_skip;
                if (actual_skip >= BATCH_SECTORS) {
                    sector += (actual_skip - BATCH_SECTORS);
                } else {
                    sector -= (BATCH_SECTORS - actual_skip);
                }
                // Exponential backoff: double skip distance for next time
                current_skip_distance = (std::min)(current_skip_distance * 2, max_skip_distance);
                consecutive_bad_batches = 0;
                LOG_FMT(L"[SigScanner] Adaptive skip: jumping %llu sectors to %llu (next skip=%llu)",
                         actual_skip, sector + BATCH_SECTORS, current_skip_distance);
                continue;
            }
        } else {
            consecutive_bad_batches = 0;
            // Good read - reset to initial skip distance
            current_skip_distance = skip_cfg.skip_distance_sectors;
        }

        for (uint32_t i = 0; i < batch_count; ++i) {
            const uint8_t* data = batch_buf.data() + i * sector_size;
            uint64_t cur_sector = sector + i;

            if (cur_sector < claimed_end_sector) continue;

            // ── PHASE 1 FIX: Use match_with_confidence() instead of legacy match() ──
            auto match_result = FileSignatures::match_with_confidence(data, sector_size);
            if (!match_result) continue;

            // ── Confidence-based early rejection ──
            if (match_result->confidence < MIN_CONFIDENCE_THRESHOLD) continue;

            const FileSignature& sig = match_result->signature;

            if (sig.file_type == FileType::Image && !config.scan_images) continue;
            if (sig.file_type == FileType::Video && !config.scan_videos) continue;

            sig_count++;
            RecoverableFile file{};
            if (try_recover_file(reader, cur_sector, *match_result, file)) {
                progress.files_found++;

                uint64_t file_end = cur_sector;
                for (const auto& frag : file.fragments) {
                    file_end = (std::max)(file_end, frag.start_sector + frag.sector_count);
                }
                claimed_end_sector = file_end;

                if (sig_count <= 10) {
                    LOG_FMT(L"[SigScanner] Found %s at sector %llu, size=%llu, confidence=%u",
                             sig.description.c_str(), cur_sector, file.file_size, file.confidence);
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

// ════════════════════════════════════════════════════════════════
// Template implementation: try_recover_file()
// ════════════════════════════════════════════════════════════════

template<typename ReaderType>
bool SignatureScanner::try_recover_file(ReaderType& reader, uint64_t start_sector,
                                         const MatchResult& match_result, RecoverableFile& file) {
    const FileSignature& sig = match_result.signature;
    const uint32_t sector_size = reader.sector_size();

    file.file_type = sig.file_type;
    file.file_name = sig.description + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + sig.extension;
    file.confidence = match_result.confidence;
    file.match_flags_raw = static_cast<uint32_t>(match_result.flags);

    const uint32_t HEADER_SECTORS = 4;
    AlignedBuffer headerBuf(HEADER_SECTORS * sector_size, sector_size);
    bool header_ok = reader.read_sectors_checked(start_sector, HEADER_SECTORS, headerBuf);

    // ── Step 1: Try embedded file size from header ──
    uint64_t estimated_size = 0;
    if (header_ok) {
        estimated_size = parse_file_size(sig.file_type, headerBuf.data(),
                                          HEADER_SECTORS * sector_size);
    }

    // ── Step 2: Footer-based boundary detection for images ──
    bool footer_found = false;
    if (sig.file_type == FileType::Image && header_ok) {
        uint64_t heuristic_size = estimated_size;
        if (heuristic_size == 0) {
            // No embedded size — use a default search range
            heuristic_size = 500 * 1024; // 500KB default
        }
        uint64_t footer_size = search_image_footer(
            reader, start_sector, sig.file_type, sig.extension,
            heuristic_size, sector_size, footer_found);
        if (footer_size > 0) {
            estimated_size = footer_size; // Use footer-based size (more accurate)
            file.match_flags_raw |= static_cast<uint32_t>(MatchFlags::HasFooter);
        }
    }

    // ── Step 3: Default estimates if still unknown ──
    if (estimated_size == 0) {
        if (sig.file_type == FileType::Video) {
            estimated_size = 10 * 1024 * 1024;
        } else if (sig.file_type == FileType::Image) {
            estimated_size = 500 * 1024;
        } else {
            estimated_size = 1 * 1024 * 1024;
        }
    }

    // ── Step 4: MP4/MOV atom tree walking ──
    if (sig.file_type == FileType::Video &&
        (sig.extension == L"mp4" || sig.extension == L"mov")) {
        uint64_t atom_size = determine_mp4_size_impl(reader, start_sector, sector_size);
        if (atom_size > 0) {
            estimated_size = atom_size;
        }
    }

    // ── Size caps ──
    if (sig.file_type == FileType::Image && estimated_size > 50 * 1024 * 1024) {
        estimated_size = 50 * 1024 * 1024;
    }
    if (sig.file_type == FileType::Video && estimated_size > 2ULL * 1024 * 1024 * 1024) {
        estimated_size = 2ULL * 1024 * 1024 * 1024;
    }

    uint64_t file_sectors = (estimated_size + sector_size - 1) / sector_size;
    if (file_sectors < 1) file_sectors = 1;

    // ── Step 5: Video next-header boundary search (improved) ──
    if (sig.file_type == FileType::Video) {
        const uint32_t PROBE_CHUNK = 64;
        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);

        // Cap search to MAX_VIDEO_SEARCH_SECTORS (100MB) instead of 2GB
        uint64_t search_limit = start_sector + (std::min)(
            MAX_VIDEO_SEARCH_SECTORS,
            2ULL * 1024 * 1024 * 1024 / sector_size);

        for (uint64_t probe = start_sector + 64;
             probe < search_limit;
             probe += PROBE_CHUNK) {

            if (!reader.read_sectors_checked(probe, PROBE_CHUNK, probeBuf)) {
                file_sectors = probe - start_sector;
                break;
            }

            bool found_next = false;
            for (uint32_t off = 0; off < PROBE_CHUNK * sector_size; off += sector_size) {
                // Use match_with_confidence() with confidence threshold
                auto next_result = FileSignatures::match_with_confidence(
                    probeBuf.data() + off, sector_size);
                if (next_result && next_result->confidence >= MIN_CONFIDENCE_THRESHOLD) {
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
    file.fragments.push_back({start_sector, file_sectors});

    // ── Step 6: Assess corruption level ──
    file.corruption_level = assess_corruption(match_result, header_ok, footer_found);

    return true;
}

} // namespace disk_recover

#endif // SIGNATURE_SCANNER_IMPL_HPP
