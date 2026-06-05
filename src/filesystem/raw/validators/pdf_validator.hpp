#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// PDF document validator
// Tier 2: Footer-terminated format — %%EOF marker at end of file.

extern const FormatDescriptor PDF_DESCRIPTOR;

ValidateResult check_pdf_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover