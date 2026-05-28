#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include "types.hpp"

namespace disk_recover {

struct FileSignature {
    FileType file_type;
    std::wstring extension;
    std::wstring description;
};

class FileSignatures {
public:
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
