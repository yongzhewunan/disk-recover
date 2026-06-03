#pragma once
#include <cstdint>
#include <string_view>
#include <optional>
#include "types.hpp"

namespace disk_recover {

// BMFF brand entry for ISO Base Media File Format containers
// Used for MP4, MOV, HEIC, AVIF, 3GP, and other ISO BMFF-based formats
struct BmffBrandEntry {
    std::string_view brand;          // 4-character brand code
    FileType file_type;              // Image or Video
    std::wstring_view extension;     // File extension
    std::wstring_view description;   // Human-readable description
    uint8_t base_confidence;         // Base confidence when brand matches (0-100)
};

// Ordered by specificity (more specific brands first)
// This enables correct classification when multiple brands could apply
constexpr BmffBrandEntry BMFF_BRANDS[] = {
    // HEIF/HEIC - High Efficiency Image Format
    {"heic", FileType::Image, L"heic", L"HEIC", 95},
    {"heix", FileType::Image, L"heic", L"HEIC", 95},
    {"hevc", FileType::Image, L"heic", L"HEIC", 95},
    {"hevx", FileType::Image, L"heic", L"HEIC", 95},
    {"mif1", FileType::Image, L"heic", L"HEIF", 92},
    {"msf1", FileType::Image, L"heic", L"HEIF", 92},

    // AVIF - AV1 Image Format
    {"avif", FileType::Image, L"avif", L"AVIF", 95},
    {"avis", FileType::Image, L"avif", L"AVIF", 95},

    // QuickTime MOV
    {"qt  ", FileType::Video, L"mov", L"MOV", 95},

    // 3GPP - 3rd Generation Partnership Project
    {"3gp4", FileType::Video, L"3gp", L"3GP", 90},
    {"3gp5", FileType::Video, L"3gp", L"3GP", 90},
    {"3gp6", FileType::Video, L"3gp", L"3GP", 90},
    {"3g2a", FileType::Video, L"3g2", L"3G2", 88},
    {"3g2b", FileType::Video, L"3g2", L"3G2", 88},
    {"3g2c", FileType::Video, L"3g2", L"3G2", 88},

    // MP4 - MPEG-4 Part 14
    {"isom", FileType::Video, L"mp4", L"MP4", 85},
    {"iso2", FileType::Video, L"mp4", L"MP4", 85},
    {"iso3", FileType::Video, L"mp4", L"MP4", 85},
    {"iso4", FileType::Video, L"mp4", L"MP4", 85},
    {"iso5", FileType::Video, L"mp4", L"MP4", 85},
    {"iso6", FileType::Video, L"mp4", L"MP4", 85},
    {"mp41", FileType::Video, L"mp4", L"MP4", 85},
    {"mp42", FileType::Video, L"mp4", L"MP4", 85},
    {"avc1", FileType::Video, L"mp4", L"MP4", 85},
    {"avc2", FileType::Video, L"mp4", L"MP4", 85},
    {"avc3", FileType::Video, L"mp4", L"MP4", 85},
    {"dash", FileType::Video, L"mp4", L"MP4", 85},

    // M4V - iTunes Video
    {"M4V ", FileType::Video, L"m4v", L"M4V", 85},
    {"M4VH", FileType::Video, L"m4v", L"M4V", 85},
    {"M4VP", FileType::Video, L"m4v", L"M4V", 85},

    // M4A - iTunes Audio (treat as video container)
    {"M4A ", FileType::Video, L"m4a", L"M4A", 80},

    // F4V - Flash Video (ISO BMFF variant)
    {"f4v ", FileType::Video, L"f4v", L"F4V", 85},

    // JPEG 2000
    {"jp2 ", FileType::Image, L"jp2", L"JPEG 2000", 90},
    {"jpm ", FileType::Image, L"jpm", L"JPEG 2000 Compound", 90},
    {"jpx ", FileType::Image, L"jpx", L"JPEG 2000 Extended", 90},
};

// Number of entries in the brand dictionary
constexpr size_t BMFF_BRAND_COUNT = sizeof(BMFF_BRANDS) / sizeof(BmffBrandEntry);

// Look up a brand by its 4-character code
// Returns the matching entry or nullopt if not found
inline std::optional<BmffBrandEntry> lookup_brand(std::string_view brand) {
    for (const auto& entry : BMFF_BRANDS) {
        if (entry.brand == brand) {
            return entry;
        }
    }
    return std::nullopt;
}

// Check if a 4-byte sequence matches a known brand
// data must point to at least 4 bytes
inline std::optional<BmffBrandEntry> lookup_brand(const uint8_t* data) {
    if (!data) return std::nullopt;

    std::string_view brand(
        reinterpret_cast<const char*>(data), 4);
    return lookup_brand(brand);
}

// Check if brand indicates an image format
inline bool is_image_brand(std::string_view brand) {
    auto entry = lookup_brand(brand);
    return entry.has_value() && entry->file_type == FileType::Image;
}

// Check if brand indicates a video format
inline bool is_video_brand(std::string_view brand) {
    auto entry = lookup_brand(brand);
    return entry.has_value() && entry->file_type == FileType::Video;
}

} // namespace disk_recover