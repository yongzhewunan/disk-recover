#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include <array>
#include "types.hpp"

namespace disk_recover {

// Match flags for quality indicators
enum class MatchFlags : uint32_t {
    None              = 0,
    HasHeader         = 1 << 0,  // Valid header structure found
    HasFooter         = 1 << 1,  // End marker detected (JPEG EOI, PNG IEND)
    DeepValidated     = 1 << 2,  // Extended validation performed
    ContainerParsed   = 1 << 3,  // Container parsed (MP4 atoms, TIFF IFDs)
    PartialMatch      = 1 << 4,  // Incomplete or damaged but recoverable
};

// Bitwise operators for MatchFlags
inline MatchFlags operator|(MatchFlags a, MatchFlags b) {
    return static_cast<MatchFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline MatchFlags operator&(MatchFlags a, MatchFlags b) {
    return static_cast<MatchFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline MatchFlags operator~(MatchFlags a) {
    return static_cast<MatchFlags>(~static_cast<uint32_t>(a));
}
inline MatchFlags& operator|=(MatchFlags& a, MatchFlags b) {
    a = a | b;
    return a;
}
inline MatchFlags& operator&=(MatchFlags& a, MatchFlags b) {
    a = a & b;
    return a;
}
inline bool has_flag(MatchFlags flags, MatchFlags flag) {
    return (flags & flag) == flag;
}

struct FileSignature {
    FileType file_type;
    std::wstring extension;
    std::wstring description;
};

// Match result with confidence scoring
struct MatchResult {
    FileSignature signature;
    uint8_t confidence = 0;      // 0-100 scale, normalized across formats
    MatchFlags flags = MatchFlags::None;
};

class FileSignatures {
public:
    // NEW: Primary interface returning MatchResult with confidence scoring
    static std::optional<MatchResult> match_with_confidence(
        const uint8_t* data, size_t length);

    // LEGACY: Existing interface - internally calls match_with_confidence
    static std::optional<FileSignature> match(const uint8_t* data, size_t length);

    struct SignatureEntry {
        FileType file_type;
        std::wstring extension;
        std::wstring description;
        const uint8_t* pattern;
        size_t pattern_len;
        size_t offset;
    };

    static const std::vector<SignatureEntry>& entries();
};

} // namespace disk_recover
