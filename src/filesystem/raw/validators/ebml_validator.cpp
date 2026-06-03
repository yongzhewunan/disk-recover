#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

namespace {

// Read EBML variable-length integer
// Returns the value and sets bytes_consumed
uint64_t read_ebml_vint(const uint8_t* data, size_t length, size_t& bytes_consumed) {
    if (length == 0) {
        bytes_consumed = 0;
        return 0;
    }

    // Determine length from leading zeros
    int num_bytes = 0;
    uint8_t first_byte = data[0];

    if (first_byte & 0x80) num_bytes = 1;
    else if (first_byte & 0x40) num_bytes = 2;
    else if (first_byte & 0x20) num_bytes = 3;
    else if (first_byte & 0x10) num_bytes = 4;
    else if (first_byte & 0x08) num_bytes = 5;
    else if (first_byte & 0x04) num_bytes = 6;
    else if (first_byte & 0x02) num_bytes = 7;
    else if (first_byte & 0x01) num_bytes = 8;
    else {
        bytes_consumed = 0;
        return 0;  // Invalid
    }

    if (length < static_cast<size_t>(num_bytes)) {
        bytes_consumed = 0;
        return 0;
    }

    // Read the value (excluding length marker bits)
    uint64_t value = 0;
    for (int i = 0; i < num_bytes; ++i) {
        value = (value << 8) | data[i];
    }

    // Clear length marker bits
    static const uint64_t markers[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    value &= ~(markers[num_bytes - 1] << ((num_bytes - 1) * 8));

    bytes_consumed = num_bytes;
    return value;
}

} // anonymous namespace

std::optional<MatchResult> validate_ebml(const uint8_t* data, size_t length) {
    // Phase 1: EBML header validation
    // EBML signature: 1A 45 DF A3
    if (length < 16) return std::nullopt;

    static const uint8_t EBML_SIGNATURE[] = {0x1A, 0x45, 0xDF, 0xA3};
    for (int i = 0; i < 4; ++i) {
        if (data[i] != EBML_SIGNATURE[i]) return std::nullopt;
    }

    float evidence = EBML_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: Parse EBML header structure
    size_t pos = 4;

    // Read EBML header size
    size_t size_bytes = 0;
    uint64_t header_size = read_ebml_vint(data + pos, length - pos, size_bytes);
    if (size_bytes == 0 || pos + size_bytes > length) {
        return MatchResult{
            {FileType::Video, L"mkv", L"MKV/WebM"},
            50,  // Low confidence
            flags | MatchFlags::PartialMatch
        };
    }

    pos += size_bytes;
    size_t header_end = pos + header_size;
    if (header_end > length) header_end = length;

    evidence += EBML_WEIGHTS.structure_weight;
    flags = flags | MatchFlags::DeepValidated;

    // Phase 3: Search for DocType element
    // DocType element ID: 42 82
    while (pos + 4 <= header_end) {
        // Read element ID
        size_t id_bytes = 0;
        uint64_t element_id = read_ebml_vint(data + pos, header_end - pos, id_bytes);
        if (id_bytes == 0) break;

        // Read element size
        size_t data_pos = pos + id_bytes;
        if (data_pos >= header_end) break;

        size_t sz_bytes = 0;
        uint64_t element_size = read_ebml_vint(data + data_pos, header_end - data_pos, sz_bytes);
        if (sz_bytes == 0) break;

        size_t element_data_pos = data_pos + sz_bytes;

        // DocType element (0x4282)
        if (element_id == 0x4282 && element_data_pos + element_size <= header_end) {
            // Read DocType string
            if (element_size >= 4 && element_size <= 16) {
                char doctype[17] = {0};
                for (uint64_t i = 0; i < element_size && i < 16; ++i) {
                    doctype[i] = static_cast<char>(data[element_data_pos + i]);
                }

                if (std::strcmp(doctype, "matroska") == 0) {
                    evidence += EBML_WEIGHTS.container_weight;
                    flags = flags | MatchFlags::ContainerParsed;

                    return MatchResult{
                        {FileType::Video, L"mkv", L"MKV"},
                        normalize_confidence(evidence, EBML_WEIGHTS),
                        flags
                    };
                }

                if (std::strcmp(doctype, "webm") == 0) {
                    evidence += EBML_WEIGHTS.container_weight;
                    flags = flags | MatchFlags::ContainerParsed;

                    return MatchResult{
                        {FileType::Video, L"webm", L"WebM"},
                        normalize_confidence(evidence, EBML_WEIGHTS),
                        flags
                    };
                }
            }
            break;
        }

        pos = element_data_pos + element_size;
    }

    // Default to MKV if DocType not found
    return MatchResult{
        {FileType::Video, L"mkv", L"MKV"},
        normalize_confidence(evidence, EBML_WEIGHTS),
        flags
    };
}

} // namespace disk_recover