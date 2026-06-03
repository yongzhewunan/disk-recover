#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"
#include "../bmff_brands.hpp"

namespace disk_recover {

namespace {

// Check if 4 bytes match a string (helper for non-literal strings)
inline bool has_4bytes(const uint8_t* data, size_t length, size_t offset, const char* str) {
    if (offset + 4 > length) return false;
    return data[offset] == static_cast<uint8_t>(str[0]) &&
           data[offset + 1] == static_cast<uint8_t>(str[1]) &&
           data[offset + 2] == static_cast<uint8_t>(str[2]) &&
           data[offset + 3] == static_cast<uint8_t>(str[3]);
}

// Find a box by type in the data
// Returns offset of the box, or npos if not found
size_t find_box(const uint8_t* data, size_t length, const char* type, size_t start = 0) {
    if (start >= length) return SIZE_MAX;

    size_t pos = start;
    while (pos + 8 <= length) {
        uint32_t box_size = read_be32(data + pos);

        // Check for extended size
        if (box_size == 1) {
            // 64-bit size
            if (pos + 16 > length) break;
            uint64_t ext_size = read_be64(data + pos + 8);
            if (ext_size > length) break;
            box_size = static_cast<uint32_t>(ext_size);
        } else if (box_size == 0) {
            // Box extends to end of file
            box_size = static_cast<uint32_t>(length - pos);
        }

        if (box_size < 8) break;  // Invalid box

        // Check box type
        if (has_4bytes(data, length, pos + 4, type)) {
            return pos;
        }

        pos += box_size;
    }

    return SIZE_MAX;
}

} // anonymous namespace

std::optional<MatchResult> validate_bmff(const uint8_t* data, size_t length) {
    // Phase 1: Find ftyp box (may not be at offset 0)
    if (length < 12) return std::nullopt;

    // Search for ftyp box in first 32 bytes
    size_t ftyp_pos = SIZE_MAX;
    for (size_t i = 0; i <= 24 && i + 12 <= length; i += 4) {
        if (has_str(data, length, i + 4, "ftyp")) {
            ftyp_pos = i;
            break;
        }
    }

    if (ftyp_pos == SIZE_MAX) return std::nullopt;

    // Validate ftyp box
    uint32_t box_size = read_be32(data + ftyp_pos);
    if (box_size < 12 || box_size > length - ftyp_pos) {
        return std::nullopt;
    }

    float evidence = BMFF_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Get major brand
    const uint8_t* brand_ptr = data + ftyp_pos + 8;
    auto brand_entry = lookup_brand(brand_ptr);

    if (brand_entry) {
        // Known brand - use predefined confidence
        evidence += 10.0f;  // Structure evidence

        // Container evidence: check for moov box
        size_t moov_pos = find_box(data, length, "moov", ftyp_pos + box_size);
        if (moov_pos != SIZE_MAX) {
            evidence += BMFF_WEIGHTS.container_weight;
            flags = flags | MatchFlags::ContainerParsed;
        }

        return MatchResult{
            {brand_entry->file_type, std::wstring(brand_entry->extension),
             std::wstring(brand_entry->description)},
            normalize_confidence(evidence, BMFF_WEIGHTS),
            flags | MatchFlags::DeepValidated
        };
    }

    // Unknown brand - check for container structure
    evidence += 5.0f;  // Partial header match
    flags = flags | MatchFlags::PartialMatch;

    // Check for moov box (indicates valid MP4 container)
    size_t moov_pos = find_box(data, length, "moov", ftyp_pos + box_size);
    if (moov_pos != SIZE_MAX) {
        evidence += BMFF_WEIGHTS.container_weight;
        flags = flags | MatchFlags::ContainerParsed;
        flags = flags & ~MatchFlags::PartialMatch;
        flags = flags | MatchFlags::DeepValidated;
    }

    // Check for mdat box (media data)
    size_t mdat_pos = find_box(data, length, "mdat", ftyp_pos + box_size);
    if (mdat_pos != SIZE_MAX) {
        evidence += 10.0f;
    }

    // Default to MP4 for unknown brands
    return MatchResult{
        {FileType::Video, L"mp4", L"MP4 (unknown brand)"},
        normalize_confidence(evidence, BMFF_WEIGHTS),
        flags
    };
}

} // namespace disk_recover