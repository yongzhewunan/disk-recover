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

    // Get file size
    uint32_t file_size = read_le32(data + 4);
    if (file_size < 4 || file_size > length - 8) {
        // Size mismatch - may be truncated or corrupted
        flags = flags | MatchFlags::PartialMatch;
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
            flags
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
            flags
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
            flags
        };
    }

    // Unknown RIFF container
    flags = flags | MatchFlags::PartialMatch;

    return MatchResult{
        {FileType::Video, L"riff", L"RIFF"},
        normalize_confidence(evidence, RIFF_WEIGHTS),
        flags
    };
}

} // namespace disk_recover