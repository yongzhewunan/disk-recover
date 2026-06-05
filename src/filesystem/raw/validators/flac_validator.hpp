#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// FLAC audio validator — Tier 3 full-validation format.
// Validates fLaC signature and STREAMINFO metadata block.
// Uses file_check to walk all metadata blocks and calculate file size
// from total samples + sample rate + metadata overhead.

extern const FormatDescriptor FLAC_DESCRIPTOR;

ValidateResult check_flac_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_flac_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover