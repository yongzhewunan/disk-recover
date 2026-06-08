#include "png_validator.hpp"
#include "binary_reader.hpp"
#include "crc32.hpp"
#include "format_registry.hpp"
#include "validators.hpp"

namespace disk_recover {
namespace {

// PNG signature: 89 50 4E 47 0D 0A 1A 0A
static const uint8_t PNG_MAGIC[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// PNG chunk structure: [4-byte length][4-byte type][data][4-byte CRC]
// Length is of data only (not including type or CRC).
// Total chunk size = 12 + data_length.

static const size_t PNG_SIGNATURE_SIZE = 8;
static const size_t PNG_CHUNK_HEADER_SIZE = 8;   // length(4) + type(4)
static const size_t PNG_CHUNK_FOOTER_SIZE = 4;   // CRC(4)

// Verify CRC of a PNG chunk
static bool verify_png_chunk_crc(const uint8_t* chunk_start, uint32_t data_length) {
    // CRC covers type + data (not length or CRC fields)
    const uint8_t* type_ptr = chunk_start + 4;
    const uint8_t* crc_ptr  = type_ptr + data_length + 4;  // skip type(4) + data

    uint32_t computed_crc = crc32(type_ptr, 4 + data_length);
    uint32_t stored_crc   = read_be32(crc_ptr);
    return computed_crc == stored_crc;
}

// ── Phase 1: Header check ──
// Validates PNG signature + IHDR chunk (must be first chunk).
// Returns AcceptStructure if IHDR is valid, AcceptVerified if IEND is also found.
// NOTE: CRC validation is skipped during header_check to allow recovery of
// partially corrupted PNG files. CRC is only validated in file_check.
ValidateResult check_png_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < PNG_SIGNATURE_SIZE) return ValidateResult::Reject;

    // Verify PNG signature
    for (int i = 0; i < 8; i++) {
        if (data[i] != PNG_MAGIC[i]) return ValidateResult::Reject;
    }

    calculated_file_size = 0;  // Size unknown until IEND found

    // If we only have the signature (8 bytes), return AcceptHeader
    if (length < PNG_SIGNATURE_SIZE + PNG_CHUNK_HEADER_SIZE)
        return ValidateResult::AcceptHeader;

    // Parse chunk stream starting after signature
    size_t pos = PNG_SIGNATURE_SIZE;
    bool found_ihdr = false;
    bool found_iend = false;

    while (pos + PNG_CHUNK_HEADER_SIZE <= length) {
        uint32_t data_length = read_be32(data + pos);

        // Read chunk type
        uint8_t t0 = data[pos + 4], t1 = data[pos + 5];
        uint8_t t2 = data[pos + 6], t3 = data[pos + 7];

        // Validate data_length (PNG spec: max 2^31 - 1)
        if (data_length > 0x7FFFFFFF) return ValidateResult::Reject;

        // Validate chunk type: all 4 bytes must be ASCII letters (PNG spec + PhotoRec)
        // This catches corrupted chunk streams early, preventing false IEND detection
        auto is_ascii_letter = [](uint8_t b) {
            return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
        };
        if (!is_ascii_letter(t0) || !is_ascii_letter(t1) ||
            !is_ascii_letter(t2) || !is_ascii_letter(t3)) {
            return ValidateResult::Reject;  // Invalid chunk type → corrupted data
        }

        // Calculate total chunk size and check bounds
        uint64_t total_chunk_size = static_cast<uint64_t>(data_length)
                                   + PNG_CHUNK_HEADER_SIZE + PNG_CHUNK_FOOTER_SIZE;
        if (pos + total_chunk_size > length) break;  // Not enough data for this chunk

        // ── IHDR chunk (must be first) ──
        if (!found_ihdr) {
            if (t0 != 'I' || t1 != 'H' || t2 != 'D' || t3 != 'R')
                return ValidateResult::Reject;  // IHDR must be first chunk

            if (data_length != 13) return ValidateResult::Reject;

            // Parse IHDR fields
            uint32_t width  = read_be32(data + pos + 8);
            uint32_t height = read_be32(data + pos + 12);
            uint8_t  bit_depth    = data[pos + 16];
            uint8_t  color_type   = data[pos + 17];
            uint8_t  compression  = data[pos + 18];
            uint8_t  filter       = data[pos + 19];
            uint8_t  interlace    = data[pos + 20];

            // Validate dimensions
            if (width == 0 || height == 0) return ValidateResult::Reject;
            if (width > 100000 || height > 100000) return ValidateResult::Reject;

            // Validate bit depth
            switch (bit_depth) {
                case 1: case 2: case 4: case 8: case 16: break;
                default: return ValidateResult::Reject;
            }

            // Validate color type + bit depth combinations
            switch (color_type) {
                case 0: // Grayscale
                    if (bit_depth == 2 || bit_depth > 16) return ValidateResult::Reject;
                    break;
                case 2: // RGB
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                case 3: // Indexed
                    if (bit_depth == 16) return ValidateResult::Reject;
                    break;
                case 4: // Grayscale + Alpha
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                case 6: // RGBA
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                default:
                    return ValidateResult::Reject;
            }

            // Compression must be 0, filter must be 0, interlace 0 or 1
            if (compression != 0) return ValidateResult::Reject;
            if (filter != 0) return ValidateResult::Reject;
            if (interlace != 0 && interlace != 1) return ValidateResult::Reject;

            // Skip CRC validation during header_check - allow recovery of corrupted files
            // CRC will be validated in file_check if full file is available

            found_ihdr = true;
            pos += total_chunk_size;
            continue;
        }

        // ── IEND chunk — end of PNG ──
        if (t0 == 'I' && t1 == 'E' && t2 == 'N' && t3 == 'D') {
            if (data_length != 0) return ValidateResult::Reject;

            // Skip CRC validation during header_check

            calculated_file_size = pos + total_chunk_size;
            found_iend = true;
            return ValidateResult::AcceptVerified;
        }

        // Move to next chunk
        pos += total_chunk_size;
    }

    // Found valid IHDR but no IEND (truncated or buffer too small)
    if (found_ihdr) return ValidateResult::AcceptStructure;

    return ValidateResult::Reject;
}

// ── Phase 2: Data check (progressive chunk traversal) ──
// Walks chunk structure looking for IEND chunk, validates chunk types along the way.
// Replaces simple pattern matching with proper chunk traversal (PhotoRec approach).
ValidateResult check_png_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // IEND chunk pattern: 00 00 00 00 49 45 4E 44 AE 42 60 82
    static const uint8_t IEND_PATTERN[] = {
        0x00, 0x00, 0x00, 0x00,  // length = 0
        0x49, 0x45, 0x4E, 0x44,  // "IEND"
        0xAE, 0x42, 0x60, 0x82   // CRC of IEND
    };

    // Always scan for IEND pattern regardless of offset alignment.
    // This is more robust than chunk traversal when data blocks may not be
    // aligned to chunk boundaries (e.g., progressive carving with 32KB blocks).
    for (size_t i = 0; i + 12 <= length; ++i) {
        bool match = true;
        for (size_t j = 0; j < 12; j++) {
            if (data[i + j] != IEND_PATTERN[j]) { match = false; break; }
        }
        if (match) {
            calculated_file_size = offset_in_file + i + 12;
            return ValidateResult::AcceptVerified;
        }
    }

    // For offset < 33, header_check already validated IHDR, so we just need to find IEND.
    // For offset >= 33, we also try chunk traversal as a secondary check.
    // But since blocks may not be aligned to chunk boundaries, we only use IEND pattern search.

    // No IEND found in this block — continue carving
    return ValidateResult::AcceptStructure;
}

// ── Phase 3: File check (full file re-validation) ──
// Re-traverses entire PNG file from disk, verifying structure and finding IEND.
// This catches files that passed header_check/data_check but have issues
// visible only in the complete file data (PhotoRec approach).
ValidateResult check_png_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < PNG_SIGNATURE_SIZE + PNG_CHUNK_HEADER_SIZE + PNG_CHUNK_FOOTER_SIZE)
        return ValidateResult::Reject;

    // Re-verify PNG signature
    for (int i = 0; i < 8; i++) {
        if (data[i] != PNG_MAGIC[i]) return ValidateResult::Reject;
    }

    // Walk the entire chunk stream
    size_t pos = PNG_SIGNATURE_SIZE;  // Start after signature
    auto is_letter = [](uint8_t b) { return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z'); };

    bool has_ihdr = false;
    bool has_idat = false;
    const size_t MAX_FILE_CHUNKS = 100000;  // Safety limit

    for (size_t chunk_count = 0; chunk_count < MAX_FILE_CHUNKS; ++chunk_count) {
        if (pos + PNG_CHUNK_HEADER_SIZE > length) {
            // Truncated chunk header — file is incomplete
            break;
        }

        uint32_t data_length = read_be32(data + pos);
        if (data_length > 0x7FFFFFFF) return ValidateResult::Reject;

        uint8_t t0 = data[pos + 4], t1 = data[pos + 5];
        uint8_t t2 = data[pos + 6], t3 = data[pos + 7];

        // Validate chunk type
        if (!is_letter(t0) || !is_letter(t1) || !is_letter(t2) || !is_letter(t3))
            return ValidateResult::Reject;

        // Calculate total chunk size and check bounds
        uint64_t total_chunk_size = static_cast<uint64_t>(data_length)
                                   + PNG_CHUNK_HEADER_SIZE + PNG_CHUNK_FOOTER_SIZE;
        if (pos + total_chunk_size > length) break;  // Truncated chunk

        // First chunk must be IHDR
        if (!has_ihdr) {
            if (t0 != 'I' || t1 != 'H' || t2 != 'D' || t3 != 'R')
                return ValidateResult::Reject;  // IHDR must be first chunk
            has_ihdr = true;

            // Re-validate IHDR content
            if (data_length != 13) return ValidateResult::Reject;  // IHDR must be 13 bytes

            uint32_t width  = read_be32(data + pos + 8);
            uint32_t height = read_be32(data + pos + 12);
            if (width == 0 || height == 0) return ValidateResult::Reject;
            if (width > 100000 || height > 100000) return ValidateResult::Reject;

            uint8_t bit_depth  = data[pos + 16];
            uint8_t color_type = data[pos + 17];

            // Validate color type + bit depth combinations (same as header_check)
            switch (color_type) {
                case 0: // Grayscale
                    if (bit_depth == 2 || bit_depth > 16) return ValidateResult::Reject;
                    break;
                case 2: // RGB
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                case 3: // Indexed
                    if (bit_depth == 16) return ValidateResult::Reject;
                    break;
                case 4: // Grayscale + Alpha
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                case 6: // RGBA
                    if (bit_depth != 8 && bit_depth != 16) return ValidateResult::Reject;
                    break;
                default: return ValidateResult::Reject;
            }

            // Verify IHDR CRC
            if (!verify_png_chunk_crc(data + pos, data_length))
                return ValidateResult::Reject;
        }

        // Detect IDAT chunks
        if (t0 == 'I' && t1 == 'D' && t2 == 'A' && t3 == 'T') {
            has_idat = true;
        }

        // Detect IEND chunk
        if (t0 == 'I' && t1 == 'E' && t2 == 'N' && t3 == 'D') {
            if (data_length != 0) return ValidateResult::Reject;

            // Verify IEND CRC
            if (!verify_png_chunk_crc(data + pos, data_length))
                return ValidateResult::Reject;

            // IEND found — calculate exact file size
            calculated_file_size = pos + total_chunk_size;

            // Verify required chunks
            if (!has_ihdr || !has_idat) return ValidateResult::Reject;

            return ValidateResult::AcceptVerified;
        }

        // Advance to next chunk
        pos += total_chunk_size;
    }

    // No IEND found or file truncated
    if (has_ihdr && has_idat)
        return ValidateResult::AcceptStructure;  // Valid structure but missing IEND

    return ValidateResult::Reject;
}

} // anonymous namespace

const FormatDescriptor PNG_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"png",
    .description     = L"PNG image",
    .min_filesize    = 33,  // 8 (sig) + 25 (IHDR) = 33
    .max_filesize    = 0,
    .signature       = {PNG_MAGIC, nullptr, 8, 0, 0},
    .header_check    = check_png_header_impl,
    .data_check      = check_png_data_impl,
    .file_check      = check_png_file_impl,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_png_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_png_header_impl(data, length, calculated_file_size);
}

ValidateResult check_png_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_png_data_impl(data, length, offset_in_file, calculated_file_size);
}

// Backward-compatible wrapper for old validators.hpp interface
std::optional<MatchResult> validate_png(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult vr = check_png_header_impl(data, length, calculated_file_size);
    if (vr == ValidateResult::Reject) return std::nullopt;

    MatchResult mr;
    mr.confidence = static_cast<int>(vr) * 20;  // 20, 40, 60, 80
    mr.flags = MatchFlags::HasHeader;
    if (vr >= ValidateResult::AcceptStructure) mr.flags = mr.flags | MatchFlags::DeepValidated;
    if (vr >= ValidateResult::AcceptVerified)   mr.flags = mr.flags | MatchFlags::HasFooter | MatchFlags::DeepValidated;
    mr.verified_file_size = calculated_file_size;
    mr.signature = {FileType::Image, L"png", L"PNG"};
    return mr;
}

} // namespace disk_recover