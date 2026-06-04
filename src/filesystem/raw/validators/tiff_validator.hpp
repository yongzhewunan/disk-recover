#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// TIFF/RAW validator — Tier 2 container-based format.
// Validates TIFF header + IFD0 structure with vendor detection for RAW formats.
// Uses file_check for IFD walking to calculate file size from strip/tile offsets.

ValidateResult check_tiff_raw_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_tiff_raw_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover
