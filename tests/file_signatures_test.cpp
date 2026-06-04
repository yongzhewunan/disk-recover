#include <gtest/gtest.h>
#include "file_signatures.hpp"

using namespace disk_recover;

TEST(FileSignaturesTest, MatchJpegHeader) {
    // Minimal valid JPEG: SOI + APP0(JFIF) + SOF0 + EOI
    // APP0 (JFIF): FF E0 + length(16) + "JFIF\0" + version + units + density + thumbnail
    // SOF0: FF C0 + length(11) + precision(8) + height(1) + width(1) + components(1) + sampling
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
        0xFF, 0xD9    // EOI
    };
    auto sig = FileSignatures::match(data, sizeof(data));
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->file_type, FileType::Image);
    EXPECT_EQ(sig->extension, L"jpg");
}

TEST(FileSignaturesTest, MatchPngHeader) {
    uint8_t data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    auto sig = FileSignatures::match(data, sizeof(data));
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->file_type, FileType::Image);
    EXPECT_EQ(sig->extension, L"png");
}

TEST(FileSignaturesTest, MatchMp4Header) {
    // Real MP4 has 'ftyp' at offset 4, followed by brand at offset 8
    // Example: size (4 bytes), 'ftyp' (4 bytes), brand 'isom' (4 bytes)
    uint8_t data[] = {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6F, 0x6D};  // ftyp + isom
    auto sig = FileSignatures::match(data, sizeof(data));
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->file_type, FileType::Video);
    EXPECT_EQ(sig->extension, L"mp4");
}

TEST(FileSignaturesTest, MatchAviHeader) {
    uint8_t data[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20};
    auto sig = FileSignatures::match(data, sizeof(data));
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(sig->file_type, FileType::Video);
    EXPECT_EQ(sig->extension, L"avi");
}

TEST(FileSignaturesTest, NoMatchGarbage) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto sig = FileSignatures::match(data, sizeof(data));
    EXPECT_FALSE(sig.has_value());
}
