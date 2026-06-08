#include "jpeg_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// JPEG magic: FF D8 FF (SOI + marker prefix)
static const uint8_t JPEG_MAGIC[] = {0xFF, 0xD8, 0xFF};

// Helper: Check if byte is printable ASCII (used for COM marker validation)
static inline bool is_printable_jpeg(uint8_t c) {
    return (c >= 0x20 && c <= 0x7E);  // Printable ASCII range
}

// ============================================================================
// JPEG Marker State Machine Validator (Three-Phase Model)
//
// Phase 1 (header_check): Parse marker stream from SOI through SOS.
//   - Validates SOF dimensions, precision, components
//   - Checks for JFIF/Exif markers (bonus evidence for real-JPEG)
//   - Strictly validates the fourth byte (following FF D8 FF)
//   - Returns AcceptHeader (SOI only), AcceptStructure (SOF found),
//     or AcceptVerified (SOF+SOS+EOI all found)
//   - Hard rejects if no SOF or SOS (not a decodable JPEG)
//
// Phase 2 (data_check): Walk entropy-coded data looking for EOI.
//   - Scans byte-by-byte respecting FF 00 byte stuffing and RSTn markers
//   - Returns AcceptStructure to continue, AcceptVerified when EOI found
//
// Inspired by PhotoRec's file_jpg.c (2922 lines) which additionally uses
// libjpeg for decompression-based validation. Our version is simpler but
// still covers the essential marker stream and footer detection.
// ============================================================================

// ── Phase 1: Header check ──
ValidateResult check_jpeg_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 10) return ValidateResult::Reject;  // Need enough bytes for validation
    if (data[0] != 0xFF || data[1] != 0xD8 || data[2] != 0xFF) return ValidateResult::Reject;

    calculated_file_size = 0;  // Size unknown until EOI found

    // =========================================================================
    // Strict fourth-byte validation (inspired by PhotoRec file_jpg.c:1021-1047)
    // After FF D8 FF, the fourth byte must be a valid marker identifier.
    // This is critical for avoiding false positives from random data.
    // =========================================================================
    uint8_t fourth_byte = data[3];

    switch (fourth_byte) {
        case 0xE0:  // APP0 - JFIF/JFXX
            // PhotoRec: buffer[6]!='J' || buffer[7]!='F' → reject
            if (length < 8 || data[6] != 'J' || data[7] != 'F') {
                return ValidateResult::Reject;
            }
            break;

        case 0xE1:  // APP1 - Exif
            // PhotoRec: buffer[6-9] must be "Exif"
            if (length < 10 || data[6] != 'E' || data[7] != 'x' ||
                data[8] != 'i' || data[9] != 'f') {
                return ValidateResult::Reject;
            }
            break;

        case 0xE2:  // APP2 - may contain MPF (Multi-Picture Format)
            // Allow through, will be validated later by marker parsing
            break;

        case 0xFE:  // COM - Comment
            // PhotoRec: comment bytes should be printable
            if (length < 8 || (!is_printable_jpeg(data[6]) && !is_printable_jpeg(data[7]))) {
                return ValidateResult::Reject;
            }
            break;

        case 0xDB:  // DQT - Define Quantization Table
            // Valid marker, continue to marker parsing
            break;

        case 0xC0: case 0xC1: case 0xC2: case 0xC3:  // SOF0-SOF3
        case 0xC5: case 0xC6: case 0xC7:             // SOF5-SOF7
        case 0xC9: case 0xCA: case 0xCB:             // SOF9-SOF11
        case 0xCD: case 0xCE: case 0xCF:             // SOF13-SOF15
            // Direct SOF without APP markers - rare but valid
            break;

        case 0xC4:  // DHT - Define Huffman Table
            // Valid marker, will be validated in marker parsing loop
            break;

        case 0xDD:  // DRI - Define Restart Interval
            // Valid marker
            break;

        case 0xDA:  // SOS - Start of Scan (very rare as 4th byte)
            // Extremely unusual but technically possible
            break;

        case 0xD8:  // Another SOI - embedded thumbnail at start?
            // Unusual but possible
            break;

        default:
            // Other markers (0xE3-0xEF APP3-APP15, 0xF0-0xFD reserved)
            // are less common and may indicate false positive
            // PhotoRec rejects most cases via header_ignored()
            if (fourth_byte >= 0xE3 && fourth_byte <= 0xEF) {
                // APP3-APP15 - some cameras use these, allow with caution
                break;
            }
            // Reject everything else as likely false positive
            return ValidateResult::Reject;
    }

    // State tracking
    bool found_sof = false;
    bool found_sos = false;
    bool found_mpo = false;
    bool found_eoi = false;
    size_t last_eoi_pos = 0;

    // Parse marker stream (header section)
    size_t pos = 2;

    while (pos + 2 <= length) {
        if (data[pos] != 0xFF) break;

        // Skip fill bytes (FF FF sequences)
        while (pos < length && data[pos] == 0xFF) ++pos;
        if (pos >= length) break;

        uint8_t marker = data[pos++];

        // ── EOI (FF D9) — standalone marker ──
        if (marker == 0xD9) {
            found_eoi = true;
            last_eoi_pos = pos + 1;
            break;
        }

        // ── SOI (FF D8) — embedded thumbnail ──
        if (marker == 0xD8) continue;

        // ── TEM (FF 01) — standalone marker ──
        if (marker == 0x01) continue;

        // ── RSTn (FF D0-D7) — standalone, no length ──
        if (marker >= 0xD0 && marker <= 0xD7) continue;

        // ── SOS (FF DA) — enters entropy-coded section ──
        if (marker == 0xDA) {
            found_sos = true;

            // Skip SOS segment header
            if (pos + 2 > length) break;
            uint16_t seg_len = read_be16(data + pos);
            if (seg_len < 2) break;
            pos += seg_len;

            // Scan entropy-coded data for EOI
            while (pos + 1 < length) {
                if (data[pos] != 0xFF) { ++pos; continue; }

                uint8_t next = data[pos + 1];

                // FF 00 — byte stuffing
                if (next == 0x00) { pos += 2; continue; }

                // FF D0-D7 — restart markers
                if (next >= 0xD0 && next <= 0xD7) { pos += 2; continue; }

                // FF D9 — EOI found!
                if (next == 0xD9) {
                    found_eoi = true;
                    last_eoi_pos = pos + 2;
                    goto scan_done;
                }

                // FF D8 — embedded thumbnail in entropy section
                if (next == 0xD8) { pos += 2; continue; }

                // Other FF xx — unexpected marker, keep scanning
                ++pos;
            }
            break;
        }

        // ── All other markers have a 2-byte length field ──
        if (pos + 2 > length) break;

        uint16_t seg_len = read_be16(data + pos);
        if (seg_len < 2) break;

        // ── SOF markers (FF C0-CF, excluding C4/C8) ──
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8) {
            if (seg_len < 8) return ValidateResult::Reject;

            if (pos + seg_len <= length) {
                uint8_t precision = data[pos + 2];
                if (precision != 8 && precision != 12) return ValidateResult::Reject;

                uint16_t height = read_be16(data + pos + 3);
                uint16_t width  = read_be16(data + pos + 5);
                uint8_t components = data[pos + 7];

                if (components != 1 && components != 3 && components != 4)
                    return ValidateResult::Reject;

                if (height == 0 || width == 0) return ValidateResult::Reject;

                found_sof = true;
            }
        }

        // ── DHT marker (FF C4) — Huffman table definition ──
        // PhotoRec validates DHT content: invalid tables are a strong
        // indicator of corrupted data masquerading as JPEG.
        if (marker == 0xC4) {
            if (seg_len < 3) return ValidateResult::Reject;  // Too short for DHT

            if (pos + seg_len <= length) {
                uint8_t class_id = data[pos + 2];
                uint8_t huffman_class = class_id >> 4;  // 0=DC, 1=AC
                uint8_t huffman_id    = class_id & 0x0F; // Table ID 0-3

                if (huffman_class > 1) return ValidateResult::Reject;  // Invalid class
                if (huffman_id > 3)    return ValidateResult::Reject;  // Invalid ID

                // Count total symbols: sum of 16 BIT-count bytes at offsets 3-18
                // Total symbols + 19 (class_id + 16 counts) must fit in seg_len
                uint32_t total_symbols = 0;
                for (int j = 0; j < 16 && pos + 3 + j < length; j++) {
                    total_symbols += data[pos + 3 + j];
                }
                if (total_symbols + 19 > seg_len) return ValidateResult::Reject;  // Symbol overflow
            }
        }

        // ── APP markers (FF E0-EF) ──
        if (marker >= 0xE0 && marker <= 0xEF) {
            // Check for APP2/MPF (Multi-Picture Object)
            // MPO files contain multiple JPEG images in a single file
            if (marker == 0xE2 && seg_len >= 6 && pos + seg_len <= length) {
                // MPF identifier: "MPF\0" at start of APP2 data
                if (data[pos + 2] == 'M' && data[pos + 3] == 'P' &&
                    data[pos + 4] == 'F' && data[pos + 5] == '\0') {
                    // This is an MPO file — multiple SOI/EOI pairs expected
                    // The data_check needs to find the LAST EOI, not the first
                    found_mpo = true;
                }
            }
            // Validate APP0 (JFIF) or APP1 (Exif) — strong real-JPEG evidence
            // but not required for AcceptStructure
        }

        pos += seg_len;
    }

scan_done:

    // ── Phase 1 header_check: Accept if SOI magic is valid ──
    // Real-world JPEGs often have large Exif/APP segments that push SOF/SOS
    // beyond the first sector (512 bytes). We should not reject these valid files.
    // Instead, return AcceptHeader and let data_check progressively validate.

    // If we found complete structure in the buffer, return best result
    if (found_sof && found_sos) {
        if (found_eoi && last_eoi_pos >= 64) {
            calculated_file_size = last_eoi_pos;
            return ValidateResult::AcceptVerified;  // Full validation + size known
        }
        return ValidateResult::AcceptStructure;  // Valid JPEG structure but no EOI
    }

    // SOI found but SOF/SOS beyond buffer
    // If pos > 4, we parsed at least one marker segment (like APP0/Exif)
    // This might be a real JPEG with large header beyond buffer
    if (pos > 4) {
        return ValidateResult::AcceptHeader;
    }
    // pos stopped at 2-3 means SOI only, no subsequent markers
    // Almost certainly a false positive - random data after FFD8FF
    return ValidateResult::Reject;
}

// ── Phase 2: Data check (progressive EOI search) ──
// Called by scanner per block during carving. Scans for EOI marker
// respecting byte stuffing (FF 00) and restart markers (FF D0-D7).
// Also tracks SOF/SOS presence for validation.
ValidateResult check_jpeg_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // Track if we find SOF/SOS in this block (for validation)
    bool found_sof = false;
    bool found_sos = false;

    // Scan for markers in this block
    for (size_t i = 0; i + 1 < length; ++i) {
        if (data[i] != 0xFF) continue;

        uint8_t next = data[i + 1];

        // FF 00 — byte stuffing, skip
        if (next == 0x00) continue;

        // FF D0-D7 — restart markers, skip
        if (next >= 0xD0 && next <= 0xD7) continue;

        // FF D8 — embedded SOI (thumbnail), skip
        if (next == 0xD8) continue;

        // FF D9 — EOI found!
        if (next == 0xD9) {
            // Check if this is the final EOI or an embedded thumbnail's EOI.
            // If the next bytes after EOI are another SOI (FF D8), this EOI
            // belongs to an embedded thumbnail — skip and continue scanning.
            // This aligns with the scanner's find_jpeg_eoi "last EOI" strategy.
            if (i + 3 < length && data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                // Embedded thumbnail EOI — skip past it, continue looking for final EOI
                i += 2;
                continue;
            }
            // Final EOI — file boundary found
            calculated_file_size = offset_in_file + i + 2;
            return ValidateResult::AcceptVerified;
        }

        // FF C0-CF (except C4, C8) — SOF markers
        if (next >= 0xC0 && next <= 0xCF && next != 0xC4 && next != 0xC8) {
            found_sof = true;
        }

        // FF DA — SOS marker
        if (next == 0xDA) {
            found_sos = true;
        }
    }

    // No EOI found in this block — keep carving
    // Return AcceptStructure if we found key markers, AcceptHeader otherwise
    if (found_sof || found_sos) {
        return ValidateResult::AcceptStructure;
    }
    return ValidateResult::AcceptHeader;
}

// ── Phase 3: File check (full file re-validation) ──
// Re-validates the entire JPEG file from disk: verifies marker structure,
// finds EOI, and checks minimal structural integrity.
ValidateResult check_jpeg_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 4) return ValidateResult::Reject;

    // Re-verify SOI marker
    if (data[0] != 0xFF || data[1] != 0xD8) return ValidateResult::Reject;

    // Walk the marker stream
    size_t i = 2;
    bool has_sof = false;
    bool has_sos = false;
    bool in_entropy = false;
    const size_t MAX_MARKER_ITERATIONS = 1000000;  // Safety limit

    for (size_t iters = 0; iters < MAX_MARKER_ITERATIONS && i < length; ++iters) {
        if (in_entropy) {
            // Scanning compressed data for markers
            if (data[i] != 0xFF) {
                i++;
                continue;
            }
            // Found FF — check next byte
            if (i + 1 >= length) break;
            uint8_t next = data[i + 1];

            if (next == 0x00) { i += 2; continue; }  // Byte stuffing
            if (next >= 0xD0 && next <= 0xD7) { i += 2; continue; }  // RST markers

            // EOI marker
            if (next == 0xD9) {
                // Check for embedded thumbnail: FF D8 after EOI
                if (i + 3 < length && data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                    // Embedded thumbnail EOI — skip past it, continue
                    i += 2;
                    in_entropy = false;
                    continue;
                }
                // Final EOI
                calculated_file_size = i + 2;
                // Must have at least SOF + SOS to be valid
                if (!has_sof || !has_sos) return ValidateResult::Reject;
                return ValidateResult::AcceptVerified;
            }

            // Another marker found (SOS, SOF, APP, etc.) — exit entropy mode
            in_entropy = false;
            // Fall through to marker processing below
            i++;  // Position at the FF
            continue;
        }

        // Not in entropy data — looking for markers
        if (data[i] != 0xFF) return ValidateResult::Reject;  // Expected marker prefix

        // Skip padding FF bytes
        while (i < length && data[i] == 0xFF) i++;
        if (i >= length) break;

        uint8_t marker = data[i];
        i++;

        // SOF markers (0xC0-0xC3, 0xC5-0xC7, 0xC9-0xCB, 0xCD-0xCF)
        if ((marker >= 0xC0 && marker <= 0xC3) ||
            (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) ||
            (marker >= 0xCD && marker <= 0xCF)) {
            has_sof = true;
            if (i + 2 > length) break;
            uint16_t seg_len = read_be16(data + i);
            i += seg_len;
            continue;
        }

        // SOS marker (0xDA) — start of scan, followed by entropy data
        if (marker == 0xDA) {
            has_sos = true;
            if (i + 2 > length) break;
            uint16_t seg_len = read_be16(data + i);
            i += seg_len;
            in_entropy = true;
            continue;
        }

        // EOI marker
        if (marker == 0xD9) {
            // Check for embedded thumbnail
            if (i + 1 < length && data[i] == 0xFF && data[i + 1] == 0xD8) {
                i += 2;
                continue;
            }
            calculated_file_size = i;
            if (!has_sof || !has_sos) return ValidateResult::Reject;
            return ValidateResult::AcceptVerified;
        }

        // SOI marker (0xD8) — start of embedded thumbnail, skip
        if (marker == 0xD8) continue;

        // Marker with length field (APP, DQT, DHT, DRI, COM, etc.)
        if ((marker >= 0xC0 && marker <= 0xFE) && marker != 0xFF && marker != 0xD8 && marker != 0xD9) {
            if (i + 2 > length) break;
            uint16_t seg_len = read_be16(data + i);
            if (seg_len < 2) return ValidateResult::Reject;  // Invalid segment length
            i += seg_len;
            continue;
        }

        // Other markers (RST, TEM, etc.) — no length field
        continue;
    }

    // No EOI found or file truncated
    if (has_sof && has_sos)
        return ValidateResult::AcceptStructure;  // Valid structure but truncated

    return ValidateResult::Reject;
}

} // anonymous namespace

const FormatDescriptor JPEG_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"jpg",
    .description     = L"JPEG image",
    .min_filesize    = 256,  // Minimum reasonable JPEG file size
    .max_filesize    = 0,
    .signature       = {JPEG_MAGIC, nullptr, 3, 0, 0},
    .header_check    = check_jpeg_header_impl,
    .data_check      = check_jpeg_data_impl,  // Progressive EOI search
    .file_check      = check_jpeg_file_impl,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_jpeg_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_jpeg_header_impl(data, length, calculated_file_size);
}

ValidateResult check_jpeg_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_jpeg_data_impl(data, length, offset_in_file, calculated_file_size);
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_jpeg(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_jpeg_header_impl(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptVerified)   mr.flags = mr.flags | MatchFlags::HasFooter | MatchFlags::DeepValidated;
    mr.verified_file_size = calculated_file_size;
    mr.signature = {FileType::Image, L"jpg", L"JPEG"};
    return mr;
}

} // namespace disk_recover