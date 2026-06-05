#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// BMP bitmap image validator (extracted from file_signatures.cpp)
// Tier 1: Size-in-header format — calculated_file_size from BMP header field.

extern const FormatDescriptor BMP_DESCRIPTOR;

ValidateResult check_bmp_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover