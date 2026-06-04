#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// JPEG validator — Tier 2 footer-terminated format.
// Uses marker state machine for header validation and EOI search for data_check.
// Inspired by PhotoRec's file_jpg.c three-phase model.

ValidateResult check_jpeg_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_jpeg_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover