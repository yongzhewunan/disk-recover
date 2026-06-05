#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// GIF validator — Tier 2 footer-terminated format.
// Validates GIF87a/GIF89a header, logical screen descriptor, and global color table.
// Uses data_check for progressive block parsing and trailer search.

extern const FormatDescriptor GIF_DESCRIPTOR;

ValidateResult check_gif_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_gif_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover