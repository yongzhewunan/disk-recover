#include "bmff_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "bmff_brands.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// BMFF ftyp signature: "ftyp" at offset 4
static const uint8_t FTYP_MAGIC[] = {0x66, 0x74, 0x79, 0x70};

// ============================================================================
// BMFF (ISO Base Media File Format) Three-Phase Validator
//
// BMFF files are identified by the "ftyp" box at offset 4. The major brand
// at ftyp+8 determines the file type (Image, Video, Audio).
//
// We register three descriptors (Image, Video, Audio) that share the same
// ftyp signature. Each header_check validates the brand category.
// The registry tries all matching descriptors and picks the deepest result.
//
// Phase 1 (header_check): Find ftyp box, look up major brand by category.
//   - Image brands: heic, heix, hevc, hevx, mif1, msf1, avif, avis, jp2, jpm, jpx
//   - Video brands: qt, 3gp*, isom, iso*, mp41, mp42, avc*, dash, M4V*, f4v
//   - Audio brands: M4A
//   - Returns AcceptContainer for known brands, AcceptVerified if moov found
//   - calculated_file_size = 0 (requires atom tree walking)
//
// Phase 3 (file_check): Walk atom tree for total file size.
//   - Walks top-level boxes to find total extent
//   - Handles extended size (box_size == 1) and extends-to-EOF (box_size == 0)
//   - Sets calculated_file_size from atom tree
// ============================================================================

// Common helper: find ftyp box position
static size_t find_ftyp_pos(const uint8_t* data, size_t length) {
    for (size_t i = 0; i <= 24 && i + 12 <= length; i += 4) {
        if (has_str(data, length, i + 4, "ftyp")) {
            return i;
        }
    }
    return SIZE_MAX;
}

// Common helper: check compatible_brands for audio brands
// The ftyp box contains: major_brand (4), minor_version (4), compatible_brands[] (4 each)
static bool has_audio_brand(const uint8_t* data, size_t length, size_t ftyp_pos, uint32_t ftyp_box_size) {
    // First check major brand
    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);
    if (brand_entry && brand_entry->file_type == FileType::Audio) return true;

    // Then check compatible_brands (after major_brand + minor_version = offset 16)
    // Compatible brands start at ftyp_pos + 16 and continue to end of ftyp box
    size_t brands_start = ftyp_pos + 16;
    size_t brands_end = ftyp_pos + ftyp_box_size;

    for (size_t i = brands_start; i + 4 <= brands_end && i + 4 <= length; i += 4) {
        auto compat_entry = lookup_brand(data + i);
        if (compat_entry && compat_entry->file_type == FileType::Audio) return true;
    }

    return false;
}

// Common helper: check for audio handler type in moov box
// If moov contains a "soun" (sound) handler, it's an audio file
static bool has_soun_handler(const uint8_t* data, size_t length, size_t ftyp_pos, uint32_t ftyp_box_size) {
    if (ftyp_box_size > length - ftyp_pos) return false;
    size_t search_start = ftyp_pos + ftyp_box_size;

    // Search for "soun" handler type anywhere in the data after ftyp
    // We search byte-by-byte for "hdlr" first, then check the handler_type field
    // hdlr box: [size(4)] ["hdlr"(4)] [version(1)] [flags(3)] [pre_defined(4)] [handler_type(4)]
    for (size_t i = search_start; i + 20 <= length; i++) {
        if (has_str(data, length, i + 4, "hdlr")) {
            // handler_type is at: box_start + 4(size) + 4(type) + 4(ver+flags) + 4(pre_defined) = box_start + 16
            // But box_start is i, and type "hdlr" is at i+4, so handler_type is at i + 16
            size_t handler_offset = i + 4 + 4 + 4 + 4;
            if (handler_offset + 4 <= length && has_str(data, length, handler_offset, "soun")) {
                return true;
            }
        }
    }
    return false;
}

// Common helper: check for video handler type in moov box
// If moov contains a "vide" (video) handler, it's a video file
static bool has_vide_handler(const uint8_t* data, size_t length, size_t ftyp_pos, uint32_t ftyp_box_size) {
    if (ftyp_box_size > length - ftyp_pos) return false;
    size_t search_start = ftyp_pos + ftyp_box_size;

    // Same logic as has_soun_handler, but looking for "vide"
    for (size_t i = search_start; i + 20 <= length; i++) {
        if (has_str(data, length, i + 4, "hdlr")) {
            size_t handler_offset = i + 4 + 4 + 4 + 4;
            if (handler_offset + 4 <= length && has_str(data, length, handler_offset, "vide")) {
                return true;
            }
        }
    }
    return false;
}

// Common helper: check for moov box after ftyp
static bool has_moov_box(const uint8_t* data, size_t length, size_t ftyp_pos, uint32_t ftyp_box_size) {
    if (ftyp_box_size > length - ftyp_pos) return false;
    size_t search_start = ftyp_pos + ftyp_box_size;
    for (size_t i = search_start; i + 8 <= length; ) {
        uint32_t current_size = read_be32(data + i);
        if (has_str(data, length, i + 4, "moov")) return true;
        if (current_size == 1) {
            if (i + 16 > length) break;
            uint64_t ext_size = read_be64(data + i + 8);
            if (ext_size < 16) break;
            i += static_cast<size_t>(ext_size);
        } else if (current_size == 0) {
            break;
        } else if (current_size < 8) {
            break;
        } else {
            i += current_size;
        }
        if (i > length) break;
    }
    return false;
}

// ── Image BMFF header check (HEIC, AVIF, JP2, etc.) ──
ValidateResult check_bmff_image_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    calculated_file_size = 0;
    if (length < 12) return ValidateResult::Reject;

    size_t ftyp_pos = find_ftyp_pos(data, length);
    if (ftyp_pos == SIZE_MAX) return ValidateResult::Reject;

    uint32_t box_size = read_be32(data + ftyp_pos);
    if (box_size < 12) return ValidateResult::Reject;

    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);

    // Only accept if brand is an Image type
    if (!brand_entry || brand_entry->file_type != FileType::Image) return ValidateResult::Reject;

    if (has_moov_box(data, length, ftyp_pos, box_size)) {
        return ValidateResult::AcceptVerified;
    }
    return ValidateResult::AcceptContainer;
}

// ── Video BMFF header check (MP4, MOV, 3GP, M4V, F4V, etc.) ──
ValidateResult check_bmff_video_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    calculated_file_size = 0;
    if (length < 12) return ValidateResult::Reject;

    size_t ftyp_pos = find_ftyp_pos(data, length);
    if (ftyp_pos == SIZE_MAX) return ValidateResult::Reject;

    uint32_t box_size = read_be32(data + ftyp_pos);
    if (box_size < 12) return ValidateResult::Reject;

    // Reject if this is purely an audio file (has audio brands AND no video handler).
    // Many MP4 files have both soun and vide handlers — those are video files.
    bool has_soun = has_soun_handler(data, length, ftyp_pos, box_size);
    bool has_vide = has_vide_handler(data, length, ftyp_pos, box_size);

    // If both soun and vide handlers exist, it's a video file (MP4 with audio track).
    // Only reject if it has soun but no vide (pure audio = M4A).
    if (has_soun && !has_vide) return ValidateResult::Reject;

    // Also reject if brand is explicitly Audio and there's no video handler
    if (has_audio_brand(data, length, ftyp_pos, box_size) && !has_vide) return ValidateResult::Reject;

    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);

    // Only accept if brand is a Video type
    if (brand_entry && brand_entry->file_type == FileType::Video) {
        if (has_moov_box(data, length, ftyp_pos, box_size)) {
            return ValidateResult::AcceptVerified;
        }
        return ValidateResult::AcceptContainer;
    }

    // Unknown brand — check for moov box (indicates valid MP4 container)
    // Unknown brand without moov is likely a false positive
    if (!brand_entry) {
        if (has_moov_box(data, length, ftyp_pos, box_size)) {
            return ValidateResult::AcceptContainer;
        }
        return ValidateResult::Reject;  // Unknown brand without moov → reject
    }

    return ValidateResult::Reject;
}

// ── Audio BMFF header check (M4A) ──
ValidateResult check_bmff_audio_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    calculated_file_size = 0;
    if (length < 12) return ValidateResult::Reject;

    size_t ftyp_pos = find_ftyp_pos(data, length);
    if (ftyp_pos == SIZE_MAX) return ValidateResult::Reject;

    uint32_t box_size = read_be32(data + ftyp_pos);
    if (box_size < 12) return ValidateResult::Reject;

    // Check if major brand is Audio type
    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);

    // Check for handler types — if vide handler exists, this is a video file (MP4), not audio (M4A)
    bool has_soun = has_soun_handler(data, length, ftyp_pos, box_size);
    bool has_vide = has_vide_handler(data, length, ftyp_pos, box_size);

    // If vide handler exists, reject — this should be classified as Video (MP4)
    if (has_vide) return ValidateResult::Reject;

    bool is_audio = false;
    if (brand_entry && brand_entry->file_type == FileType::Audio) {
        is_audio = true;
    }

    // Also check compatible_brands for audio brands (e.g., major=mp42 with M4A in compat)
    if (!is_audio && has_audio_brand(data, length, ftyp_pos, box_size)) {
        is_audio = true;
    }

    // Also check for "soun" handler in moov (definitive audio indicator)
    // But only accept if there's no vide handler (pure audio file)
    if (!is_audio && has_soun && !has_vide) {
        is_audio = true;
    }

    if (!is_audio) return ValidateResult::Reject;

    if (has_moov_box(data, length, ftyp_pos, box_size)) {
        return ValidateResult::AcceptVerified;
    }
    return ValidateResult::AcceptContainer;
}

} // anonymous namespace

// Phase 3: File check (atom tree walking for size calculation) — exported for signature_scanner_impl
ValidateResult check_bmff_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 8) return ValidateResult::Reject;

    size_t pos = 0;
    uint64_t total_size = 0;
    int valid_boxes = 0;

    // Known BMFF box types for validation
    static const char* known_types[] = {
        "ftyp", "moov", "mdat", "meta", "free", "skip", "udta",
        "sinf", "schi", "mvhd", "tkhd", "stbl", "dinf", "smhd",
        "vmhd", "hmhd", "nmhd", "ilst", "uuid"
    };

    while (pos + 8 <= length) {
        uint32_t box_size = read_be32(data + pos);

        // Check if box type is a known BMFF box
        const uint8_t* box_type = data + pos + 4;
        bool is_known_box = false;
        for (auto kt : known_types) {
            if (memcmp(box_type, kt, 4) == 0) {
                is_known_box = true;
                break;
            }
        }

        // Check for extended size (box_size == 1)
        if (box_size == 1) {
            if (pos + 16 > length) break;
            uint64_t ext_size = read_be64(data + pos + 8);
            if (ext_size < 16) break;  // Invalid extended size
            total_size = pos + ext_size;
            // If the extended size exceeds our data, we have our answer
            if (ext_size > length - pos) {
                calculated_file_size = total_size;
                return valid_boxes >= 2 ? ValidateResult::AcceptVerified : ValidateResult::AcceptContainer;
            }
            if (is_known_box) ++valid_boxes;
            pos += static_cast<size_t>(ext_size);
        } else if (box_size == 0) {
            // Box extends to end of file — total size is unknown from header alone
            calculated_file_size = 0;  // Unknown, extends to EOF
            return valid_boxes >= 2 ? ValidateResult::AcceptVerified : ValidateResult::AcceptContainer;
        } else if (box_size >= 8) {
            total_size = pos + box_size;
            // If this box extends past our data, we have our answer
            if (pos + box_size > length) {
                calculated_file_size = total_size;
                return valid_boxes >= 2 ? ValidateResult::AcceptVerified : ValidateResult::AcceptContainer;
            }
            if (is_known_box) ++valid_boxes;
            pos += box_size;
        } else {
            // Invalid box size (< 8) — stop walking
            break;
        }
    }

    // If we walked all boxes within the buffer, total_size is the file size
    if (total_size > 0) {
        calculated_file_size = total_size;
    }

    // Require at least 2 known box types for AcceptVerified
    return valid_boxes >= 2 ? ValidateResult::AcceptVerified : ValidateResult::AcceptContainer;
}

const FormatDescriptor BMFF_IMAGE_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"heic",
    .description     = L"HEIC-AVIF (ISO BMFF Image)",
    .min_filesize    = 32,  // ftyp box (12+) + at least one more box (20+)
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, nullptr, 4, 4, 0},
    .header_check    = check_bmff_image_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

const FormatDescriptor BMFF_VIDEO_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"mp4",
    .description     = L"MP4-MOV (ISO BMFF Video)",
    .min_filesize    = 32,  // ftyp box (12+) + at least one more box (20+)
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, nullptr, 4, 4, 0},
    .header_check    = check_bmff_video_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

const FormatDescriptor BMFF_AUDIO_DESCRIPTOR = {
    .file_type       = FileType::Audio,
    .extension       = L"m4a",
    .description     = L"M4A (ISO BMFF Audio)",
    .min_filesize    = 32,  // ftyp box (12+) + at least one more box (20+)
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, nullptr, 4, 4, 0},
    .header_check    = check_bmff_audio_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_bmff_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Try each category in priority order
    ValidateResult result = check_bmff_image_header_impl(data, length, calculated_file_size);
    if (result != ValidateResult::Reject) return result;

    result = check_bmff_audio_header_impl(data, length, calculated_file_size);
    if (result != ValidateResult::Reject) return result;

    result = check_bmff_video_header_impl(data, length, calculated_file_size);
    return result;
}

ValidateResult check_bmff_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_bmff_file_impl(data, length, calculated_file_size);
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_bmff(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_bmff_header(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptContainer) mr.flags = mr.flags | MatchFlags::ContainerParsed;
    mr.verified_file_size = calculated_file_size;

    // Determine sub-type from brand
    size_t ftyp_pos = SIZE_MAX;
    for (size_t i = 0; i <= 24 && i + 12 <= length; i += 4) {
        if (has_str(data, length, i + 4, "ftyp")) {
            ftyp_pos = i;
            break;
        }
    }

    if (ftyp_pos != SIZE_MAX && ftyp_pos + 8 + 4 <= length) {
        auto brand_entry = lookup_brand(data + ftyp_pos + 8);
        if (brand_entry) {
            mr.signature = {brand_entry->file_type,
                            std::wstring(brand_entry->extension),
                            std::wstring(brand_entry->description)};
        } else {
            mr.signature = {FileType::Video, L"mp4", L"MP4 (unknown brand)"};
        }
    } else {
        mr.signature = {FileType::Video, L"mp4", L"MP4"};
    }
    return mr;
}

} // namespace disk_recover
