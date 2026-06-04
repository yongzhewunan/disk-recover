#include "ebml_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"
#include "../file_signatures.hpp"  // For backward-compatible wrapper

namespace disk_recover {
namespace {

// EBML signature: 1A 45 DF A3
static const uint8_t EBML_MAGIC[] = {0x1A, 0x45, 0xDF, 0xA3};

// Read EBML variable-length integer.
// Returns the value and sets bytes_consumed.
// EBML VINT: first byte indicates length via leading zeros, value excludes marker bit.
uint64_t read_ebml_vint(const uint8_t* data, size_t length, size_t& bytes_consumed) {
    if (length == 0) {
        bytes_consumed = 0;
        return 0;
    }

    // Determine length from leading zeros
    int num_bytes = 0;
    uint8_t first_byte = data[0];

    if (first_byte & 0x80) num_bytes = 1;
    else if (first_byte & 0x40) num_bytes = 2;
    else if (first_byte & 0x20) num_bytes = 3;
    else if (first_byte & 0x10) num_bytes = 4;
    else if (first_byte & 0x08) num_bytes = 5;
    else if (first_byte & 0x04) num_bytes = 6;
    else if (first_byte & 0x02) num_bytes = 7;
    else if (first_byte & 0x01) num_bytes = 8;
    else {
        bytes_consumed = 0;
        return 0;  // Invalid
    }

    if (length < static_cast<size_t>(num_bytes)) {
        bytes_consumed = 0;
        return 0;
    }

    // Read the value (excluding length marker bits)
    uint64_t value = 0;
    for (int i = 0; i < num_bytes; ++i) {
        value = (value << 8) | data[i];
    }

    // Clear length marker bits
    static const uint64_t markers[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    value &= ~(markers[num_bytes - 1] << ((num_bytes - 1) * 8));

    bytes_consumed = num_bytes;
    return value;
}

// Read EBML element ID (variable-length, similar to VINT but marker bits are part of ID).
// Returns element ID and sets bytes_consumed.
uint64_t read_ebml_element_id(const uint8_t* data, size_t length, size_t& bytes_consumed) {
    if (length == 0) {
        bytes_consumed = 0;
        return 0;
    }

    // Determine length from leading zeros (same as VINT)
    int num_bytes = 0;
    uint8_t first_byte = data[0];

    if (first_byte & 0x80) num_bytes = 1;
    else if (first_byte & 0x40) num_bytes = 2;
    else if (first_byte & 0x20) num_bytes = 3;
    else if (first_byte & 0x10) num_bytes = 4;
    else {
        bytes_consumed = 0;
        return 0;
    }

    if (length < static_cast<size_t>(num_bytes)) {
        bytes_consumed = 0;
        return 0;
    }

    // Element ID includes the marker bits (unlike VINT value)
    uint64_t id = 0;
    for (int i = 0; i < num_bytes; ++i) {
        id = (id << 8) | data[i];
    }

    bytes_consumed = num_bytes;
    return id;
}

// ── Phase 1: Header check ──
// Validates EBML signature + EBML header structure.
// Searches for DocType element (0x4282) to distinguish MKV vs WebM.
// Returns AcceptContainer if DocType found, AcceptStructure otherwise.
ValidateResult check_ebml_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 16) return ValidateResult::Reject;

    // Verify EBML signature
    for (int i = 0; i < 4; ++i) {
        if (data[i] != EBML_MAGIC[i]) return ValidateResult::Reject;
    }

    calculated_file_size = 0;  // Size unknown until Segment found

    // Parse EBML header structure
    size_t pos = 4;

    // Read EBML header size
    size_t size_bytes = 0;
    uint64_t header_size = read_ebml_vint(data + pos, length - pos, size_bytes);
    if (size_bytes == 0 || pos + size_bytes > length) {
        return ValidateResult::AcceptHeader;  // Minimal match
    }

    pos += size_bytes;
    size_t header_end = pos + header_size;
    if (header_end > length) header_end = length;

    // Search for DocType element (0x4282) within EBML header
    while (pos + 4 <= header_end) {
        // Read element ID
        size_t id_bytes = 0;
        uint64_t element_id = read_ebml_element_id(data + pos, header_end - pos, id_bytes);
        if (id_bytes == 0) break;

        // Read element size
        size_t data_pos = pos + id_bytes;
        if (data_pos >= header_end) break;

        size_t sz_bytes = 0;
        uint64_t element_size = read_ebml_vint(data + data_pos, header_end - data_pos, sz_bytes);
        if (sz_bytes == 0) break;

        size_t element_data_pos = data_pos + sz_bytes;

        // DocType element (0x4282)
        if (element_id == 0x4282 && element_data_pos + element_size <= header_end) {
            // Read DocType string
            if (element_size >= 4 && element_size <= 16) {
                char doctype[17] = {0};
                for (uint64_t i = 0; i < element_size && i < 16; ++i) {
                    doctype[i] = static_cast<char>(data[element_data_pos + i]);
                }

                // DocType found — container validated
                // Note: We return AcceptContainer here; the actual extension
                // (mkv vs webm) will be determined by file_check
                return ValidateResult::AcceptContainer;
            }
            break;
        }

        pos = element_data_pos + element_size;
    }

    // EBML header parsed but DocType not found
    return ValidateResult::AcceptStructure;
}

// ── Phase 3: File check ──
// Finds Segment element and reads its size to calculate file size.
// Also determines final extension based on DocType.
ValidateResult check_ebml_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 16) return ValidateResult::Reject;

    // Verify EBML signature
    for (int i = 0; i < 4; ++i) {
        if (data[i] != EBML_MAGIC[i]) return ValidateResult::Reject;
    }

    calculated_file_size = 0;

    // Parse EBML header to find its end
    size_t pos = 4;
    size_t size_bytes = 0;
    uint64_t header_size = read_ebml_vint(data + pos, length - pos, size_bytes);
    if (size_bytes == 0) return ValidateResult::AcceptStructure;

    pos += size_bytes + header_size;  // Skip entire EBML header

    // Now look for Segment element (0x18538067)
    // Segment element contains the entire Matroska content
    while (pos + 4 <= length) {
        size_t id_bytes = 0;
        uint64_t element_id = read_ebml_element_id(data + pos, length - pos, id_bytes);
        if (id_bytes == 0) break;

        size_t data_pos = pos + id_bytes;
        if (data_pos >= length) break;

        size_t sz_bytes = 0;
        uint64_t element_size = read_ebml_vint(data + data_pos, length - data_pos, sz_bytes);
        if (sz_bytes == 0) break;

        // Segment element (0x18538067)
        if (element_id == 0x18538067) {
            // Segment size is the content size
            // Unknown size is encoded as max value (all 0xFF in VINT)
            // For unknown size, we can't determine file size
            size_t element_data_pos = data_pos + sz_bytes;

            // Check if size is "unknown" (all 1s in the value portion)
            // For 1-byte VINT: 0x1F (max), for 2-byte: 0x3FFF, etc.
            // These indicate streaming/unknown size
            bool unknown_size = false;
            if (sz_bytes == 1 && (data[data_pos] & 0x7F) == 0x7F) unknown_size = true;
            else if (sz_bytes == 2 && (data[data_pos] & 0x3F) == 0x3F && data[data_pos + 1] == 0xFF) unknown_size = true;
            else if (sz_bytes >= 3) {
                // Check for all-FF pattern in value portion
                bool all_ff = true;
                for (size_t i = 0; i < sz_bytes; ++i) {
                    uint8_t mask = (i == 0) ? (0xFF >> (sz_bytes - 1)) : 0xFF;
                    if ((data[data_pos + i] & mask) != mask) {
                        all_ff = false;
                        break;
                    }
                }
                if (all_ff) unknown_size = true;
            }

            if (!unknown_size && element_size > 0) {
                // File size = position of segment data + segment size
                calculated_file_size = element_data_pos + element_size;
            }

            return ValidateResult::AcceptVerified;
        }

        // Skip other top-level elements
        // For elements we don't recognize, try to skip based on size
        if (element_size == 0) break;  // Can't determine size
        pos = data_pos + sz_bytes + element_size;
    }

    return ValidateResult::AcceptContainer;
}

// Auto-registration with FormatRegistry
// Note: EBML can be either MKV or WebM; we register as MKV (more common)
// and the file_check can refine the extension if needed.
static const FormatDescriptor EBML_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"mkv",
    .description     = L"MKV/WebM video",
    .min_filesize    = 16,
    .max_filesize    = 0,
    .signature       = {EBML_MAGIC, 4, 0},
    .header_check    = check_ebml_header_impl,
    .data_check      = nullptr,
    .file_check      = check_ebml_file_impl,
    .enabled_by_default = true,
};

static bool _ebml_registered = []() {
    FormatRegistry::instance().register_format(EBML_DESCRIPTOR);
    return true;
}();

} // anonymous namespace

// Public interface
ValidateResult check_ebml_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_ebml_header_impl(data, length, calculated_file_size);
}

ValidateResult check_ebml_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_ebml_file_impl(data, length, calculated_file_size);
}

// ── Backward-compatible wrapper for old FileSignatures interface ──
// Converts ValidateResult + FormatDescriptor to old MatchResult.
namespace {

// Helper to determine DocType from data for extension selection
const wchar_t* ebml_doctype_extension(const uint8_t* data, size_t length) {
    if (length < 16) return L"mkv";

    size_t pos = 4;
    size_t size_bytes = 0;
    uint64_t header_size = read_ebml_vint(data + pos, length - pos, size_bytes);
    if (size_bytes == 0) return L"mkv";

    pos += size_bytes;
    size_t header_end = pos + header_size;
    if (header_end > length) header_end = length;

    while (pos + 4 <= header_end) {
        size_t id_bytes = 0;
        uint64_t element_id = read_ebml_element_id(data + pos, header_end - pos, id_bytes);
        if (id_bytes == 0) break;

        size_t data_pos = pos + id_bytes;
        if (data_pos >= header_end) break;

        size_t sz_bytes = 0;
        uint64_t element_size = read_ebml_vint(data + data_pos, header_end - data_pos, sz_bytes);
        if (sz_bytes == 0) break;

        size_t element_data_pos = data_pos + sz_bytes;

        if (element_id == 0x4282 && element_data_pos + element_size <= header_end) {
            if (element_size >= 4 && element_size <= 16) {
                char doctype[17] = {0};
                for (uint64_t i = 0; i < element_size && i < 16; ++i) {
                    doctype[i] = static_cast<char>(data[element_data_pos + i]);
                }
                if (std::strcmp(doctype, "webm") == 0) return L"webm";
            }
            break;
        }

        pos = element_data_pos + element_size;
    }

    return L"mkv";
}

} // anonymous namespace

std::optional<MatchResult> validate_ebml(const uint8_t* data, size_t length) {
    uint64_t calculated_file_size = 0;
    ValidateResult result = check_ebml_header_impl(data, length, calculated_file_size);
    if (result == ValidateResult::Reject) return std::nullopt;

    const wchar_t* ext = ebml_doctype_extension(data, length);
    const wchar_t* desc = (ext[0] == L'w') ? L"WebM" : L"MKV";

    MatchFlags flags = MatchFlags::HasHeader;
    if (result >= ValidateResult::AcceptStructure) flags = flags | MatchFlags::DeepValidated;
    if (result >= ValidateResult::AcceptContainer) flags = flags | MatchFlags::ContainerParsed;

    return MatchResult{
        {FileType::Video, ext, desc},
        validate_result_to_confidence(result),
        flags,
        0  // verified_file_size: only available via file_check
    };
}

} // namespace disk_recover
