#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// RIFF validator — Tier 1 container format.
// Validates RIFF header + container type dispatch (AVI, WebP, WAV).
// Size is determinable from header (riff_size + 8).

ValidateResult check_riff_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover
