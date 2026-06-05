#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// MP3 audio validator — Tier 2 frame-based format.
// Validates MPEG audio frame headers with strict sync + field validation.
// Uses data_check to verify 3+ consecutive consistent frames.
// Parses XING/VBRI header for frame count when present.

extern const FormatDescriptor MP3_DESCRIPTOR;

ValidateResult check_mp3_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_mp3_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover