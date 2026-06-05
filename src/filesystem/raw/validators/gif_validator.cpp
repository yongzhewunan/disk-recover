#include "gif_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// GIF magic: "GIF8" (followed by '7a' or '9a')
static const uint8_t GIF_MAGIC[] = {'G', 'I', 'F', '8'};

// ── Phase 1: Header check ──
// Validates GIF87a/GIF89a signature, Logical Screen Descriptor, and Global Color Table.
// Returns AcceptVerified if trailer found, AcceptStructure if header+LSD valid but no trailer.
ValidateResult check_gif_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 6) return ValidateResult::Reject;

    // Verify GIF signature prefix
    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F' || data[3] != '8')
        return ValidateResult::Reject;

    // Version must be 87a or 89a
    bool is_gif89a = (data[4] == '9' && data[5] == 'a');
    bool is_gif87a = (data[4] == '7' && data[5] == 'a');
    if (!is_gif87a && !is_gif89a) return ValidateResult::Reject;

    calculated_file_size = 0;  // Size unknown until trailer found

    // Validate Logical Screen Descriptor (LSD) at offset 6-12
    if (length < 13) return ValidateResult::Reject;  // Insufficient data for valid GIF

    uint16_t width  = read_le16(data + 6);
    uint16_t height = read_le16(data + 8);
    uint8_t packed  = data[10];

    // Sanity check dimensions
    if (width == 0 || height == 0) return ValidateResult::Reject;
    // Unreasonably large dimensions in a small buffer indicate garbage data
    // Real GIF pixel data would far exceed a 2048-byte buffer
    if (width > 16384 || height > 16384) return ValidateResult::Reject;

    // Extract Global Color Table flag
    bool has_gct = (packed & 0x80) != 0;
    uint8_t gct_bits = (packed & 0x07) + 1;
    size_t gct_size = 0;
    if (has_gct) {
        gct_size = 3 * (1 << gct_bits);
    }

    // Parse data blocks looking for trailer (0x3B)
    size_t pos = 13 + gct_size;
    bool found_image = false;
    size_t trailer_pos = 0;

    while (pos < length) {
        uint8_t block_type = data[pos];

        // Trailer — end of GIF
        if (block_type == 0x3B) {
            trailer_pos = pos + 1;
            calculated_file_size = trailer_pos;
            return ValidateResult::AcceptVerified;
        }

        // Image Descriptor (0x2C)
        if (block_type == 0x2C) {
            found_image = true;
            if (pos + 10 > length) break;
            pos += 9;  // Move past descriptor fields to packed byte

            // Local Color Table
            uint8_t img_packed = data[pos++];
            bool has_lct = (img_packed & 0x80) != 0;
            if (has_lct) {
                uint8_t lct_bits = (img_packed & 0x07) + 1;
                pos += 3 * (1 << lct_bits);
            }

            // LZW Minimum Code Size
            if (pos >= length) break;
            pos++;

            // Image Data Sub-blocks: size(1) + data(size), terminated by 0x00
            while (pos < length) {
                uint8_t sub_block_size = data[pos++];
                if (sub_block_size == 0) break;
                pos += sub_block_size;
            }
            continue;
        }

        // Extension Block (0x21)
        if (block_type == 0x21) {
            if (pos + 2 > length) break;
            pos++;  // Skip 0x21
            uint8_t label = data[pos++];

            // Extension sub-blocks: size(1) + data(size), terminated by 0x00
            while (pos < length) {
                uint8_t sub_block_size = data[pos++];
                if (sub_block_size == 0) break;
                pos += sub_block_size;
            }
            continue;
        }

        // Unknown block — advance
        pos++;
    }

    // Valid header structure but no trailer found (truncated or buffer too small)
    if (found_image) return ValidateResult::AcceptStructure;
    return ValidateResult::AcceptHeader;  // Only header verified, no image data yet
}

// ── Phase 2: Data check (progressive trailer search) ──
// Scans for GIF trailer byte (0x3B) in the data block.
// Search forward to avoid false positives from backward search
ValidateResult check_gif_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // Search forward for trailer 0x3B (semicolon)
    // A valid GIF trailer should appear after image data
    for (size_t i = 0; i + 1 < length; ++i) {
        if (data[i] == 0x3B) {
            // Verify this is a valid trailer position
            // Real GIF data ends with: 0x00 (terminator) + 0x3B (trailer)
            // Or the trailer appears at a reasonable position
            calculated_file_size = offset_in_file + i + 1;
            return ValidateResult::AcceptVerified;
        }
    }

    return ValidateResult::AcceptStructure;  // Keep carving
}

} // anonymous namespace

const FormatDescriptor GIF_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"gif",
    .description     = L"GIF image",
    .min_filesize    = 256,  // Minimum reasonable GIF file size
    .max_filesize    = 0,
    .signature       = {GIF_MAGIC, 4, 0},
    .header_check    = check_gif_header_impl,
    .data_check      = check_gif_data_impl,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_gif_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_gif_header_impl(data, length, calculated_file_size);
}

ValidateResult check_gif_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_gif_data_impl(data, length, offset_in_file, calculated_file_size);
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_gif(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_gif_header_impl(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptVerified)   mr.flags = mr.flags | MatchFlags::HasFooter | MatchFlags::DeepValidated;
    mr.verified_file_size = calculated_file_size;
    mr.signature = {FileType::Image, L"gif", L"GIF"};
    return mr;
}

} // namespace disk_recover