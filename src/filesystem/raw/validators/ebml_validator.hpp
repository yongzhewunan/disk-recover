#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// EBML/MKV/WebM validator — Tier 2 container format.
// Validates EBML header signature, parses DocType to distinguish MKV vs WebM.
// Uses file_check to calculate file size from Segment element.

ValidateResult check_ebml_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_ebml_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover
