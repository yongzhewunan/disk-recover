#include <gtest/gtest.h>
#include "format_registry.hpp"

using namespace disk_recover;

TEST(FileSignaturesTest, MatchJpegHeader) {
    // Minimal valid JPEG: SOI + APP0(JFIF) + SOF0 + SOS + EOI
    // APP0 (JFIF): FF E0 + length(16) + "JFIF\0" + version + units + density + thumbnail
    // SOF0: FF C0 + length(11) + precision(8) + height(1) + width(1) + components(1) + sampling
    // SOS: FF DA + length(8) + components(1) + component spec + spectral selection
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0xE0,   // APP0 marker
        0x00, 0x10,   // Length: 16
        0x4A, 0x46, 0x49, 0x46, 0x00,  // "JFIF\0"
        0x01, 0x01,   // Version 1.1
        0x00,         // Units: no units
        0x00, 0x01,   // X density
        0x00, 0x01,   // Y density
        0x00, 0x00,   // No thumbnail
        0xFF, 0xC0,   // SOF0 marker
        0x00, 0x0B,   // Length: 11
        0x08,         // Precision: 8 bits
        0x00, 0x01,   // Height: 1
        0x00, 0x01,   // Width: 1
        0x01,         // Components: 1 (grayscale)
        0x01, 0x11, 0x00,  // Component: ID=1, sampling=1x1, quant table=0
        0xFF, 0xDA,   // SOS marker
        0x00, 0x08,   // Length: 8
        0x01,         // Number of components: 1
        0x01, 0x00,   // Component 1: selector=1, DC=0, AC=0
        0x00, 0x3F, 0x00,  // Spectral selection: 0-63, successive approx: 0
        0xFF, 0xD9    // EOI
    };
    auto match = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->descriptor->file_type, FileType::Image);
    // Extension should be jpg or jpeg (TestDisk uses "jpg")
    EXPECT_TRUE(match->descriptor->extension == std::wstring(L"jpg") ||
                match->descriptor->extension == std::wstring(L"jpeg"));
}

TEST(FileSignaturesTest, MatchPngHeader) {
    // PNG signature + valid IHDR chunk (TestDisk requires IHDR with valid fields)
    uint8_t data[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,  // IHDR chunk length: 13
        0x49, 0x48, 0x44, 0x52,  // "IHDR"
        0x00, 0x00, 0x00, 0x01,  // Width: 1
        0x00, 0x00, 0x00, 0x01,  // Height: 1
        0x08,                    // Bit depth: 8
        0x02,                    // Color type: 2 (Truecolour)
        0x00,                    // Compression: 0
        0x00,                    // Filter: 0
        0x00,                    // Interlace: 0
        0x00, 0x00, 0x00, 0x00   // CRC (placeholder)
    };
    auto match = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->descriptor->file_type, FileType::Image);
    EXPECT_EQ(match->descriptor->extension, std::wstring(L"png"));
}

TEST(FileSignaturesTest, MatchMp4Header) {
    // Real MP4 has 'ftyp' at offset 4, followed by brand at offset 8
    // Example: size (4 bytes), 'ftyp' (4 bytes), brand 'isom' (4 bytes)
    uint8_t data[] = {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6F, 0x6D};  // ftyp + isom
    auto match = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->descriptor->file_type, FileType::Video);
    // MOV validator handles MP4/MOV/HEIC - extension may be "mov" or "mp4"
    EXPECT_TRUE(match->descriptor->extension == std::wstring(L"mov") ||
                match->descriptor->extension == std::wstring(L"mp4") ||
                match->descriptor->extension == std::wstring(L"m4v"));
}

TEST(FileSignaturesTest, MatchAviHeader) {
    uint8_t data[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20};
    auto match = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->descriptor->file_type, FileType::Video);
    // RIFF validator handles AVI - extension may be "avi" or "riff"
    EXPECT_TRUE(match->descriptor->extension == std::wstring(L"avi") ||
                match->descriptor->extension == std::wstring(L"riff"));
}

TEST(FileSignaturesTest, NoMatchGarbage) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto match = FormatRegistry::instance().match(data, sizeof(data));
    EXPECT_FALSE(match.has_value());
}