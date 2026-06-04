#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// PNG validator — Tier 2 footer-terminated format.
// Validates PNG signature, IHDR chunk, and scans for IEND.
// Uses data_check for progressive chunk walking with CRC verification.

ValidateResult check_png_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_png_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover