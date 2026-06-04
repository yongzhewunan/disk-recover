#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_gif(const uint8_t* data, size_t length) {
    // Phase 1: GIF signature validation
    // GIF87a: 47 49 46 38 37 61
    // GIF89a: 47 49 46 38 39 61
    if (length < 6) return std::nullopt;

    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F' ||
        data[3] != '8') {
        return std::nullopt;
    }

    bool is_gif87a = (data[4] == '7' && data[5] == 'a');
    bool is_gif89a = (data[4] == '9' && data[5] == 'a');

    if (!is_gif87a && !is_gif89a) return std::nullopt;

    float evidence = GIF_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: Validate Logical Screen Descriptor
    // Width(2) + Height(2) + Flags(1) + Background(1) + Aspect(1)
    if (length < 13) {
        return MatchResult{
            {FileType::Image, L"gif", is_gif89a ? L"GIF89a" : L"GIF87a"},
            30,
            flags | MatchFlags::PartialMatch,
            0
        };
    }

    uint16_t width = read_le16(data + 6);
    uint16_t height = read_le16(data + 8);
    uint8_t packed = data[10];

    // Extract flags
    bool has_global_color_table = (packed & 0x80) != 0;
    uint8_t bits_per_pixel = (packed & 0x07) + 1;

    // Sanity check
    if (width > 0 && height > 0) {
        evidence += GIF_WEIGHTS.structure_weight;
        flags = flags | MatchFlags::DeepValidated;
    }

    // Calculate global color table size
    size_t gct_size = 0;
    if (has_global_color_table) {
        gct_size = 3 * (1 << (bits_per_pixel + 1));
    }

    // Phase 3: Parse data blocks to find trailer (0x3B)
    // GIF structure after header:
    // - Global Color Table (optional)
    // - Image Descriptor (0x2C) or Extension (0x21)
    // - Image Data (LZW compressed sub-blocks)
    // - Trailer (0x3B)

    size_t pos = 13 + gct_size;
    size_t trailer_pos = 0;
    bool found_image = false;

    while (pos < length) {
        if (pos >= length) break;

        uint8_t block_type = data[pos];

        // Trailer - end of GIF
        if (block_type == 0x3B) {
            trailer_pos = pos + 1;  // Position after 0x3B
            evidence += GIF_WEIGHTS.footer_weight;
            flags = flags | MatchFlags::HasFooter;
            break;
        }

        // Image Descriptor (0x2C)
        if (block_type == 0x2C) {
            found_image = true;
            // Image Descriptor is 10 bytes: 0x2C + left(2) + top(2) + width(2) + height(2) + packed(1)
            if (pos + 10 > length) break;
            pos += 9;  // Move to packed byte

            // Check for Local Color Table
            uint8_t img_packed = data[pos++];
            bool has_lct = (img_packed & 0x80) != 0;
            if (has_lct) {
                uint8_t lct_bits = (img_packed & 0x07) + 1;
                size_t lct_size = 3 * (1 << lct_bits);
                pos += lct_size;
            }

            // LZW Minimum Code Size (1 byte)
            if (pos >= length) break;
            pos++;

            // Image Data Sub-blocks
            // Each sub-block: size(1) + data(size), terminated by 0x00
            while (pos < length) {
                uint8_t sub_block_size = data[pos++];
                if (sub_block_size == 0) break;  // Block terminator
                pos += sub_block_size;
            }
            continue;
        }

        // Extension Block (0x21)
        if (block_type == 0x21) {
            if (pos + 2 > length) break;
            pos++;  // Skip 0x21
            uint8_t label = data[pos++];  // Extension label

            // Extension sub-blocks
            // Each sub-block: size(1) + data(size), terminated by 0x00
            while (pos < length) {
                uint8_t sub_block_size = data[pos++];
                if (sub_block_size == 0) break;  // Block terminator
                pos += sub_block_size;
            }
            continue;
        }

        // Unknown block type - might be corruption
        // Try to recover by advancing
        pos++;
    }

    // Phase 4: Assessment
    if (!found_image) {
        evidence *= 0.7f;
        flags = flags | MatchFlags::PartialMatch;
    }

    if (trailer_pos == 0) {
        flags = flags | MatchFlags::PartialMatch;
    }

    std::wstring desc = is_gif89a ? L"GIF89a" : L"GIF87a";

    return MatchResult{
        {FileType::Image, L"gif", desc},
        normalize_confidence(evidence, GIF_WEIGHTS),
        flags,
        trailer_pos  // verified_file_size: position after 0x3B trailer
    };
}

} // namespace disk_recover