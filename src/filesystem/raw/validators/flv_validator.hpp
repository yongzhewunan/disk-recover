#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// FLV (Flash Video) validator (extracted from file_signatures.cpp)
// Tier 1: Size-in-header format — file size from last tag's offset.

extern const FormatDescriptor FLV_DESCRIPTOR;

ValidateResult check_flv_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover