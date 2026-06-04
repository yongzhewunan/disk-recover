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
    if (!brand_entry) {
        if (has_moov_box(data, length, ftyp_pos, box_size)) {
            return ValidateResult::AcceptContainer;
        }
        return ValidateResult::AcceptHeader;
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

    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);

    // Only accept if brand is an Audio type
    if (!brand_entry || brand_entry->file_type != FileType::Audio) return ValidateResult::Reject;

    if (has_moov_box(data, length, ftyp_pos, box_size)) {
        return ValidateResult::AcceptVerified;
    }
    return ValidateResult::AcceptContainer;
}

// ── Phase 3: File check (atom tree walking for size calculation) ──
// Walks top-level boxes to determine total file size.
// Shared by all BMFF descriptors.
ValidateResult check_bmff_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 8) return ValidateResult::Reject;

    size_t pos = 0;
    uint64_t total_size = 0;

    while (pos + 8 <= length) {
        uint32_t box_size = read_be32(data + pos);

        // Check for extended size (box_size == 1)
        if (box_size == 1) {
            if (pos + 16 > length) break;
            uint64_t ext_size = read_be64(data + pos + 8);
            if (ext_size < 16) break;  // Invalid extended size
            total_size = pos + ext_size;
            // If the extended size exceeds our data, we have our answer
            if (ext_size > length - pos) {
                calculated_file_size = total_size;
                return ValidateResult::AcceptVerified;
            }
            pos += static_cast<size_t>(ext_size);
        } else if (box_size == 0) {
            // Box extends to end of file — total size is unknown from header alone
            calculated_file_size = 0;  // Unknown, extends to EOF
            return ValidateResult::AcceptVerified;
        } else if (box_size >= 8) {
            total_size = pos + box_size;
            // If this box extends past our data, we have our answer
            if (pos + box_size > length) {
                calculated_file_size = total_size;
                return ValidateResult::AcceptVerified;
            }
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

    return ValidateResult::AcceptVerified;
}

// ============================================================================
// Auto-registration with FormatRegistry
// Three descriptors for the three BMFF file type categories.
// All share the same ftyp signature at offset 4.
// The registry tries all matching descriptors and picks the deepest result.
// ============================================================================

static const FormatDescriptor BMFF_IMAGE_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"heic",
    .description     = L"HEIC/AVIF (ISO BMFF Image)",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, 4, 4},
    .header_check    = check_bmff_image_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

static bool _bmff_image_registered = []() {
    FormatRegistry::instance().register_format(BMFF_IMAGE_DESCRIPTOR);
    return true;
}();

static const FormatDescriptor BMFF_VIDEO_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"mp4",
    .description     = L"MP4/MOV (ISO BMFF Video)",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, 4, 4},
    .header_check    = check_bmff_video_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

static bool _bmff_video_registered = []() {
    FormatRegistry::instance().register_format(BMFF_VIDEO_DESCRIPTOR);
    return true;
}();

static const FormatDescriptor BMFF_AUDIO_DESCRIPTOR = {
    .file_type       = FileType::Audio,
    .extension       = L"m4a",
    .description     = L"M4A (ISO BMFF Audio)",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {FTYP_MAGIC, 4, 4},
    .header_check    = check_bmff_audio_header_impl,
    .data_check      = nullptr,
    .file_check      = check_bmff_file_impl,
    .enabled_by_default = true,
};

static bool _bmff_audio_registered = []() {
    FormatRegistry::instance().register_format(BMFF_AUDIO_DESCRIPTOR);
    return true;
}();

} // anonymous namespace

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
