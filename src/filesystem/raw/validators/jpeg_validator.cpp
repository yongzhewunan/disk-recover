#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

// ============================================================================
// JPEG Marker State Machine Validator
//
// Scoring model (file carving optimized):
//   SOI   = 15  (mandatory, already checked before entry)
//   APP0/1= 10  (JFIF/Exif — strong real-JPEG evidence)
//   DQT   = 10  (quantization table)
//   DHT   = 10  (Huffman table)
//   SOF   = 20  (Start of Frame — hard requirement)
//   SOS   = 20  (Start of Scan — hard requirement)
//   EOI   = 15  (End of Image)
// Total = 100 (perfect score)
//
// Hard requirements for high confidence:
//   - Must have SOF + SOS (otherwise not a decodable JPEG)
//   - Without EOI: evidence *= 0.3, confidence capped at 40
// ============================================================================

std::optional<MatchResult> validate_jpeg(const uint8_t* data, size_t length) {
    // Phase 0: SOI (Start of Image) — already guaranteed by routing
    if (length < 4) return std::nullopt;
    if (data[0] != 0xFF || data[1] != 0xD8) return std::nullopt;

    float evidence = JPEG_WEIGHTS.header_weight;  // 15
    MatchFlags flags = MatchFlags::HasHeader;

    // State tracking
    bool found_sof = false;
    bool found_sos = false;
    bool found_eoi = false;
    size_t last_eoi_pos = 0;

    // Phase 1: Parse marker stream (header section)
    // JPEG structure: SOI → [markers] → SOS → entropy data → EOI
    size_t pos = 2;

    while (pos + 2 <= length) {
        // Expect FF at current position
        if (data[pos] != 0xFF) break;

        // Skip fill bytes (FF FF sequences)
        while (pos < length && data[pos] == 0xFF) ++pos;
        if (pos >= length) break;

        uint8_t marker = data[pos++];

        // ── EOI (FF D9) — standalone marker ──
        if (marker == 0xD9) {
            found_eoi = true;
            last_eoi_pos = pos + 1;  // pos already past D9 byte
            evidence += JPEG_WEIGHTS.footer_weight;  // 15
            break;
        }

        // ── SOI (FF D8) — embedded thumbnail detected ──
        if (marker == 0xD8) {
            // Embedded JPEG thumbnail inside Exif — skip for now
            continue;
        }

        // ── TEM (FF 01) — standalone marker ──
        if (marker == 0x01) continue;

        // ── RSTn (FF D0-D7) — standalone, no length ──
        if (marker >= 0xD0 && marker <= 0xD7) continue;

        // ── SOS (FF DA) — enters entropy-coded section ──
        if (marker == 0xDA) {
            found_sos = true;
            evidence += 20.0f;  // SOS = 20

            // Skip SOS segment header (length field + components)
            if (pos + 2 > length) break;
            uint16_t seg_len = read_be16(data + pos);
            if (seg_len < 2) break;
            pos += seg_len;

            // ── Phase 2: Scan entropy-coded data for EOI ──
            // In entropy section, FF 00 is byte stuffing (NOT a marker)
            // FF D0-D7 are restart markers (standalone, skip)
            // FF D9 is EOI (the real end)
            while (pos + 1 < length) {
                if (data[pos] != 0xFF) {
                    ++pos;
                    continue;
                }

                uint8_t next = data[pos + 1];

                // FF 00 — byte stuffing, skip both bytes
                if (next == 0x00) {
                    pos += 2;
                    continue;
                }

                // FF D0-D7 — RSTn, continue entropy scanning
                if (next >= 0xD0 && next <= 0xD7) {
                    pos += 2;
                    continue;
                }

                // FF D9 — EOI found!
                if (next == 0xD9) {
                    found_eoi = true;
                    last_eoi_pos = pos + 2;
                    evidence += JPEG_WEIGHTS.footer_weight;  // 15
                    pos += 2;
                    // Don't break — there may be embedded thumbnails after main EOI
                    // But for file size purposes, this is the first real EOI
                    goto scan_done;
                }

                // FF D8 — embedded thumbnail in entropy section (rare but possible)
                if (next == 0xD8) {
                    pos += 2;
                    continue;
                }

                // Other FF xx — unexpected marker in entropy section
                // Could be corruption or end of scan. Continue scanning.
                ++pos;
            }
            break;  // End of data
        }

        // ── All other markers have a 2-byte length field ──
        if (pos + 2 > length) break;

        uint16_t seg_len = read_be16(data + pos);
        if (seg_len < 2) break;

        // ── SOF markers (FF C0-CF, excluding C4/D8) ──
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8) {
            // Validate SOF segment
            if (seg_len < 8) return std::nullopt;  // Invalid SOF length → reject

            if (pos + seg_len <= length) {
                uint8_t precision = data[pos + 2];

                // Only 8-bit and 12-bit JPEG are valid
                if (precision != 8 && precision != 12) return std::nullopt;

                uint16_t height = read_be16(data + pos + 3);
                uint16_t width  = read_be16(data + pos + 5);
                uint8_t components = data[pos + 7];

                // Components must be 1 (grayscale), 3 (YCbCr), or 4 (CMYK)
                if (components != 1 && components != 3 && components != 4)
                    return std::nullopt;

                // Validate dimensions
                if (height == 0 || width == 0) return std::nullopt;

                found_sof = true;
                evidence += 20.0f;  // SOF = 20

                if (marker == 0xC2) evidence += 3.0f;  // Progressive — bonus
            }
        }

        // ── APP markers (FF E0-EF) ──
        if (marker >= 0xE0 && marker <= 0xEF) {
            evidence += 5.0f;

            if (seg_len >= 6 && pos + seg_len <= length) {
                size_t app_pos = pos + 2;
                // JFIF
                if (marker == 0xE0 && app_pos + 5 <= length &&
                    has_str(data, length, app_pos, "JFIF")) {
                    evidence += 10.0f;
                }
                // Exif
                if (marker == 0xE1 && app_pos + 6 <= length &&
                    has_str(data, length, app_pos, "Exif\x00")) {
                    evidence += 10.0f;
                }
                // Adobe
                if (marker == 0xEE && app_pos + 6 <= length &&
                    has_str(data, length, app_pos, "Adobe")) {
                    evidence += 10.0f;
                }
            }
        }

        // ── DHT (FF C4) ──
        if (marker == 0xC4) evidence += 10.0f;

        // ── DQT (FF DB) ──
        if (marker == 0xDB) evidence += 10.0f;

        // ── DRI (FF DD) ──
        if (marker == 0xDD) evidence += 3.0f;

        // ── COM (FF FE) ──
        if (marker == 0xFE) evidence += 2.0f;

        pos += seg_len;
    }

scan_done:

    // ═══════════════════════════════════════════════════════
    // Phase 3: Hard requirements + confidence assignment
    // ═══════════════════════════════════════════════════════

    // SOF is mandatory — without it this is NOT a usable JPEG
    if (!found_sof) return std::nullopt;

    // SOS is mandatory — without it this cannot be decoded
    if (!found_sos) return std::nullopt;

    // EOI: missing EOI means truncated/incomplete JPEG
    if (!found_eoi) {
        evidence *= 0.3f;
        flags = flags | MatchFlags::PartialMatch;
    }

    flags = flags | MatchFlags::DeepValidated;

    // verified_file_size: only return if EOI found at reasonable position
    uint64_t verified_size = 0;
    if (found_eoi && last_eoi_pos >= 64 && last_eoi_pos <= length) {
        verified_size = last_eoi_pos;
    }

    return MatchResult{
        {FileType::Image, L"jpg", L"JPEG"},
        normalize_confidence(evidence, JPEG_WEIGHTS),
        flags,
        verified_size
    };
}

} // namespace disk_recover
