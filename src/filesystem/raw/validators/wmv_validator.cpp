#include "wmv_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// ASF Header Object GUID: 30 26 B2 75 8E 66 CF 11 A6 D9 00 AA 00 62 CE 6C
static const uint8_t ASF_MAGIC[] = {
    0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
    0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
};

// ── WMV/ASF header check ──
// Tier 1: Size-in-header format. Validates ASF Header Object GUID + size field.
// ASF Header Object contains the total file size at offset 40.
ValidateResult check_wmv_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 64) return ValidateResult::Reject;

    // Verify ASF Header Object GUID (16 bytes at offset 0)
    for (int i = 0; i < 16; i++) {
        if (data[i] != ASF_MAGIC[i]) return ValidateResult::Reject;
    }

    // Object Size (8 bytes at offset 16) — size of the Header Object itself
    uint64_t header_obj_size = read_le64(data + 16);
    if (header_obj_size < 30) return ValidateResult::Reject;

    // Number of Header Objects (4 bytes at offset 24)
    uint32_t num_objects = read_le32(data + 24);
    if (num_objects == 0 || num_objects > 1000) return ValidateResult::Reject;

    // Reserved1 (1 byte at offset 28) — must be 0x01 or 0x02
    uint8_t reserved1 = data[28];
    if (reserved1 != 0x01 && reserved1 != 0x02) return ValidateResult::Reject;

    // Reserved2 (1 byte at offset 29) — must be 0x02
    if (data[29] != 0x02) return ValidateResult::Reject;

    // Total file data size from the File Properties Object
    // This is located inside the header objects, but we can at least
    // confirm the header is valid and set calculated_file_size from header_obj_size
    // if we have the File Properties Object. For now, return AcceptStructure
    // since we've validated the header structure.
    calculated_file_size = 0;  // Exact size requires parsing File Properties Object
    return ValidateResult::AcceptStructure;
}

} // anonymous namespace

const FormatDescriptor WMV_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"wmv",
    .description     = L"Windows Media Video / ASF container",
    .min_filesize    = 64,
    .max_filesize    = 0,
    .signature       = {ASF_MAGIC, nullptr, 16, 0, 0},
    .header_check    = check_wmv_header_impl,
    .data_check      = nullptr,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_wmv_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_wmv_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover