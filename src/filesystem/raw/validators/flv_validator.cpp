#include "flv_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// FLV magic: 'FLV' (0x46 0x4C 0x56)
static const uint8_t FLV_MAGIC[] = {0x46, 0x4C, 0x56};

// ── FLV header check ──
// Tier 1: Size-in-header format. Validates FLV signature, version, and flags.
// File size requires walking the body (stored as tag chain), so we return AcceptHeader
// with calculated_file_size = 0. The scanner will use next-header boundary search.
ValidateResult check_flv_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 13) return ValidateResult::Reject;

    // Verify 'FLV' signature
    if (data[0] != 0x46 || data[1] != 0x4C || data[2] != 0x56) return ValidateResult::Reject;

    // Version must be 1
    uint8_t version = data[3];
    if (version != 1) return ValidateResult::Reject;

    // Flags: bit 0 = audio, bit 1 = video. At least one must be set.
    uint8_t flags = data[4];
    if ((flags & 0x05) == 0) return ValidateResult::Reject;  // Neither audio nor video

    // Header size (4 bytes at offset 5) — must be 9
    uint32_t header_size = read_be32(data + 5);
    if (header_size != 9) return ValidateResult::Reject;

    // First PreviousTagSize (4 bytes at offset 9) — must be 0
    uint32_t prev_tag_size = read_be32(data + 9);
    if (prev_tag_size != 0) return ValidateResult::Reject;

    // FLV file size requires walking all tags — cannot determine from header alone
    calculated_file_size = 0;
    return ValidateResult::AcceptHeader;
}

} // anonymous namespace

const FormatDescriptor FLV_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"flv",
    .description     = L"Flash Video",
    .min_filesize    = 13,
    .max_filesize    = 0,
    .signature       = {FLV_MAGIC, nullptr, 3, 0, 0},
    .header_check    = check_flv_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_flv_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_flv_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover