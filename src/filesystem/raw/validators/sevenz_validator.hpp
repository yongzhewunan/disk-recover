#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// 7z archive validator
// Tier 1: Size-in-header format — calculated_file_size from 7z Start Header.

ValidateResult check_sevenz_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover
