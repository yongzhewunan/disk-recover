#include "zip_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// ZIP local file header magic: 'PK\x03\x04'
static const uint8_t ZIP_MAGIC[] = {0x50, 0x4B, 0x03, 0x04};

// ZIP End-of-Central-Directory record magic: 'PK\x05\x06'
static const uint8_t ZIP_EOCD_MAGIC[] = {0x50, 0x4B, 0x05, 0x06};

// OOXML content types file marker
static const char OOXML_CONTENT_TYPES[] = "[Content_Types].xml";

// ── ZIP header check ──
// Validates local file header (PK\x03\x04), version needed, and compression method.
// Returns AcceptHeader when all fields are valid.
ValidateResult check_zip_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 30) return ValidateResult::Reject;

    // Verify 'PK\x03\x04' signature
    for (int i = 0; i < 4; i++) {
        if (data[i] != ZIP_MAGIC[i]) return ValidateResult::Reject;
    }

    // Version needed to extract (2 bytes at offset 4)
    // Valid range: 10 (1.0) to 63 (6.3)
    uint16_t version_needed = read_le16(data + 4);
    if (version_needed < 10 || version_needed > 63) return ValidateResult::Reject;

    // General purpose bit flag (2 bytes at offset 6) — no strict validation

    // Compression method (2 bytes at offset 8)
    uint16_t compression = read_le16(data + 8);
    switch (compression) {
        case 0:   // Stored (no compression)
        case 8:   // Deflate
        case 12:  // BZIP2
        case 14:  // LZMA
        case 98:  // PPMd
            break;
        default:
            return ValidateResult::Reject;
    }

    // ZIP file size is unknown until EOCD record is found during file check
    calculated_file_size = 0;
    return ValidateResult::AcceptHeader;
}

// ── ZIP file check ──
// Searches for End-of-Central-Directory (EOCD) record to determine total archive size.
// Also detects OOXML subtypes (docx/xlsx/pptx) via [Content_Types].xml.
// Returns AcceptVerified when EOCD is found and size is determined.
ValidateResult check_zip_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Search for EOCD record from the end of the file.
    // EOCD record is at least 22 bytes. Search backwards in the last 65536+22 bytes.
    // The comment field can be up to 65535 bytes, so EOCD can be at most
    // 22 + 65535 = 65557 bytes from the end.

    size_t search_start = 0;
    if (length > 65557) {
        search_start = length - 65557;
    }

    // Find the last EOCD signature (search from end)
    int64_t eocd_pos = -1;
    for (size_t i = length; i >= search_start + 22; --i) {
        size_t pos = i - 4;
        if (data[pos]     == ZIP_EOCD_MAGIC[0] &&
            data[pos + 1] == ZIP_EOCD_MAGIC[1] &&
            data[pos + 2] == ZIP_EOCD_MAGIC[2] &&
            data[pos + 3] == ZIP_EOCD_MAGIC[3]) {
            eocd_pos = static_cast<int64_t>(pos);
            break;
        }
    }

    if (eocd_pos < 0) return ValidateResult::AcceptHeader;

    // Parse EOCD record
    size_t eocd = static_cast<size_t>(eocd_pos);

    // Need at least 22 bytes of EOCD record
    if (eocd + 22 > length) return ValidateResult::AcceptHeader;

    // Comment length (2 bytes at EOCD+20)
    uint16_t comment_len = read_le16(data + eocd + 20);

    // Verify that the record extends to the end of the file
    if (eocd + 22 + comment_len > length) return ValidateResult::AcceptHeader;

    // Offset of start of central directory (4 bytes at EOCD+16)
    uint32_t cd_offset = read_le32(data + eocd + 16);
    uint32_t cd_size   = read_le32(data + eocd + 12);

    // Calculate total archive size
    // Archive size = cd_offset + cd_size + (EOCD record size including comment)
    uint64_t archive_size = static_cast<uint64_t>(cd_offset) + cd_size + (22 + comment_len);

    // Check for OOXML subtypes — scan for [Content_Types].xml in the file data
    // This indicates docx/xlsx/pptx rather than a plain zip
    // We only scan the first 64KB for efficiency
    size_t ooxml_scan_len = length < 65536 ? length : 65536;
    bool is_ooxml = false;
    for (size_t i = 0; i + 40 <= ooxml_scan_len; ++i) {
        // Quick check for '[' first
        if (data[i] != '[') continue;

        // Full match check
        bool match = true;
        for (int j = 0; OOXML_CONTENT_TYPES[j] != '\0'; ++j) {
            if (i + j >= ooxml_scan_len || data[i + j] != static_cast<uint8_t>(OOXML_CONTENT_TYPES[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            is_ooxml = true;
            break;
        }
    }

    calculated_file_size = archive_size;
    return ValidateResult::AcceptVerified;
}

} // anonymous namespace

const FormatDescriptor ZIP_DESCRIPTOR = {
    .file_type       = FileType::Archive,
    .extension       = L"zip",
    .description     = L"ZIP archive",
    .min_filesize    = 22,
    .max_filesize    = 0,
    .signature       = {ZIP_MAGIC, nullptr, 4, 0, 0},
    .header_check    = check_zip_header_impl,
    .data_check      = nullptr,
    .file_check      = check_zip_file_impl,
    .enabled_by_default = true,
};

// Public interface (for direct use by tests or legacy callers)
ValidateResult check_zip_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_zip_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover
