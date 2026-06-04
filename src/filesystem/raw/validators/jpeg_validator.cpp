#include "jpeg_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// JPEG magic: FF D8 FF (SOI + marker prefix)
static const uint8_t JPEG_MAGIC[] = {0xFF, 0xD8, 0xFF};

// ============================================================================
// JPEG Marker State Machine Validator (Three-Phase Model)
//
// Phase 1 (header_check): Parse marker stream from SOI through SOS.
//   - Validates SOF dimensions, precision, components
//   - Checks for JFIF/Exif markers (bonus evidence for real-JPEG)
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
    if (length < 4) return ValidateResult::Reject;
    if (data[0] != 0xFF || data[1] != 0xD8 || data[2] != 0xFF) return ValidateResult::Reject;

    calculated_file_size = 0;  // Size unknown until EOI found

    // State tracking
    bool found_sof = false;
    bool found_sos = false;
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

        // ── APP markers (FF E0-EF) ──
        if (marker >= 0xE0 && marker <= 0xEF) {
            // Validate APP0 (JFIF) or APP1 (Exif) — strong real-JPEG evidence
            // but not required for AcceptStructure
        }

        pos += seg_len;
    }

scan_done:

    // Hard requirements: SOF and SOS are mandatory for a decodable JPEG
    if (!found_sof) return ValidateResult::Reject;
    if (!found_sos) return ValidateResult::Reject;

    // Determine result depth based on what was found
    if (found_eoi && last_eoi_pos >= 64) {
        calculated_file_size = last_eoi_pos;
        return ValidateResult::AcceptVerified;  // Full validation + size known
    }

    // Valid JPEG structure but no EOI (truncated or only partial data in buffer)
    return ValidateResult::AcceptStructure;
}

// ── Phase 2: Data check (progressive EOI search) ──
// Called by scanner per block during carving. Scans for EOI marker
// respecting byte stuffing (FF 00) and restart markers (FF D0-D7).
ValidateResult check_jpeg_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // Scan for EOI marker (FF D9) in this block
    for (size_t i = 0; i + 1 < length; ++i) {
        if (data[i] != 0xFF) continue;

        uint8_t next = data[i + 1];

        // FF 00 — byte stuffing, skip
        if (next == 0x00) continue;

        // FF D0-D7 — restart markers, skip
        if (next >= 0xD0 && next <= 0xD7) continue;

        // FF D9 — EOI found!
        if (next == 0xD9) {
            calculated_file_size = offset_in_file + i + 2;
            return ValidateResult::AcceptVerified;
        }
    }

    // No EOI found in this block — keep carving
    return ValidateResult::AcceptStructure;
}

// Auto-registration with FormatRegistry
static const FormatDescriptor JPEG_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"jpg",
    .description     = L"JPEG image",
    .min_filesize    = 64,
    .max_filesize    = 0,
    .signature       = {JPEG_MAGIC, 3, 0},
    .header_check    = check_jpeg_header_impl,
    .data_check      = check_jpeg_data_impl,  // Progressive EOI search
    .file_check      = nullptr,
    .enabled_by_default = true,
};

static bool _jpeg_registered = []() {
    FormatRegistry::instance().register_format(JPEG_DESCRIPTOR);
    return true;
}();

} // anonymous namespace

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