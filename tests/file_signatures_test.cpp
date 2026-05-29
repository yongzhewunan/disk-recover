#include <gtest/gtest.h>
#include "file_signatures.hpp"

using namespace disk_recover;

TEST(FileSignaturesTest, MatchJpegHeader) {
    uint8_t data[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
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
