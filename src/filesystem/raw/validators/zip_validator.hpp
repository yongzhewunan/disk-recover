#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// ZIP archive validator
// Tier 2: Archive format — End-of-Central-Directory record determines size.

extern const FormatDescriptor ZIP_DESCRIPTOR;

ValidateResult check_zip_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover