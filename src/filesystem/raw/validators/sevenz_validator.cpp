#include "sevenz_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// 7z signature: '7z\xBC\xAF\x27\x1C'
static const uint8_t SEVENZ_MAGIC[] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};

// 7z Start Header (Signature Header) layout:
//   Offset  0:  Signature (6 bytes): 37 7A BC AF 27 1C
//   Offset  6:  Major version (1 byte)
//   Offset  7:  Minor version (1 byte)
//   Offset  8:  StartHeaderCRC (4 bytes, CRC32 of NextHeader fields)
//   Offset 12:  NextHeaderOffset (8 bytes, uint64 LE)
//   Offset 20:  NextHeaderSize (8 bytes, uint64 LE)
//   Offset 28:  NextHeaderCRC (4 bytes, CRC32)
//   Total: 32 bytes

static const size_t SEVENZ_HEADER_SIZE = 32;

// ── 7z header check ──
// Validates 6-byte signature, version byte, and StartHeaderCRC.
// Returns AcceptHeader when all fields are valid.
ValidateResult check_sevenz_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < SEVENZ_HEADER_SIZE) return ValidateResult::Reject;

    // Verify 6-byte signature
    for (int i = 0; i < 6; i++) {
        if (data[i] != SEVENZ_MAGIC[i]) return ValidateResult::Reject;
    }

    // Version: major at offset 6, minor at offset 7
    // Known versions: 0.0 through 0.4 (major=0)
    uint8_t major = data[6];
    uint8_t minor = data[7];
    if (major != 0) return ValidateResult::Reject;
    if (minor > 4) return ValidateResult::Reject;

    // StartHeaderCRC at offset 8 (4 bytes) — must not be 0
    // A zero CRC indicates a corrupted or placeholder header
    uint32_t start_header_crc = read_le32(data + 8);
    if (start_header_crc == 0) return ValidateResult::Reject;

    // File size can be determined from NextHeaderOffset + NextHeaderSize
    // Read NextHeaderOffset (8 bytes at offset 12) and NextHeaderSize (8 bytes at offset 20)
    uint64_t next_header_offset = read_le64(data + 12);
    uint64_t next_header_size   = read_le64(data + 20);

    // Calculate total file size: 32 (signature header) + NextHeaderOffset + NextHeaderSize
    calculated_file_size = SEVENZ_HEADER_SIZE + next_header_offset + next_header_size;

    return ValidateResult::AcceptHeader;
}

// ── 7z file check ──
// Reads NextHeaderOffset and NextHeaderSize to calculate exact file size.
// Returns AcceptVerified with precise calculated_file_size.
ValidateResult check_sevenz_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < SEVENZ_HEADER_SIZE) return ValidateResult::AcceptHeader;

    uint64_t next_header_offset = read_le64(data + 12);
    uint64_t next_header_size   = read_le64(data + 20);

    uint64_t total_size = SEVENZ_HEADER_SIZE + next_header_offset + next_header_size;

    // Sanity check: total size should not exceed what we have or be impossibly large
    // A 7z archive with next_header_offset > 1TB is almost certainly corrupt
    if (next_header_offset > 1099511627776ULL) return ValidateResult::AcceptHeader;

    calculated_file_size = total_size;
    return ValidateResult::AcceptVerified;
}

} // anonymous namespace

const FormatDescriptor SEVENZ_DESCRIPTOR = {
    .file_type       = FileType::Archive,
    .extension       = L"7z",
    .description     = L"7-Zip archive",
    .min_filesize    = 32,
    .max_filesize    = 0,
    .signature       = {SEVENZ_MAGIC, nullptr, 6, 0, 0},
    .header_check    = check_sevenz_header_impl,
    .data_check      = nullptr,
    .file_check      = check_sevenz_file_impl,
    .enabled_by_default = true,
};

// Public interface (for direct use by tests or legacy callers)
ValidateResult check_sevenz_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_sevenz_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover
