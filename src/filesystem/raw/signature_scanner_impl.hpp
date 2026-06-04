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

namespace disk_recover {

// Walk the top-level MP4/MOV atom tree to calculate total file size.
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

        uint32_t atom_size32 = (static_cast<uint32_t>(hdr[0]) << 24) |
                                (static_cast<uint32_t>(hdr[1]) << 16) |
                                (static_cast<uint32_t>(hdr[2]) << 8) |
                                static_cast<uint32_t>(hdr[3]);

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
            atom_size = (static_cast<uint64_t>(hdr[8]) << 56) |
                        (static_cast<uint64_t>(hdr[9]) << 48) |
                        (static_cast<uint64_t>(hdr[10]) << 40) |
                        (static_cast<uint64_t>(hdr[11]) << 32) |
                        (static_cast<uint64_t>(hdr[12]) << 24) |
                        (static_cast<uint64_t>(hdr[13]) << 16) |
                        (static_cast<uint64_t>(hdr[14]) << 8) |
                        static_cast<uint64_t>(hdr[15]);
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

// Parse actual file size from file header bytes
static uint64_t parse_file_size(FileType file_type, const uint8_t* data, size_t data_len) {
    if (data_len < 16) return 0;

    switch (file_type) {
    case FileType::Image:
        // JPEG
        if (data[0] == 0xFF && data[1] == 0xD8) {
            size_t pos = 2;
            while (pos + 9 < data_len) {
                if (data[pos] != 0xFF) break;
                uint8_t marker = data[pos + 1];
                if (marker == 0xDA) break;
                if (marker == 0xC0 || marker == 0xC2) {
                    int height = (data[pos + 5] << 8) | data[pos + 6];
                    int width = (data[pos + 7] << 8) | data[pos + 8];
                    if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                        uint64_t sz = static_cast<uint64_t>(width) * height * 3 / 10;
                        return (std::min)(sz, 50ULL * 1024 * 1024);
                    }
                    break;
                }
                uint16_t seg_len = (data[pos + 2] << 8) | data[pos + 3];
                pos += 2 + seg_len;
            }
        }
        // PNG
        if (data_len >= 24 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
            int width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
            int height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
            if (width > 0 && height > 0 && width <= 65535 && height <= 65535) {
                uint64_t sz = static_cast<uint64_t>(width) * height * 4 / 2;
                return (std::min)(sz, 50ULL * 1024 * 1024);
            }
        }
        // BMP
        if (data[0] == 'B' && data[1] == 'M' && data_len >= 30) {
            uint32_t file_sz = *reinterpret_cast<const uint32_t*>(data + 2);
            if (file_sz > 0 && file_sz <= 50 * 1024 * 1024) {
                return file_sz;
            }
        }
        // WebP
        if (data_len >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
            uint32_t riff_sz = *reinterpret_cast<const uint32_t*>(data + 4);
            if (riff_sz > 8 && riff_sz <= 50 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        break;

    case FileType::Video:
        // AVI
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data_len >= 8) {
            uint32_t riff_sz = *reinterpret_cast<const uint32_t*>(data + 4);
            if (riff_sz > 8 && riff_sz <= 2ULL * 1024 * 1024 * 1024) {
                return riff_sz + 8;
            }
        }
        // MP4/MOV
        if (data_len >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
            return 0;  // Requires atom tree walking
        }
        // MKV/WebM
        if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) {
            return 0;
        }
        // FLV
        if (data[0] == 'F' && data[1] == 'L' && data[2] == 'V') {
            return 0;
        }
        break;

    default:
        break;
    }
    return 0;
}

// Template implementation
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
            if (consecutive_bad_batches >= 4) {
                uint64_t actual_skip = (std::min)(1024ULL, config.end_sector - sector);
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
                    file_end = (std::max)(file_end, frag.start_sector + frag.sector_count);
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

template<typename ReaderType>
bool SignatureScanner::try_recover_file(ReaderType& reader, uint64_t start_sector,
                                         const FileSignature& sig, RecoverableFile& file) {
    const uint32_t sector_size = reader.sector_size();

    file.file_type = sig.file_type;
    file.file_name = sig.description + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + sig.extension;

    const uint32_t HEADER_SECTORS = 4;
    AlignedBuffer headerBuf(HEADER_SECTORS * sector_size, sector_size);
    bool header_ok = reader.read_sectors_checked(start_sector, HEADER_SECTORS, headerBuf);

    uint64_t estimated_size = 0;
    if (header_ok) {
        estimated_size = parse_file_size(sig.file_type, headerBuf.data(),
                                          HEADER_SECTORS * sector_size);
    }
    file.is_corrupted = !header_ok;

    if (estimated_size == 0) {
        if (sig.file_type == FileType::Video) {
            estimated_size = 10 * 1024 * 1024;
        } else if (sig.file_type == FileType::Image) {
            estimated_size = 500 * 1024;
        } else {
            estimated_size = 1 * 1024 * 1024;
        }
    }

    if (sig.file_type == FileType::Video &&
        (sig.extension == L"mp4" || sig.extension == L"mov")) {
        uint64_t atom_size = determine_mp4_size_impl(reader, start_sector, sector_size);
        if (atom_size > 0) {
            estimated_size = atom_size;
        }
    }

    if (sig.file_type == FileType::Image && estimated_size > 50 * 1024 * 1024) {
        estimated_size = 50 * 1024 * 1024;
    }
    if (sig.file_type == FileType::Video && estimated_size > 2ULL * 1024 * 1024 * 1024) {
        estimated_size = 2ULL * 1024 * 1024 * 1024;
    }

    uint64_t file_sectors = (estimated_size + sector_size - 1) / sector_size;
    if (file_sectors < 1) file_sectors = 1;

    if (sig.file_type == FileType::Video) {
        const uint32_t PROBE_CHUNK = 64;
        AlignedBuffer probeBuf(PROBE_CHUNK * sector_size, sector_size);

        uint64_t search_limit = start_sector + (2ULL * 1024 * 1024 * 1024 / sector_size);

        for (uint64_t probe = start_sector + 64;
             probe < search_limit;
             probe += PROBE_CHUNK) {

            if (!reader.read_sectors_checked(probe, PROBE_CHUNK, probeBuf)) {
                file_sectors = probe - start_sector;
                break;
            }

            bool found_next = false;
            for (uint32_t off = 0; off < PROBE_CHUNK * sector_size; off += sector_size) {
                auto next_sig = FileSignatures::match(probeBuf.data() + off, sector_size);
                if (next_sig) {
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

    return true;
}

} // namespace disk_recover

#endif // SIGNATURE_SCANNER_IMPL_HPP
