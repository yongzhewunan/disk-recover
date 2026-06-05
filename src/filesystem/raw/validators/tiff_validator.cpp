#include "tiff_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "validators.hpp"
#include <cstring>

namespace disk_recover {
namespace {

// TIFF magic bytes
static const uint8_t TIFF_LE_MAGIC[] = {0x49, 0x49, 0x2A, 0x00};  // Little-Endian "II*"
static const uint8_t TIFF_BE_MAGIC[] = {0x4D, 0x4D, 0x00, 0x2A};  // Big-Endian "MM*"
static const uint8_t ORF_MAGIC[]     = {0x49, 0x49, 0x52, 0x4F};  // Olympus ORF "IIRO"

// Helper to read 16-bit value with endianness
inline uint16_t rd16(const uint8_t* p, bool le) {
    return le ? read_le16(p) : read_be16(p);
}

// Helper to read 32-bit value with endianness
inline uint32_t rd32(const uint8_t* p, bool le) {
    return le ? read_le32(p) : read_be32(p);
}

// ============================================================================
// TIFF/RAW Three-Phase Validator
//
// Phase 1 (header_check): Validate TIFF header + IFD0.
//   - Validates byte order mark, magic number, IFD offset
//   - Scans IFD0 tags for vendor identification (CR2, NEF, ARW, DNG, RW2, ORF)
//   - Returns AcceptStructure if IFD tags parsed
//   - Returns AcceptContainer if RAW format identified
//
// Phase 3 (file_check): Walk IFDs to calculate file size from strip/tile offsets.
//   - Traverses all IFDs and sub-IFDs
//   - Finds max (strip/tile offset + byte count) to determine file extent
//   - Sets calculated_file_size from strip offset calculations
// ============================================================================

// ── Phase 1: Header check ──
ValidateResult check_tiff_raw_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    calculated_file_size = 0;  // Size unknown until file_check walks IFDs

    if (length < 8) return ValidateResult::Reject;

    // Determine byte order
    bool le;
    if (data[0] == 0x49 && data[1] == 0x49) {
        le = true;
    } else if (data[0] == 0x4D && data[1] == 0x4D) {
        le = false;
    } else {
        return ValidateResult::Reject;
    }

    // Validate magic number (42 for standard TIFF, 0x55 for Panasonic RW2)
    uint16_t magic = rd16(data + 2, le);
    if (magic != 42 && magic != 0x55) return ValidateResult::Reject;

    // Validate IFD offset
    uint32_t ifd_offset = rd32(data + 4, le);
    if (ifd_offset < 8 || ifd_offset >= length) return ValidateResult::Reject;

    // ── CR2 explicit check (strongest evidence) ──
    // CR2 has "CR" at bytes 8-9
    if (length >= 10 && data[8] == 'C' && data[9] == 'R') {
        return ValidateResult::AcceptContainer;
    }

    // ── IFD tag scanning for vendor detection ──
    if (ifd_offset + 2 <= length) {
        uint16_t count = rd16(data + ifd_offset, le);

        // Sanity check tag count
        if (count > 0 && count < 100 && ifd_offset + 2 + 12 * count <= length) {

            // Scan for specific tags in priority order
            for (uint16_t i = 0; i < count; ++i) {
                const uint8_t* entry = data + ifd_offset + 2 + 12 * i;
                uint16_t tag = rd16(entry, le);

                // DNGVersion tag (50706) - definitive DNG evidence
                if (tag == 50706) {
                    return ValidateResult::AcceptContainer;
                }

                // MakerNote tag (37500) - check for vendor signature
                if (tag == 37500) {
                    uint32_t mn_count = rd32(entry + 4, le);
                    uint32_t offset = rd32(entry + 8, le);

                    if (offset < length && mn_count > 0) {
                        // Nikon: "Nikon\x00" or "Nikon" + IFD
                        if (offset + 6 <= length &&
                            std::memcmp(data + offset, "Nikon", 5) == 0) {
                            return ValidateResult::AcceptContainer;
                        }

                        // Sony: "SONY DSC " or "SONY"
                        if (offset + 4 <= length &&
                            std::memcmp(data + offset, "SONY", 4) == 0) {
                            return ValidateResult::AcceptContainer;
                        }

                        // Panasonic: "Panasonic"
                        if (offset + 9 <= length &&
                            std::memcmp(data + offset, "Panasonic", 9) == 0) {
                            return ValidateResult::AcceptContainer;
                        }

                        // Olympus: "OLYMP" or "OLYMPUS"
                        if (offset + 5 <= length &&
                            std::memcmp(data + offset, "OLYMP", 5) == 0) {
                            return ValidateResult::AcceptContainer;
                        }
                    }
                }
            }
        }
    }

    // Valid TIFF structure with IFD parsed but no RAW vendor identified
    return ValidateResult::AcceptStructure;
}

// ── Phase 3: File check (IFD walking for size calculation) ──
// Walks all IFDs to find the maximum strip/tile offset + size.
// Properly pairs StripOffsets with StripByteCounts for accurate extent calculation.
ValidateResult check_tiff_raw_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 8) return ValidateResult::Reject;

    // Determine byte order
    bool le;
    if (data[0] == 0x49 && data[1] == 0x49) {
        le = true;
    } else if (data[0] == 0x4D && data[1] == 0x4D) {
        le = false;
    } else {
        return ValidateResult::Reject;
    }

    // Check for ORF signature (IIRO)
    bool is_orf = (data[0] == 0x49 && data[1] == 0x49 &&
                   data[2] == 0x52 && data[3] == 0x4F);

    // Validate magic number
    uint16_t magic = rd16(data + 2, le);
    if (magic != 42 && magic != 0x55 && !is_orf) return ValidateResult::Reject;

    uint32_t ifd_offset = rd32(data + 4, le);
    if (ifd_offset < 8 || ifd_offset >= length) return ValidateResult::Reject;

    // Walk IFDs to find maximum data extent
    uint64_t max_extent = 0;
    uint32_t current_ifd = ifd_offset;
    int ifd_count = 0;

    // Helper lambda to read offset array and corresponding byte count array
    // Returns the maximum (offset + byte_count) found
    auto process_strip_tiles = [&](uint16_t offset_tag, uint16_t count_tag,
                                    const uint8_t* ifd_start, uint16_t num_entries,
                                    bool is_le) -> uint64_t {
        uint64_t local_max = 0;
        uint32_t offset_array_offset = 0;
        uint32_t count_array_offset = 0;
        uint32_t num_strips = 0;
        uint16_t offset_type = 4;  // Default LONG
        uint16_t count_type = 4;   // Default LONG
        bool offset_inline = false;
        uint32_t offset_inline_val = 0;
        bool count_inline = false;
        uint32_t count_inline_val = 0;

        // First pass: find both offset and count entries
        for (uint16_t i = 0; i < num_entries; ++i) {
            const uint8_t* entry = ifd_start + 2 + 12 * i;
            if (ifd_start + 2 + 12 * (i + 1) > data + length) break;

            uint16_t tag = rd16(entry, is_le);
            uint16_t type = rd16(entry + 2, is_le);
            uint32_t count = rd32(entry + 4, is_le);
            uint32_t value = rd32(entry + 8, is_le);

            if (tag == offset_tag) {
                num_strips = count;
                offset_type = type;
                uint8_t entry_size = (type == 3) ? 2 : 4;

                if (count * entry_size <= 4) {
                    // Inline single value
                    offset_inline = true;
                    offset_inline_val = value;
                    offset_array_offset = 0;  // Not used
                } else {
                    offset_inline = false;
                    offset_array_offset = value;
                }
            }

            if (tag == count_tag) {
                count_type = type;
                uint8_t entry_size = (type == 3) ? 2 : 4;

                if (count * entry_size <= 4) {
                    count_inline = true;
                    count_inline_val = value;
                    count_array_offset = 0;
                } else {
                    count_inline = false;
                    count_array_offset = value;
                }
            }
        }

        if (num_strips == 0) return 0;

        // Second pass: compute extent for each strip/tile
        for (uint32_t i = 0; i < num_strips; ++i) {
            uint64_t strip_offset = 0;
            uint64_t strip_count = 0;

            // Get strip offset
            if (offset_inline) {
                strip_offset = offset_inline_val;
            } else {
                uint8_t entry_size = (offset_type == 3) ? 2 : 4;
                size_t read_pos = offset_array_offset + i * entry_size;
                if (read_pos + entry_size > length) break;

                if (entry_size == 2) {
                    strip_offset = rd16(data + read_pos, is_le);
                } else {
                    strip_offset = rd32(data + read_pos, is_le);
                }
            }

            // Get strip byte count
            if (count_inline) {
                strip_count = count_inline_val;
            } else if (count_array_offset > 0) {
                uint8_t entry_size = (count_type == 3) ? 2 : 4;
                size_t read_pos = count_array_offset + i * entry_size;
                if (read_pos + entry_size > length) {
                    // Can't read byte count, estimate from offset
                    strip_count = 0;  // Will use offset-only fallback
                } else {
                    if (entry_size == 2) {
                        strip_count = rd16(data + read_pos, is_le);
                    } else {
                        strip_count = rd32(data + read_pos, is_le);
                    }
                }
            }

            // Compute extent: offset + byte count
            uint64_t extent = strip_offset + strip_count;
            if (extent > local_max) local_max = extent;

            // Also track raw offset in case byte count is invalid
            if (strip_offset > local_max && strip_count == 0) {
                local_max = strip_offset;
            }
        }

        return local_max;
    };

    while (current_ifd > 0 && current_ifd < length && ifd_count < 10) {
        ifd_count++;

        if (current_ifd + 2 > length) break;
        uint16_t count = rd16(data + current_ifd, le);
        if (count == 0 || count >= 100) break;
        if (current_ifd + 2 + 12 * count > length) break;

        // Process StripOffsets (273) + StripByteCounts (279)
        uint64_t strip_extent = process_strip_tiles(273, 279, data + current_ifd, count, le);
        if (strip_extent > max_extent) max_extent = strip_extent;

        // Process TileOffsets (324) + TileByteCounts (325)
        uint64_t tile_extent = process_strip_tiles(324, 325, data + current_ifd, count, le);
        if (tile_extent > max_extent) max_extent = tile_extent;

        // Scan for SubIFD (330) - follow to find more data references
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t* entry = data + current_ifd + 2 + 12 * i;
            uint16_t tag = rd16(entry, le);
            uint32_t value_offset = rd32(entry + 8, le);

            if (tag == 330 && value_offset > 0 && value_offset < length) {
                // Follow SubIFD pointer(s)
                uint32_t sub_ifd = value_offset;
                int sub_count = 0;
                while (sub_ifd > 0 && sub_ifd < length && sub_count < 5) {
                    sub_count++;
                    if (sub_ifd + 2 > length) break;
                    uint16_t sc = rd16(data + sub_ifd, le);
                    if (sc == 0 || sc >= 100) break;
                    if (sub_ifd + 2 + 12 * sc > length) break;

                    // Process strips/tiles in SubIFD
                    uint64_t sub_strip_extent = process_strip_tiles(273, 279, data + sub_ifd, sc, le);
                    if (sub_strip_extent > max_extent) max_extent = sub_strip_extent;

                    uint64_t sub_tile_extent = process_strip_tiles(324, 325, data + sub_ifd, sc, le);
                    if (sub_tile_extent > max_extent) max_extent = sub_tile_extent;

                    // Next IFD pointer
                    if (sub_ifd + 2 + 12 * sc + 4 <= length) {
                        sub_ifd = rd32(data + sub_ifd + 2 + 12 * sc, le);
                    } else {
                        break;
                    }
                }
            }

            // ExifIFD (34665) - may contain thumbnail data
            if (tag == 34665 && value_offset > 0 && value_offset < length) {
                // ExifIFD may have JPEG thumbnail in StripOffsets
                // We'll scan it for any strip data
                uint32_t exif_ifd = value_offset;
                if (exif_ifd + 2 <= length) {
                    uint16_t exif_count = rd16(data + exif_ifd, le);
                    if (exif_count > 0 && exif_count < 100 && exif_ifd + 2 + 12 * exif_count <= length) {
                        uint64_t exif_strip_extent = process_strip_tiles(273, 279, data + exif_ifd, exif_count, le);
                        if (exif_strip_extent > max_extent) max_extent = exif_strip_extent;
                    }
                }
            }
        }

        // Next IFD pointer (at end of current IFD entries)
        uint32_t next_ifd_pos = current_ifd + 2 + 12 * count;
        if (next_ifd_pos + 4 <= length) {
            current_ifd = rd32(data + next_ifd_pos, le);
        } else {
            current_ifd = 0;
        }
    }

    // If we found data extents, set calculated file size
    if (max_extent > 0) {
        calculated_file_size = max_extent;
    }

    return ValidateResult::AcceptVerified;
}

} // anonymous namespace

// TIFF Little-Endian
const FormatDescriptor TIFF_LE_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"tiff",
    .description     = L"TIFF-LE",
    .min_filesize    = 8,
    .max_filesize    = 0,
    .signature       = {TIFF_LE_MAGIC, 4, 0},
    .header_check    = check_tiff_raw_header_impl,
    .data_check      = nullptr,
    .file_check      = check_tiff_raw_file_impl,
    .enabled_by_default = true,
};

// TIFF Big-Endian
const FormatDescriptor TIFF_BE_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"tiff",
    .description     = L"TIFF-BE",
    .min_filesize    = 8,
    .max_filesize    = 0,
    .signature       = {TIFF_BE_MAGIC, 4, 0},
    .header_check    = check_tiff_raw_header_impl,
    .data_check      = nullptr,
    .file_check      = check_tiff_raw_file_impl,
    .enabled_by_default = true,
};

// Olympus ORF (IIRO signature)
const FormatDescriptor ORF_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"orf",
    .description     = L"Olympus ORF",
    .min_filesize    = 8,
    .max_filesize    = 0,
    .signature       = {ORF_MAGIC, 4, 0},
    .header_check    = check_tiff_raw_header_impl,
    .data_check      = nullptr,
    .file_check      = check_tiff_raw_file_impl,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_tiff_raw_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_tiff_raw_header_impl(data, length, calculated_file_size);
}

ValidateResult check_tiff_raw_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_tiff_raw_file_impl(data, length, calculated_file_size);
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_tiff_raw(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_tiff_raw_header_impl(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptContainer) mr.flags = mr.flags | MatchFlags::ContainerParsed;
    if (vr >= ValidateResult::AcceptVerified)   mr.flags = mr.flags | MatchFlags::DeepValidated;
    mr.verified_file_size = calculated_file_size;

    // Determine file type based on vendor detection
    if (length >= 10 && data[8] == 'C' && data[9] == 'R') {
        mr.signature = {FileType::Image, L"cr2", L"Canon CR2"};
    } else {
        // Check for ORF
        bool is_orf = (data[0] == 0x49 && data[1] == 0x49 &&
                       data[2] == 0x52 && data[3] == 0x4F);
        if (is_orf) {
            mr.signature = {FileType::Image, L"orf", L"Olympus ORF"};
        } else {
            mr.signature = {FileType::Image, L"tiff", L"TIFF"};
        }
    }
    return mr;
}

} // namespace disk_recover
