#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_riff(const uint8_t* data, size_t length) {
    // Phase 1: RIFF header validation
    // RIFF signature: 52 49 46 46
    if (length < 12) return std::nullopt;

    if (!has_str(data, length, 0, "RIFF")) return std::nullopt;

    float evidence = RIFF_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Get file size from RIFF header (bytes 4-7)
    // Note: RIFF size field does NOT include the first 8 bytes (RIFF + size)
    // So actual file size = file_size + 8
    uint32_t riff_size = read_le32(data + 4);
    uint64_t verified_file_size = 0;

    if (riff_size < 4) {
        // Invalid size
        flags = flags | MatchFlags::PartialMatch;
    } else {
        // Calculate actual file size
        verified_file_size = static_cast<uint64_t>(riff_size) + 8;

        // Check if size matches available data
        if (riff_size > length - 8) {
            // File is truncated - may still be recoverable
            flags = flags | MatchFlags::PartialMatch;
        }
    }

    // Phase 2: Determine container type
    if (has_str(data, length, 8, "WEBP")) {
        // WebP container
        evidence += 10.0f;  // Structure evidence

        // Check for VP8/VP8L/VP8X chunk
        if (length >= 20) {
            if (has_str(data, length, 12, "VP8 ") ||
                has_str(data, length, 12, "VP8L") ||
                has_str(data, length, 12, "VP8X")) {
                evidence += RIFF_WEIGHTS.container_weight;
                flags = flags | MatchFlags::ContainerParsed | MatchFlags::DeepValidated;
            }
        }

        return MatchResult{
            {FileType::Image, L"webp", L"WebP"},
            normalize_confidence(evidence, RIFF_WEIGHTS),
            flags,
            verified_file_size
        };
    }

    if (has_str(data, length, 8, "AVI ")) {
        // AVI container
        evidence += 10.0f;  // Structure evidence

        // Check for LIST hdrl (AVI header list)
        // Search for "LIST" chunk with "hdrl" type
        for (size_t i = 12; i + 12 <= length; i += 4) {
            if (has_str(data, length, i, "LIST")) {
                uint32_t list_size = read_le32(data + i + 4);
                if (i + 8 + list_size <= length && has_str(data, length, i + 8, "hdrl")) {
                    evidence += RIFF_WEIGHTS.container_weight;
                    flags = flags | MatchFlags::ContainerParsed | MatchFlags::DeepValidated;
                    break;
                }
            }
        }

        return MatchResult{
            {FileType::Video, L"avi", L"AVI"},
            normalize_confidence(evidence, RIFF_WEIGHTS),
            flags,
            verified_file_size
        };
    }

    if (has_str(data, length, 8, "WAVE")) {
        // WAV audio
        evidence += 10.0f;

        // Check for fmt chunk
        for (size_t i = 12; i + 8 <= length; i += 4) {
            if (has_str(data, length, i, "fmt ")) {
                evidence += RIFF_WEIGHTS.container_weight;
                flags = flags | MatchFlags::ContainerParsed | MatchFlags::DeepValidated;
                break;
            }
        }

        return MatchResult{
            {FileType::Video, L"wav", L"WAV"},
            normalize_confidence(evidence, RIFF_WEIGHTS),
            flags,
            verified_file_size
        };
    }

    // Unknown RIFF container
    flags = flags | MatchFlags::PartialMatch;

    return MatchResult{
        {FileType::Video, L"riff", L"RIFF"},
        normalize_confidence(evidence, RIFF_WEIGHTS),
        flags,
        verified_file_size
    };
}

} // namespace disk_recover