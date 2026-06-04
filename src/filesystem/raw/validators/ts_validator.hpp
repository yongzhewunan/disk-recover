#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// MPEG-TS validator — Tier 2 streaming format.
// Validates sync byte periodicity for MTS (188-byte) and M2TS (192-byte) packets.
// Uses data_check for progressive continuity counter validation.

ValidateResult check_ts_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_ts_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size);

} // namespace disk_recover
