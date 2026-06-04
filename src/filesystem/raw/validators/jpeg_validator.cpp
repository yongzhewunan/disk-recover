#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_jpeg(const uint8_t* data, size_t length) {
    // Phase 1: SOI (Start of Image) marker check
    if (length < 4) return std::nullopt;
    if (data[0] != 0xFF || data[1] != 0xD8) return std::nullopt;

    float evidence = JPEG_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: Scan marker stream
    size_t scan_limit = std::min(length, static_cast<size_t>(65536));

    size_t pos = 2;
    bool found_sos = false;
    bool found_sof = false;
    bool found_eoi = false;

    while (pos + 2 <= length && pos < scan_limit) {
        if (data[pos] != 0xFF) break;

        // Skip stuffing bytes (FF FF sequences)
        while (pos < length && data[pos] == 0xFF) ++pos;
        if (pos >= length) break;

        uint8_t marker = data[pos++];

        // EOI (End of Image) - standalone marker, no length
        if (marker == 0xD9) {
            found_eoi = true;
            evidence += JPEG_WEIGHTS.footer_weight;
            flags = flags | MatchFlags::HasFooter;
            break;
        }

        // TEM (Temporary) - standalone marker
        if (marker == 0x01) continue;

        // RSTn (Restart) markers - standalone
        if (marker >= 0xD0 && marker <= 0xD7) continue;

        // All other markers have a 2-byte length field
        if (pos + 2 > length) break;

        uint16_t seg_len = read_be16(data + pos);
        if (seg_len < 2) break;

        // SOF (Start of Frame) markers
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8) {
            found_sof = true;
            evidence += 12.0f;

            if (marker == 0xC2) evidence += 3.0f;  // Progressive

            if (seg_len >= 8 && pos + seg_len <= length) {
                uint8_t precision = data[pos + 2];
                uint16_t height = read_be16(data + pos + 3);
                uint16_t width = read_be16(data + pos + 5);
                uint8_t components = data[pos + 7];

                if (precision >= 8 && precision <= 12 &&
                    height > 0 && width > 0 &&
                    components >= 1 && components <= 4) {
                    evidence += 5.0f;
                }
            }
        }

        // APP markers
        if (marker >= 0xE0 && marker <= 0xEF) {
            evidence += 8.0f;

            if (seg_len >= 6 && pos + seg_len <= length) {
                size_t app_pos = pos + 2;

                if (marker == 0xE0 && app_pos + 5 <= length) {
                    if (has_str(data, length, app_pos, "JFIF")) {
                        evidence += 10.0f;
                    }
                }
                if (marker == 0xE1 && app_pos + 6 <= length) {
                    if (has_str(data, length, app_pos, "Exif\x00")) {
                        evidence += 10.0f;
                    }
                }
                if (marker == 0xEE && app_pos + 6 <= length) {
                    if (has_str(data, length, app_pos, "Adobe")) {
                        evidence += 10.0f;
                    }
                }
            }
        }

        if (marker == 0xC4) evidence += 5.0f;  // DHT
        if (marker == 0xDB) evidence += 5.0f;  // DQT
        if (marker == 0xDD) evidence += 3.0f;  // DRI
        if (marker == 0xFE) evidence += 2.0f;  // COM

        pos += seg_len;
    }

    // Phase 3: Validation decision
    // Critical: JPEG without SOF is basically unusable
    if (!found_sof) {
        evidence *= 0.5f;
        flags = flags | MatchFlags::PartialMatch;
    }

    // Critical: JPEG without SOS cannot be decoded
    if (!found_sos) {
        evidence *= 0.7f;
        flags = flags | MatchFlags::PartialMatch;
    }

    // Critical: JPEG without EOI is incomplete
    if (!found_eoi) {
        flags = flags | MatchFlags::PartialMatch;
    }

    // Minimum threshold: must have SOI marker
    // For file carving with minimal data, accept SOI + any marker
    if (evidence < 10.0f) {
        return std::nullopt;
    }

    flags = flags | MatchFlags::DeepValidated;

    return MatchResult{
        {FileType::Image, L"jpg", L"JPEG"},
        normalize_confidence(evidence, JPEG_WEIGHTS),
        flags
    };
}

} // namespace disk_recover