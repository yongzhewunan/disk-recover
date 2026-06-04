#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// WMV/ASF validator (extracted from file_signatures.cpp)
// Tier 1: Size-in-header format — calculated_file_size from ASF Header Object.

ValidateResult check_wmv_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover