#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// RAR archive validator — Tier 2 container format.
// Validates RAR signature (RAR3.x and RAR5.x), walks block headers.
// Uses data_check to validate multiple block headers with CRC.

ValidateResult check_rar_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_rar_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover
