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
    if (length >= 13) {
        uint16_t width = read_le16(data + 6);
        uint16_t height = read_le16(data + 8);
        uint8_t packed = data[10];

        // Extract flags
        bool has_global_color_table = (packed & 0x80) != 0;
        uint8_t color_resolution = ((packed >> 4) & 0x07) + 1;
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

        // Phase 3: Check for trailer (0x3B)
        if (length >= 13 + gct_size + 1) {
            size_t pos = 13 + gct_size;

            // Skip image data blocks (simplified - just look for trailer)
            // In a real implementation, we'd parse the full block structure
            for (size_t i = pos; i < length; ++i) {
                if (data[i] == 0x3B) {
                    evidence += GIF_WEIGHTS.footer_weight;
                    flags = flags | MatchFlags::HasFooter;
                    break;
                }
            }
        }

        // Phase 4: Check for extension blocks (GIF89a feature)
        if (is_gif89a && length >= 14) {
            // Look for application extension (NETSCAPE, etc.)
            for (size_t i = 13; i < length - 11; ++i) {
                if (data[i] == 0x21 && data[i + 1] == 0xFF) {
                    // Application extension found
                    evidence += GIF_WEIGHTS.container_weight;
                    break;
                }
            }
        }
    }

    std::wstring desc = is_gif89a ? L"GIF89a" : L"GIF87a";

    return MatchResult{
        {FileType::Image, L"gif", desc},
        normalize_confidence(evidence, GIF_WEIGHTS),
        flags
    };
}

} // namespace disk_recover