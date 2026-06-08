#pragma once
#include <cstdint>
#include <cstddef>
#include "validation.hpp"
#include "types.hpp"

namespace disk_recover {

// Descriptor for a file format, including its signature, validation functions,
// and size constraints. Each validator module defines a static instance that
// auto-registers with FormatRegistry.
//
// Inspired by PhotoRec's file_hint_t, but adapted for C++ with:
// - Type-safe function pointers instead of void*
// - Signature pattern embedded in the descriptor
// - Three-phase validation: header_check → data_check → file_check
struct FormatDescriptor {
    FileType        file_type;
    const wchar_t*  extension;       // Default file extension (e.g., L"jpg")
    const wchar_t*  description;     // Human-readable description

    uint64_t        min_filesize;    // Minimum valid file size (0 = no minimum)
    uint64_t        max_filesize;    // Maximum valid file size (0 = no limit)

    // Signature pattern for first-byte index lookup.
    struct SignaturePattern {
        const uint8_t* pattern;          // Magic byte pattern to match
        const uint8_t* mask;             // Bitwise AND mask for pattern matching (nullptr = exact match)
        uint8_t        pattern_len;      // Length of pattern in bytes
        uint8_t        offset;           // Byte offset where pattern appears (0 = at start)
        uint8_t        alt_first_byte;   // Alternative first byte for index lookup (0 = none)
                                      // Used for formats with multiple entry signatures (e.g., MP3 with ID3 tag)
    } signature;

    // Phase 1: Header check — called once per candidate sector.
    // Input: data buffer and length (at least ~512 bytes).
    // Output: ValidateResult (Reject if not this format).
    // Sets calculated_file_size if determinable from header (0 if unknown).
    using HeaderCheckFn = ValidateResult(*)(const uint8_t* data, size_t length,
                                             uint64_t& calculated_file_size);

    // Phase 2: Data check — called per block during progressive carving.
    // Input: current data block, offset within file, running calculated_file_size.
    // Output: AcceptStructure to continue, AcceptVerified if end found, Reject if corruption.
    // nullptr = use generic size-based check (stop when offset >= calculated_file_size).
    using DataCheckFn = ValidateResult(*)(const uint8_t* data, size_t length,
                                           uint64_t offset_in_file,
                                           uint64_t& calculated_file_size);

    // Phase 3: File check — called once after all data collected.
    // Input: complete file data, length, mutable calculated_file_size.
    // Output: final ValidateResult. May refine calculated_file_size.
    // nullptr = no final check needed.
    using FileCheckFn = ValidateResult(*)(const uint8_t* data, size_t length,
                                           uint64_t& calculated_file_size);

    HeaderCheckFn   header_check     = nullptr;
    DataCheckFn     data_check       = nullptr;
    FileCheckFn     file_check       = nullptr;
    bool            enabled_by_default = true;
};

} // namespace disk_recover