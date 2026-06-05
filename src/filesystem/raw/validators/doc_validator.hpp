#pragma once
#include "format_descriptor.hpp"

namespace disk_recover {

// DOC/OLE2 compound document validator — Tier 3 full-validation format.
// Validates OLE2 signature, FAT chain, and directory entries.
// Uses file_check to identify specific stream names (WordDocument/Workbook/PowerPoint)
// and calculate file size from total sectors.

extern const FormatDescriptor DOC_DESCRIPTOR;

ValidateResult check_doc_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size);
ValidateResult check_doc_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size);

} // namespace disk_recover