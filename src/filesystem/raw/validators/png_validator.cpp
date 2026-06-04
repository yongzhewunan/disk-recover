#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"
#include "../crc32.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_png(const uint8_t* data, size_t length) {
    // Phase 1: PNG signature validation
    if (length < 8) return std::nullopt;

    static const uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) {
        if (data[i] != PNG_SIGNATURE[i]) return std::nullopt;
    }

    float evidence = PNG_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: Validate IHDR chunk
    if (length < 33) {
        return MatchResult{
            {FileType::Image, L"png", L"PNG"},
            30,
            flags | MatchFlags::PartialMatch,
            0
        };
    }

    uint32_t ihdr_len = read_be32(data + 8);
    if (ihdr_len != 13 || !has_str(data, length, 12, "IHDR")) {
        return MatchResult{
            {FileType::Image, L"png", L"PNG"},
            35,
            flags | MatchFlags::PartialMatch,
            0
        };
    }

    // Verify IHDR CRC
    bool ihdr_crc_valid = verify_png_chunk_crc(data, 8, length);
    if (ihdr_crc_valid) {
        evidence += 5.0f;  // CRC match is strong evidence
    } else {
        evidence -= 3.0f;  // CRC mismatch reduces confidence
        flags = flags | MatchFlags::PartialMatch;
    }

    evidence += PNG_WEIGHTS.structure_weight;

    uint32_t width = read_be32(data + 16);
    uint32_t height = read_be32(data + 20);
    uint8_t bit_depth = data[24];
    uint8_t color_type = data[25];
    uint8_t compression = data[26];
    uint8_t filter = data[27];
    uint8_t interlace = data[28];

    bool valid_ihdr = width > 0 && height > 0 &&
                      bit_depth >= 1 && bit_depth <= 16 &&
                      (color_type == 0 || color_type == 2 || color_type == 3 ||
                       color_type == 4 || color_type == 6) &&
                      compression == 0 && filter == 0 &&
                      interlace <= 1;

    if (valid_ihdr) {
        evidence += 10.0f;
        flags = flags | MatchFlags::DeepValidated;
    } else {
        flags = flags | MatchFlags::PartialMatch;
    }

    // Phase 3: Scan for chunks including IEND, with CRC verification
    size_t pos = 33;
    bool found_iend = false;
    bool found_idat = false;
    size_t iend_end_pos = 0;  // Position after IEND chunk for verified_file_size
    int crc_errors = 0;
    int chunks_checked = 0;

    while (pos + 12 <= length) {
        uint32_t chunk_len = read_be32(data + pos);

        // Sanity check: chunk shouldn't be > 50MB
        if (chunk_len > 50 * 1024 * 1024) break;

        if (pos + 12 + chunk_len > length) {
            // Incomplete chunk - check if it's IEND or IDAT
            if (pos + 8 <= length) {
                if (has_str(data, length, pos + 4, "IEND")) {
                    found_iend = true;
                    evidence += PNG_WEIGHTS.footer_weight;
                    flags = flags | MatchFlags::HasFooter;
                    iend_end_pos = length;  // Use available length as file size
                } else if (has_str(data, length, pos + 4, "IDAT")) {
                    found_idat = true;
                    evidence += 10.0f;
                }
            }
            break;
        }

        // Verify chunk CRC (except for very large chunks to save time)
        if (chunk_len <= 1024 * 1024) {  // Only verify CRC for chunks <= 1MB
            if (!verify_png_chunk_crc(data, pos, length)) {
                crc_errors++;
            }
            chunks_checked++;
        }

        if (has_str(data, length, pos + 4, "IEND")) {
            found_iend = true;
            evidence += PNG_WEIGHTS.footer_weight;
            flags = flags | MatchFlags::HasFooter;
            iend_end_pos = pos + 12 + chunk_len;  // Exact position after IEND
            break;
        }

        if (has_str(data, length, pos + 4, "IDAT")) {
            found_idat = true;
            evidence += 5.0f;
        }

        if (has_str(data, length, pos + 4, "sRGB") ||
            has_str(data, length, pos + 4, "gAMA") ||
            has_str(data, length, pos + 4, "pHYs") ||
            has_str(data, length, pos + 4, "tEXt") ||
            has_str(data, length, pos + 4, "iTXt") ||
            has_str(data, length, pos + 4, "zTXt")) {
            evidence += 2.0f;
        }

        pos += 12 + chunk_len;
    }

    // Phase 4: CRC error assessment
    if (chunks_checked > 0 && crc_errors > 0) {
        // Reduce confidence based on CRC error rate
        float error_rate = static_cast<float>(crc_errors) / chunks_checked;
        if (error_rate > 0.5f) {
            evidence *= 0.7f;  // Many CRC errors - likely corruption
            flags = flags | MatchFlags::PartialMatch;
        } else if (error_rate > 0.0f) {
            evidence *= 0.9f;  // Some CRC errors - minor corruption
            flags = flags | MatchFlags::PartialMatch;
        }
    }

    // Phase 5: Completeness assessment
    if (!found_idat) {
        flags = flags | MatchFlags::PartialMatch;
        evidence *= 0.6f;
    }

    if (!found_iend) {
        flags = flags | MatchFlags::PartialMatch;
    }

    return MatchResult{
        {FileType::Image, L"png", L"PNG"},
        normalize_confidence(evidence, PNG_WEIGHTS),
        flags,
        iend_end_pos  // verified_file_size: exact position after IEND (0 if not found)
    };
}

} // namespace disk_recover