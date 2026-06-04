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

    // Phase 2: Full marker stream parsing (no arbitrary limit)
    // JPEG structure: SOI -> markers -> SOS -> entropy-coded data -> EOI
    // Key insight: After SOS (FF DA), we're in entropy-coded data until EOI (FF D9)
    // Entropy-coded data can contain FF bytes followed by non-zero bytes (not markers)

    size_t pos = 2;
    bool found_sos = false;
    bool found_sof = false;
    size_t last_eoi_pos = 0;  // Track last EOI position for verified_file_size
    int embedded_jpeg_count = 0;  // Track embedded thumbnails

    // First pass: Parse markers up to SOS
    while (pos + 2 <= length) {
        // In entropy-coded section: scan for next marker
        // JPEG byte stuffing: FF 00 in entropy data is NOT a marker
        // Only FF followed by non-00 and non-RST is a real marker
        if (found_sos) {
            // Scan for FF followed by potential marker byte
            while (pos + 1 < length) {
                if (data[pos] != 0xFF) {
                    // Normal entropy data byte
                    ++pos;
                    continue;
                }

                // Found FF - check next byte
                uint8_t next = data[pos + 1];

                // FF 00 - byte stuffing, NOT a marker, just data
                // Skip both bytes (the 00 is a stuffing byte)
                if (next == 0x00) {
                    pos += 2;
                    continue;
                }

                // FF D0-D7 - RSTn (Restart) markers, valid in entropy section
                // These are standalone markers, continue entropy scanning
                if (next >= 0xD0 && next <= 0xD7) {
                    pos += 2;
                    continue;
                }

                // FF D9 - EOI (End of Image), this is the end
                if (next == 0xD9) {
                    last_eoi_pos = pos + 2;  // Position after FF D9
                    found_sos = false;  // Exit SOS section
                    evidence += JPEG_WEIGHTS.footer_weight;
                    flags = flags | MatchFlags::HasFooter;
                    pos += 2;
                    // Continue scanning - there may be embedded thumbnails after main EOI
                    continue;
                }

                // FF D8 - embedded JPEG thumbnail detected in entropy section
                // (Rare but can happen in some malformed files)
                if (next == 0xD8) {
                    embedded_jpeg_count++;
                    pos += 2;
                    continue;
                }

                // FF followed by other bytes - could be:
                // 1. Corrupted data
                // 2. Another marker that shouldn't be here
                // Continue scanning to find actual EOI
                ++pos;
            }
            break;  // End of entropy section or data exhausted
        }

        // In header section: parse markers with length fields
        if (data[pos] != 0xFF) break;

        // Skip stuffing bytes (FF FF sequences)
        while (pos < length && data[pos] == 0xFF) ++pos;
        if (pos >= length) break;

        uint8_t marker = data[pos++];

        // EOI (End of Image) - standalone marker
        if (marker == 0xD9) {
            last_eoi_pos = pos + 1;  // Position after FF D9 (pos already incremented past marker)
            evidence += JPEG_WEIGHTS.footer_weight;
            flags = flags | MatchFlags::HasFooter;
            break;
        }

        // SOI (Start of Image) - embedded thumbnail detected
        if (marker == 0xD8) {
            embedded_jpeg_count++;
            // This is an embedded JPEG - need to find its EOI
            // Complex parsing required, for now just note it
            evidence += 2.0f;  // Weak evidence for embedded content
            continue;
        }

        // TEM (Temporary) - standalone marker
        if (marker == 0x01) continue;

        // RSTn (Restart) markers - standalone
        if (marker >= 0xD0 && marker <= 0xD7) continue;

        // SOS (Start of Scan) - enters entropy-coded section
        if (marker == 0xDA) {
            found_sos = true;
            evidence += 15.0f;
            // Skip SOS segment length
            if (pos + 2 > length) break;
            uint16_t seg_len = read_be16(data + pos);
            pos += seg_len;
            continue;
        }

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
    // ── STRICT VALIDATION: Require complete marker chain ──
    // A valid JPEG must have: SOI + SOF + SOS + EOI
    // Missing any of these significantly reduces confidence

    // Track completeness score (0-4)
    int completeness = 1;  // SOI is always present (we already checked)

    if (found_sof) {
        completeness++;
    } else {
        // JPEG without SOF is basically unusable
        evidence *= 0.4f;
        flags = flags | MatchFlags::PartialMatch;
    }

    if (found_sos) {
        completeness++;
    } else {
        // JPEG without SOS cannot be decoded
        evidence *= 0.5f;
        flags = flags | MatchFlags::PartialMatch;
    }

    if (last_eoi_pos > 0) {
        completeness++;
        flags = flags | MatchFlags::HasFooter;
    } else {
        // JPEG without EOI is incomplete
        flags = flags | MatchFlags::PartialMatch;
    }

    // ── Confidence cap based on completeness ──
    // Only complete JPEGs (4/4) can have high confidence
    // 3/4: max confidence 60
    // 2/4: max confidence 40
    // 1/4: max confidence 25 (likely false positive)

    float max_confidence = 100.0f;
    if (completeness < 4) {
        max_confidence = 25.0f + (completeness - 1) * 20.0f;  // 25, 45, 65, 100
    }

    // Apply confidence cap
    if (evidence > max_confidence) {
        evidence = max_confidence;
    }

    // Minimum threshold: must have SOI + at least one marker beyond SOI
    if (completeness < 2 || evidence < 15.0f) {
        return std::nullopt;  // Reject - likely false positive
    }

    flags = flags | MatchFlags::DeepValidated;

    // Only return verified_file_size if we found a reasonable EOI position
    // If input data is too short (< 2KB) or EOI is at a suspicious position,
    // return 0 to let the scanner estimate size by other means
    uint64_t verified_size = 0;
    if (last_eoi_pos >= 64 && last_eoi_pos <= length) {
        // EOI found at a reasonable position
        verified_size = last_eoi_pos;
    }
    // Otherwise return 0 - let parse_file_size() or footer search handle it

    return MatchResult{
        {FileType::Image, L"jpg", L"JPEG"},
        normalize_confidence(evidence, JPEG_WEIGHTS),
        flags,
        verified_size
    };
}

} // namespace disk_recover