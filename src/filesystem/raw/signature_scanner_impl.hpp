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
#include "format_registry.hpp"
#include "validation.hpp"
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

// ── Assess corruption level from validation result ──
// Uses ValidateResult from the new three-phase model instead of confidence+flags.

static CorruptionLevel assess_corruption(ValidateResult result, bool header_ok, bool footer_found) {
    if (!header_ok) return CorruptionLevel::Severe;

    if (result >= ValidateResult::AcceptVerified && footer_found)
        return CorruptionLevel::None;

    if (result >= ValidateResult::AcceptContainer)
        return CorruptionLevel::Minor;

    if (result >= ValidateResult::AcceptStructure)
        return footer_found ? CorruptionLevel::Minor : CorruptionLevel::Moderate;

    if (result == ValidateResult::AcceptHeader)
        return CorruptionLevel::Moderate;

    return CorruptionLevel::Severe;
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

    LOG_FMT(L"[SigScanner] Starting RAW scan: sector %llu to %llu, total=%llu",
             config.start_sector, config.end_sector, progress.total_sectors);

    // Test FormatRegistry initialization before main scan loop
    uint8_t test_jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01};
    auto test_match = FormatRegistry::instance().match(test_jpeg, sizeof(test_jpeg));
    if (test_match) {
        LOG_FMT(L"[SigScanner] FormatRegistry OK: JPEG test match '%hs' result=%d",
                 test_match->descriptor->extension, static_cast<int>(test_match->result));
    } else {
        LOG_MSG(L"[SigScanner] ERROR: FormatRegistry test FAILED - no formats registered!");
    }

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

    // Periodic video dispatch configuration
    constexpr uint32_t VIDEO_DISPATCH_THRESHOLD = 100;    // Dispatch when video_files exceeds this
    constexpr uint64_t VIDEO_DISPATCH_SECTORS = 1000000; // Dispatch every 1M sectors
    uint64_t last_video_dispatch_sector = scan_start;

    // Exponential backoff skip configuration
    const SkipAheadConfig& skip_cfg = reader.skip_ahead_config();
    uint64_t current_skip_distance = skip_cfg.skip_distance_sectors;
    const uint64_t max_skip_distance = 65536;

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
            if (skip_cfg.enabled && consecutive_bad_batches >= skip_cfg.consecutive_bad_threshold) {
                uint64_t actual_skip = (std::min)(current_skip_distance, config.end_sector - sector);
                progress.sectors_scanned += actual_skip;
                progress.bad_sectors_hit += actual_skip;
                if (actual_skip >= BATCH_SECTORS) {
                    sector += (actual_skip - BATCH_SECTORS);
                } else {
                    sector -= (BATCH_SECTORS - actual_skip);
                }
                current_skip_distance = (std::min)(current_skip_distance * 2, max_skip_distance);
                consecutive_bad_batches = 0;
                continue;
            }
        } else {
            consecutive_bad_batches = 0;
            current_skip_distance = skip_cfg.skip_distance_sectors;
        }

        for (uint32_t i = 0; i < batch_count; ++i) {
            const uint8_t* data = batch_buf.data() + i * sector_size;
            uint64_t cur_sector = sector + i;

            if (cur_sector < claimed_end_sector) continue;

            // ── Use FormatRegistry for signature matching ──
            auto match_result = FormatRegistry::instance().match(data, sector_size);
            if (!match_result || match_result->result == ValidateResult::Reject) continue;

            const FormatDescriptor* desc = match_result->descriptor;

            // ── Type filtering based on ScanConfig ──
            if (desc->file_type == FileType::Image && !config.scan_images) continue;
            if (desc->file_type == FileType::Video && !config.scan_videos) continue;
            if (desc->file_type == FileType::Audio && !config.scan_audio) continue;
            if (desc->file_type == FileType::Document && !config.scan_documents) continue;
            if (desc->file_type == FileType::Archive && !config.scan_archives) continue;

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
                    LOG_FMT(L"[SigScanner] Found #%u: %s at sector %llu, size=%llu",
                             sig_count, desc->description, cur_sector, file.file_size);
                }
                if (file.file_type == FileType::Video) {
                    video_files.push_back(std::move(file));

                    // Periodic dispatch: when video_files exceeds threshold
                    if (video_files.size() >= VIDEO_DISPATCH_THRESHOLD) {
                        auto merged = merge_video_fragments(video_files, sector_size);
                        for (auto& vf : merged) {
                            if (on_file_found) on_file_found(std::move(vf));
                        }
                        video_files.clear();
                        last_video_dispatch_sector = sector;
                    }
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

            // Periodic dispatch: every VIDEO_DISPATCH_SECTORS sectors
            uint64_t sectors_since_video_dispatch = sector - last_video_dispatch_sector;
            if (sectors_since_video_dispatch >= VIDEO_DISPATCH_SECTORS && !video_files.empty()) {
                auto merged = merge_video_fragments(video_files, sector_size);
                for (auto& vf : merged) {
                    if (on_file_found) on_file_found(std::move(vf));
                }
                video_files.clear();
                last_video_dispatch_sector = sector;
            }
        }
    }

    LOG_FMT(L"[SigScanner] RAW scan complete: signatures=%u, files_found=%llu, sectors_scanned=%llu",
             sig_count, static_cast<unsigned long long>(progress.files_found), static_cast<unsigned long long>(progress.sectors_scanned));

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
                                         const FormatRegistry::MatchResult& match_result, RecoverableFile& file) {
    const FormatDescriptor* desc = match_result.descriptor;
    const uint32_t sector_size = reader.sector_size();

    file.file_type = desc->file_type;
    file.file_name = std::wstring(desc->description) + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + desc->extension;
    file.confidence = validate_result_to_confidence(match_result.result);

    const uint32_t HEADER_SECTORS = 4;
    AlignedBuffer headerBuf(HEADER_SECTORS * sector_size, sector_size);
    bool header_ok = reader.read_sectors_checked(start_sector, HEADER_SECTORS, headerBuf);

    // ── Step 0: Use calculated_file_size from validator if available ──
    // This is the most accurate source — comes from format-specific header parsing
    uint64_t estimated_size = match_result.calculated_file_size;

    // ── Step 1: Apply min_filesize from FormatDescriptor ──
    if (desc->min_filesize > 0 && estimated_size > 0 && estimated_size < desc->min_filesize) {
        estimated_size = desc->min_filesize;
    }

    // ── Step 2: Progressive carving using data_check (if available) ──
    bool footer_found = (match_result.result >= ValidateResult::AcceptVerified);

    if (estimated_size == 0 && desc->data_check != nullptr) {
        // Progressive carve: read blocks and call data_check until AcceptVerified or Reject
        const uint32_t PROBE_CHUNK = 64;
        const uint64_t MAX_CARVE_SECTORS = 102400;  // 50MB max

        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);
        uint64_t running_size = 0;

        for (uint64_t probe = start_sector;
             probe < start_sector + MAX_CARVE_SECTORS;
             probe += PROBE_CHUNK) {

            uint32_t to_read = (std::min)(PROBE_CHUNK,
                static_cast<uint32_t>(start_sector + MAX_CARVE_SECTORS - probe));
            if (!reader.read_sectors_checked(probe, to_read, probeBuf)) break;

            uint64_t calc_size = 0;
            ValidateResult data_result = desc->data_check(
                probeBuf.data(), to_read * sector_size,
                (probe - start_sector) * sector_size, calc_size);

            if (calc_size > 0) {
                estimated_size = calc_size;
                footer_found = true;
                break;
            }

            if (data_result == ValidateResult::Reject) break;
        }

        // If data_check didn't find size, use default estimate
        if (estimated_size == 0) {
            estimated_size = desc->file_type == FileType::Image ? 500 * 1024 : 10 * 1024 * 1024;
        }
    }

    // ── Step 3: File check for container formats (if available) ──
    if (estimated_size == 0 && desc->file_check != nullptr && header_ok) {
        uint64_t calc_size = 0;
        ValidateResult file_result = desc->file_check(
            headerBuf.data(), HEADER_SECTORS * sector_size, calc_size);
        if (calc_size > 0) {
            estimated_size = calc_size;
            footer_found = (file_result >= ValidateResult::AcceptVerified);
        }
    }

    // ── Step 4: Default estimates if still unknown ──
    if (estimated_size == 0) {
        switch (desc->file_type) {
        case FileType::Video:   estimated_size = 10 * 1024 * 1024; break;
        case FileType::Image:   estimated_size = 500 * 1024; break;
        case FileType::Audio:   estimated_size = 5 * 1024 * 1024; break;
        case FileType::Document: estimated_size = 1 * 1024 * 1024; break;
        case FileType::Archive: estimated_size = 5 * 1024 * 1024; break;
        default:                 estimated_size = 1 * 1024 * 1024; break;
        }
    }

    // ── Step 5: Apply max_filesize from FormatDescriptor ──
    if (desc->max_filesize > 0 && estimated_size > desc->max_filesize) {
        estimated_size = desc->max_filesize;
    }

    // ── Size caps by type ──
    if (desc->file_type == FileType::Image && estimated_size > 50 * 1024 * 1024) {
        estimated_size = 50 * 1024 * 1024;
    }
    if (desc->file_type == FileType::Video && estimated_size > 2ULL * 1024 * 1024 * 1024) {
        estimated_size = 2ULL * 1024 * 1024 * 1024;
    }

    uint64_t file_sectors = (estimated_size + sector_size - 1) / sector_size;
    if (file_sectors < 1) file_sectors = 1;

    // ── Step 6: Next-header boundary search for video ──
    if (desc->file_type == FileType::Video) {
        const uint32_t PROBE_CHUNK = 64;
        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);

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
                // Use FormatRegistry for next-header detection
                auto next_result = FormatRegistry::instance().match(
                    probeBuf.data() + off, sector_size);
                if (next_result && next_result->result != ValidateResult::Reject) {
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

    // ── Step 7: Assess corruption level ──
    file.corruption_level = assess_corruption(match_result.result, header_ok, footer_found);

    return true;
}

} // namespace disk_recover

#endif // SIGNATURE_SCANNER_IMPL_HPP
