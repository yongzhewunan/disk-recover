#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"
#include <cstring>

namespace disk_recover {

namespace {

// Helper to read 16-bit value with endianness
inline uint16_t rd16(const uint8_t* p, bool le) {
    return le ? read_le16(p) : read_be16(p);
}

// Helper to read 32-bit value with endianness
inline uint32_t rd32(const uint8_t* p, bool le) {
    return le ? read_le32(p) : read_be32(p);
}

// Validate MakerNote structure (not just string presence)
// Returns true if MakerNote has valid structure
bool validate_makernote_structure(const uint8_t* data, size_t length,
                                   uint32_t offset, bool le) {
    if (offset + 12 > length) return false;

    // Common MakerNote formats:
    // Nikon: Starts with "Nikon\x00" or "Nikon" + IFD
    // Canon: Direct IFD structure
    // Sony: "SONY DSC " prefix or direct IFD
    // Olympus: "OLYMP\x00\x01\x00" or "OLYMPUS"

    // Check for Nikon signature
    if (offset + 8 <= length) {
        if (std::memcmp(data + offset, "Nikon\x00", 6) == 0) {
            // Nikon MakerNote with signature
            // Check for IFD offset after signature
            return true;
        }
    }

    // Check for Sony signature
    if (offset + 10 <= length) {
        if (std::memcmp(data + offset, "SONY DSC ", 9) == 0 ||
            std::memcmp(data + offset, "SONY", 4) == 0) {
            return true;
        }
    }

    // Check for Olympus signature
    if (offset + 8 <= length) {
        if (std::memcmp(data + offset, "OLYMP\x00", 6) == 0 ||
            std::memcmp(data + offset, "OLYMPUS", 7) == 0) {
            return true;
        }
    }

    // Check for Panasonic signature
    if (offset + 9 <= length) {
        if (std::memcmp(data + offset, "Panasonic", 9) == 0) {
            return true;
        }
    }

    // Check for valid IFD structure (count < 100)
    if (offset + 2 <= length) {
        uint16_t count = rd16(data + offset, le);
        if (count > 0 && count < 100) {
            // Reasonable tag count
            return true;
        }
    }

    return false;
}

// Find a string in data (limited search)
bool find_string_limited(const uint8_t* data, size_t length,
                         const char* str, size_t str_len, size_t max_search) {
    if (str_len == 0 || length < str_len) return false;

    size_t search_len = length < max_search ? length : max_search;

    for (size_t i = 0; i + str_len <= search_len; ++i) {
        if (std::memcmp(data + i, str, str_len) == 0) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

std::optional<MatchResult> validate_tiff_raw(const uint8_t* data, size_t length) {
    // Phase 1: TIFF header validation
    if (length < 8) return std::nullopt;

    bool le = false;  // Little-endian
    bool be = false;  // Big-endian

    if (data[0] == 0x49 && data[1] == 0x49) {
        le = true;
    } else if (data[0] == 0x4D && data[1] == 0x4D) {
        be = true;
    } else {
        return std::nullopt;
    }

    // Validate magic number 42
    uint16_t magic = rd16(data + 2, le);
    if (magic != 42) return std::nullopt;

    // Validate IFD offset
    uint32_t ifd_offset = rd32(data + 4, le);
    if (ifd_offset < 8 || ifd_offset >= length) {
        return std::nullopt;
    }

    float evidence = TIFF_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: CR2 explicit check (strongest evidence)
    // CR2 has "CR" at bytes 8-9
    if (length >= 10 && data[8] == 'C' && data[9] == 'R') {
        return MatchResult{
            {FileType::Image, L"cr2", L"Canon CR2"},
            98,
            flags | MatchFlags::ContainerParsed
        };
    }

    // Phase 3: IFD tag scanning (primary evidence)
    if (ifd_offset + 2 <= length) {
        uint16_t count = rd16(data + ifd_offset, le);

        // Sanity check tag count
        if (count > 0 && count < 100 && ifd_offset + 2 + 12 * count <= length) {
            evidence += TIFF_WEIGHTS.structure_weight;
            flags = flags | MatchFlags::DeepValidated;

            // Scan for specific tags
            for (uint16_t i = 0; i < count; ++i) {
                const uint8_t* entry = data + ifd_offset + 2 + 12 * i;
                uint16_t tag = rd16(entry, le);

                // DNGVersion tag (50706) - definitive DNG evidence
                if (tag == 50706) {
                    return MatchResult{
                        {FileType::Image, L"dng", L"Adobe DNG"},
                        98,
                        flags | MatchFlags::ContainerParsed
                    };
                }

                // MakerNote tag (37500) - strong vendor evidence
                if (tag == 37500) {
                    // Get MakerNote offset
                    uint16_t type = rd16(entry + 2, le);
                    uint32_t count = rd32(entry + 4, le);
                    uint32_t offset;

                    if (type == 7 && count > 0) {  // UNDEFINED type
                        offset = rd32(entry + 8, le);
                    } else {
                        offset = rd32(entry + 8, le);
                    }

                    // Validate MakerNote structure
                    if (offset < length && validate_makernote_structure(data, length, offset, le)) {
                        evidence += 25.0f;  // Strong container evidence
                    }
                }

                // SubIFD tag (330) - can contain RAW data
                if (tag == 330) {
                    evidence += 8.0f;
                }

                // EXIF IFD tag (34665)
                if (tag == 34665) {
                    evidence += 5.0f;
                }

                // CFAPattern (41730) - indicates RAW
                if (tag == 41730) {
                    evidence += 15.0f;
                }
            }
        }
    }

    // Phase 4: String search ONLY as weak signal (< 15 points)
    // Only perform if no strong IFD evidence found
    if (evidence < TIFF_WEIGHTS.header_weight + 30.0f) {
        size_t search_len = length < 1024 ? length : 1024;

        // These are WEAK signals - < 12 points each
        if (find_string_limited(data, length, "Nikon", 5, search_len)) {
            evidence += 12.0f;
        } else if (find_string_limited(data, length, "SONY", 4, search_len)) {
            evidence += 12.0f;
        } else if (find_string_limited(data, length, "OLYMP", 5, search_len)) {
            evidence += 12.0f;
        } else if (find_string_limited(data, length, "Panasonic", 9, search_len)) {
            evidence += 12.0f;
        }
    }

    // Determine vendor from evidence
    std::wstring ext = L"tiff";
    std::wstring desc = le ? L"TIFF-LE" : L"TIFF-BE";

    if (evidence >= TIFF_WEIGHTS.header_weight + 30.0f) {
        // Vendor detected - determine which one
        size_t search_len = length < 1024 ? length : 1024;

        if (find_string_limited(data, length, "Nikon", 5, search_len)) {
            ext = L"nef";
            desc = L"Nikon NEF";
        } else if (find_string_limited(data, length, "SONY", 4, search_len)) {
            ext = L"arw";
            desc = L"Sony ARW";
        } else if (find_string_limited(data, length, "OLYMP", 5, search_len)) {
            ext = L"orf";
            desc = L"Olympus ORF";
        } else if (find_string_limited(data, length, "Panasonic", 9, search_len)) {
            ext = L"rw2";
            desc = L"Panasonic RW2";
        }
    }

    return MatchResult{
        {FileType::Image, ext, desc},
        normalize_confidence(evidence, TIFF_WEIGHTS),
        flags
    };
}

} // namespace disk_recover