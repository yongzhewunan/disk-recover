#include "bmp_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// BMP magic bytes: 'BM'
static const uint8_t BMP_MAGIC[] = {0x42, 0x4D};

// ── BMP header check ──
// Tier 1: Size-in-header format. Validates DIB header, dimensions, and compression.
// Returns AcceptVerified when all fields are valid (size is known from header).
ValidateResult check_bmp_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 54) return ValidateResult::Reject;
    if (data[0] != 0x42 || data[1] != 0x4D) return ValidateResult::Reject;

    uint32_t file_size    = read_le32(data + 2);
    uint32_t pixel_offset = read_le32(data + 10);
    uint32_t dib_size     = read_le32(data + 14);

    // Hard rejects: fundamental fields must be valid
    if (file_size < 54)    return ValidateResult::Reject;
    if (pixel_offset < 54) return ValidateResult::Reject;

    // DIB header size must be a known value
    switch (dib_size) {
        case 12:  // BITMAPCOREHEADER (OS/2)
        case 40:  // BITMAPINFOHEADER (standard)
        case 52: case 56: case 64:    // extended
        case 108: // BITMAPV4HEADER
        case 124: // BITMAPV5HEADER
            break;
        default:
            return ValidateResult::Reject;  // Unknown DIB header → reject
    }

    // Dimensions and format fields
    int32_t width  = static_cast<int32_t>(read_le32(data + 18));
    int32_t height = static_cast<int32_t>(read_le32(data + 22));
    uint16_t planes = read_le16(data + 26);
    uint16_t bpp    = read_le16(data + 28);
    uint32_t compression = read_le32(data + 30);

    // Planes must be 1
    if (planes != 1) return ValidateResult::Reject;

    // Bits per pixel must be valid
    switch (bpp) {
        case 1: case 4: case 8:
        case 16: case 24: case 32:
            break;
        default:
            return ValidateResult::Reject;
    }

    // Compression must be known
    switch (compression) {
        case 0: // BI_RGB
        case 1: // BI_RLE8
        case 2: // BI_RLE4
        case 3: // BI_BITFIELDS
        case 4: // BI_JPEG
        case 5: // BI_PNG
            break;
        default:
            return ValidateResult::Reject;
    }

    // Dimension sanity
    if (width <= 0 || width > 100000) return ValidateResult::Reject;
    if (height == 0) return ValidateResult::Reject;
    int64_t abs_height = height < 0 ? -static_cast<int64_t>(height) : static_cast<int64_t>(height);
    if (abs_height > 100000) return ValidateResult::Reject;

    // All fields validated — file size is known from header
    calculated_file_size = file_size;
    return ValidateResult::AcceptVerified;
}

} // anonymous namespace

const FormatDescriptor BMP_DESCRIPTOR = {
    .file_type       = FileType::Image,
    .extension       = L"bmp",
    .description     = L"BMP bitmap image",
    .min_filesize    = 54,
    .max_filesize    = 0,  // No hard max (scanner uses per-type defaults)
    .signature       = {BMP_MAGIC, 2, 0},
    .header_check    = check_bmp_header_impl,
    .data_check      = nullptr,  // Size-in-header: generic size check
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface (for direct use by tests or legacy callers)
ValidateResult check_bmp_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_bmp_header_impl(data, length, calculated_file_size);
}

} // namespace disk_recover