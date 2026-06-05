#include "riff_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// RIFF magic bytes: "RIFF"
static const uint8_t RIFF_MAGIC[] = {0x52, 0x49, 0x46, 0x46};

// ============================================================================
// RIFF Three-Phase Validator
//
// RIFF is a container format where bytes 8-11 identify the sub-type.
// We register separate FormatDescriptors for each sub-type so that
// the descriptor's file_type, extension, and description are correct.
//
// All share the same RIFF signature but have different header_check
// functions that validate the specific container type.
//
// calculated_file_size = riff_size + 8 (from RIFF header field at offset 4)
// ============================================================================

// ── WebP header check ──
ValidateResult check_webp_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 12) return ValidateResult::Reject;
    if (!has_str(data, length, 0, "RIFF")) return ValidateResult::Reject;
    if (!has_str(data, length, 8, "WEBP")) return ValidateResult::Reject;

    uint32_t riff_size = read_le32(data + 4);
    calculated_file_size = (riff_size >= 4) ? static_cast<uint64_t>(riff_size) + 8 : 0;

    // Check for VP8/VP8L/VP8X chunk
    if (length >= 20) {
        if (has_str(data, length, 12, "VP8 ") ||
            has_str(data, length, 12, "VP8L") ||
            has_str(data, length, 12, "VP8X")) {
            return ValidateResult::AcceptVerified;
        }
    }
    // WebP header found but no codec chunk
    return ValidateResult::AcceptContainer;
}

// ── AVI header check ──
ValidateResult check_avi_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 12) return ValidateResult::Reject;
    if (!has_str(data, length, 0, "RIFF")) return ValidateResult::Reject;
    if (!has_str(data, length, 8, "AVI ")) return ValidateResult::Reject;

    uint32_t riff_size = read_le32(data + 4);
    calculated_file_size = (riff_size >= 4) ? static_cast<uint64_t>(riff_size) + 8 : 0;

    // Check for LIST hdrl (AVI header list)
    for (size_t i = 12; i + 12 <= length; i += 4) {
        if (has_str(data, length, i, "LIST")) {
            uint32_t list_size = read_le32(data + i + 4);
            if (i + 8 + list_size <= length && has_str(data, length, i + 8, "hdrl")) {
                return ValidateResult::AcceptVerified;
            }
        }
    }
    // AVI header found but no hdrl
    return ValidateResult::AcceptContainer;
}

// ── WAV header check ──
ValidateResult check_wav_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 12) return ValidateResult::Reject;
    if (!has_str(data, length, 0, "RIFF")) return ValidateResult::Reject;
    if (!has_str(data, length, 8, "WAVE")) return ValidateResult::Reject;

    uint32_t riff_size = read_le32(data + 4);
    calculated_file_size = (riff_size >= 4) ? static_cast<uint64_t>(riff_size) + 8 : 0;

    // Check for fmt chunk
    for (size_t i = 12; i + 8 <= length; i += 4) {
        if (has_str(data, length, i, "fmt ")) {
            return ValidateResult::AcceptVerified;
        }
    }
    // WAV header found but no fmt chunk
    return ValidateResult::AcceptContainer;
}

// ── Generic RIFF header check (fallback for unknown container types) ──
ValidateResult check_riff_generic_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 12) return ValidateResult::Reject;
    if (!has_str(data, length, 0, "RIFF")) return ValidateResult::Reject;

    // Reject if a known container type — those have their own descriptors
    if (has_str(data, length, 8, "WEBP") ||
        has_str(data, length, 8, "AVI ") ||
        has_str(data, length, 8, "WAVE")) {
        return ValidateResult::Reject;
    }

    uint32_t riff_size = read_le32(data + 4);
    calculated_file_size = (riff_size >= 4) ? static_cast<uint64_t>(riff_size) + 8 : 0;

    return ValidateResult::AcceptHeader;
}

} // anonymous namespace

const FormatDescriptor WEBP_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"webp",
    .description     = L"WebP",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {RIFF_MAGIC, 4, 0},
    .header_check    = check_webp_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

const FormatDescriptor AVI_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"avi",
    .description     = L"AVI",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {RIFF_MAGIC, 4, 0},
    .header_check    = check_avi_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

const FormatDescriptor WAV_DESCRIPTOR = {
    .file_type       = FileType::Audio,
    .extension       = L"wav",
    .description     = L"WAV",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {RIFF_MAGIC, 4, 0},
    .header_check    = check_wav_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

const FormatDescriptor RIFF_GENERIC_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"riff",
    .description     = L"RIFF",
    .min_filesize    = 12,
    .max_filesize    = 0,
    .signature       = {RIFF_MAGIC, 4, 0},
    .header_check    = check_riff_generic_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_riff_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Try each sub-type in priority order
    ValidateResult result = check_webp_header_impl(data, length, calculated_file_size);
    if (result != ValidateResult::Reject) return result;

    result = check_avi_header_impl(data, length, calculated_file_size);
    if (result != ValidateResult::Reject) return result;

    result = check_wav_header_impl(data, length, calculated_file_size);
    if (result != ValidateResult::Reject) return result;

    result = check_riff_generic_header_impl(data, length, calculated_file_size);
    return result;
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_riff(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_riff_header(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptContainer) mr.flags = mr.flags | MatchFlags::ContainerParsed;
    mr.verified_file_size = calculated_file_size;

    // Determine sub-type
    if (length >= 12 && has_str(data, length, 8, "WEBP")) {
        mr.signature = {FileType::Image, L"webp", L"WebP"};
    } else if (length >= 12 && has_str(data, length, 8, "AVI ")) {
        mr.signature = {FileType::Video, L"avi", L"AVI"};
    } else if (length >= 12 && has_str(data, length, 8, "WAVE")) {
        mr.signature = {FileType::Audio, L"wav", L"WAV"};
    } else {
        mr.signature = {FileType::Video, L"riff", L"RIFF"};
    }
    return mr;
}

} // namespace disk_recover
