#include "pdf_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// PDF magic: '%PDF-' (0x25 0x50 0x44 0x46 0x2D)
static const uint8_t PDF_MAGIC[] = {0x25, 0x50, 0x44, 0x46, 0x2D};

// %%EOF marker: 0x25 0x25 0x45 0x4F 0x46
static const uint8_t PDF_EOF_MARKER[] = {0x25, 0x25, 0x45, 0x4F, 0x46};

// ── PDF header check ──
// Validates %PDF-1.x signature and version range.
// Returns AcceptHeader when signature and version are valid.
ValidateResult check_pdf_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 8) return ValidateResult::Reject;

    // Verify '%PDF-' signature
    for (int i = 0; i < 5; i++) {
        if (data[i] != PDF_MAGIC[i]) return ValidateResult::Reject;
    }

    // Parse version: must be '1.' followed by a digit
    if (data[5] != '1' || data[6] != '.') return ValidateResult::Reject;

    uint8_t minor = data[7];
    if (minor < '0' || minor > '7') return ValidateResult::Reject;

    // Check for binary comment marker (bytes > 128) after the version line.
    // PDF spec recommends a comment with high-bit bytes after the header line
    // to flag the file as binary. This is a soft check — not a rejection.
    // Scan up to 64 bytes from offset 8 for any byte > 128.
    bool has_binary_marker = false;
    size_t scan_end = length < 64 ? length : 64;
    for (size_t i = 8; i < scan_end; ++i) {
        if (data[i] > 128) {
            has_binary_marker = true;
            break;
        }
    }

    // PDF file size is unknown until %%EOF is found during data check
    calculated_file_size = 0;
    return ValidateResult::AcceptHeader;
}

// ── PDF data check ──
// Searches for %%EOF marker near end of block.
// If found, sets calculated_file_size and returns AcceptVerified.
ValidateResult check_pdf_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // Search for %%EOF marker in this block
    // The marker can appear anywhere; we search from the beginning
    for (size_t i = 0; i + 5 <= length; ++i) {
        bool match = true;
        for (int j = 0; j < 5; j++) {
            if (data[i + j] != PDF_EOF_MARKER[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            // Position after %%EOF
            uint64_t end_pos = offset_in_file + i + 5;

            // Include possible trailing newline (\r, \n, or \r\n)
            size_t remaining = length - (i + 5);
            if (remaining >= 2 && data[i + 5] == '\r' && data[i + 6] == '\n') {
                end_pos += 2;
            } else if (remaining >= 1 && (data[i + 5] == '\r' || data[i + 5] == '\n')) {
                end_pos += 1;
            }

            calculated_file_size = end_pos;
            return ValidateResult::AcceptVerified;
        }
    }

    // No %%EOF found — keep carving
    return ValidateResult::AcceptStructure;
}

} // anonymous namespace

const FormatDescriptor PDF_DESCRIPTOR = {
    .file_type       = FileType::Document,
    .extension       = L"pdf",
    .description     = L"PDF document",
    .min_filesize    = 8,
    .max_filesize    = 0,
    .signature       = {PDF_MAGIC, nullptr, 5, 0, 0},
    .header_check    = check_pdf_header_impl,
    .data_check      = check_pdf_data_impl,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface (for direct use by tests or legacy callers)
ValidateResult check_pdf_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_pdf_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover
