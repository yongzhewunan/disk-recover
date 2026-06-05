#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// BMFF (ISO Base Media File Format) validator — Tier 2 container format.
// Validates ftyp box + brand lookup for MP4/MOV/HEIC/AVIF/M4A etc.
// Uses file_check for atom tree walking to calculate total file size.

extern const FormatDescriptor BMFF_IMAGE_DESCRIPTOR;
extern const FormatDescriptor BMFF_VIDEO_DESCRIPTOR;
extern const FormatDescriptor BMFF_AUDIO_DESCRIPTOR;

ValidateResult check_bmff_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_bmff_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover
