#include <gtest/gtest.h>
#include "format_registry.hpp"
#include "validation.hpp"
#include "format_descriptor.hpp"
#include "types.hpp"

using namespace disk_recover;

// ════════════════════════════════════════════════════════════════
// FormatRegistry Tests
// ════════════════════════════════════════════════════════════════

TEST(FormatRegistryTest, SingletonInstance) {
    auto& a = FormatRegistry::instance();
    auto& b = FormatRegistry::instance();
    EXPECT_EQ(&a, &b);
}

TEST(FormatRegistryTest, HasRegisteredFormats) {
    auto& registry = FormatRegistry::instance();
    EXPECT_GT(registry.formats().size(), 0u);
}

TEST(FormatRegistryTest, FirstByteIndexBuilt) {
    auto& registry = FormatRegistry::instance();
    // Force a match to trigger index build
    uint8_t garbage[] = {0x00, 0x00, 0x00, 0x00};
    registry.match(garbage, sizeof(garbage));
    EXPECT_TRUE(registry.is_indexed());
}

TEST(FormatRegistryTest, MatchJpegHeader) {
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0xE0,   // APP0 marker
        0x00, 0x10,   // Length: 16
        0x4A, 0x46, 0x49, 0x46, 0x00,  // "JFIF\0"
        0x01, 0x01,   // Version 1.1
        0x00,         // Units
        0x00, 0x01,   // X density
        0x00, 0x01,   // Y density
        0x00, 0x00,   // No thumbnail
        0xFF, 0xC0,   // SOF0 marker
        0x00, 0x0B,   // Length: 11
        0x08,         // Precision: 8 bits
        0x00, 0x01,   // Height: 1
        0x00, 0x01,   // Width: 1
        0x01,         // Components: 1
        0x01, 0x11, 0x00,
        0xFF, 0xDA,   // SOS marker
        0x00, 0x08,   // Length: 8
        0x01, 0x01, 0x00,
        0x00, 0x3F, 0x00,
        0xFF, 0xD9    // EOI
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Image);
    EXPECT_STREQ(result->descriptor->extension, L"jpg");
}

TEST(FormatRegistryTest, MatchPngHeader) {
    // PNG signature only (8 bytes) — should return AcceptHeader
    uint8_t data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Image);
    EXPECT_STREQ(result->descriptor->extension, L"png");
}

TEST(FormatRegistryTest, MatchBmpHeader) {
    uint8_t data[] = {
        0x42, 0x4D,             // "BM"
        0x36, 0x00, 0x00, 0x00, // File size: 54
        0x00, 0x00,             // Reserved1
        0x00, 0x00,             // Reserved2
        0x36, 0x00, 0x00, 0x00, // Pixel offset: 54
        0x28, 0x00, 0x00, 0x00, // DIB header size: 40
        0x01, 0x00, 0x00, 0x00, // Width: 1
        0x01, 0x00, 0x00, 0x00, // Height: 1
        0x01, 0x00,             // Planes: 1
        0x18, 0x00,             // BPP: 24
        0x00, 0x00, 0x00, 0x00, // Compression: BI_RGB
        0x00, 0x00, 0x00, 0x00, // Image size
        0x00, 0x00, 0x00, 0x00, // X pixels/m
        0x00, 0x00, 0x00, 0x00, // Y pixels/m
        0x00, 0x00, 0x00, 0x00, // Colors used
        0x00, 0x00, 0x00, 0x00  // Important colors
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->result, ValidateResult::AcceptVerified);
    EXPECT_EQ(result->descriptor->file_type, FileType::Image);
    EXPECT_STREQ(result->descriptor->extension, L"bmp");
    EXPECT_EQ(result->calculated_file_size, 54u);
}

TEST(FormatRegistryTest, MatchPdfHeader) {
    const uint8_t data[] = {0x25, 0x50, 0x44, 0x46, 0x2D, 0x31, 0x2E, 0x34};  // %PDF-1.4
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Document);
    EXPECT_STREQ(result->descriptor->extension, L"pdf");
}

TEST(FormatRegistryTest, MatchZipHeader) {
    // Minimal ZIP local file header: PK\x03\x04
    uint8_t data[] = {
        0x50, 0x4B, 0x03, 0x04,  // Local file header signature
        0x14, 0x00,               // Version needed: 20
        0x00, 0x00,               // General purpose flags
        0x08, 0x00,               // Compression: deflate
        0x00, 0x00,               // Last mod time
        0x00, 0x00,               // Last mod date
        0x00, 0x00, 0x00, 0x00,  // CRC-32
        0x00, 0x00, 0x00, 0x00,  // Compressed size
        0x00, 0x00, 0x00, 0x00,  // Uncompressed size
        0x01, 0x00,               // Filename length: 1
        0x00, 0x00,               // Extra field length: 0
        0x61                      // Filename: "a"
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Archive);
    EXPECT_STREQ(result->descriptor->extension, L"zip");
}

TEST(FormatRegistryTest, Match7zHeader) {
    // Minimal valid 7z header: 6-byte signature + version + start header fields
    uint8_t data[32] = {};
    // 7z signature
    data[0] = 0x37; data[1] = 0x7A; data[2] = 0xBC; data[3] = 0xAF;
    data[4] = 0x27; data[5] = 0x1C;
    // Version: 0.4
    data[6] = 0x00; data[7] = 0x04;
    // StartHeaderCRC (non-zero to pass validation)
    data[8] = 0x01; data[9] = 0x00; data[10] = 0x00; data[11] = 0x00;
    // NextHeaderOffset, NextHeaderSize, NextHeaderCRC (zeros)
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Archive);
    EXPECT_STREQ(result->descriptor->extension, L"7z");
}

TEST(FormatRegistryTest, MatchRarHeader) {
    const uint8_t data[] = {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00};  // Rar!\x1A\x07\x00
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Archive);
    EXPECT_STREQ(result->descriptor->extension, L"rar");
}

TEST(FormatRegistryTest, MatchDocOle2Header) {
    const uint8_t data[] = {
        0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1,  // OLE2 signature
        0xFE, 0xFF,  // Byte order: little-endian (0xFFFE)
        0x00, 0x02,  // Sector size power: 9 (512 bytes)
        0x00, 0x06,  // Mini sector size power: 6 (64 bytes)
        0x00, 0x00, 0x00, 0x00,  // Reserved
        0x00, 0x00, 0x00, 0x00,  // Total directory sectors (0 for V3)
        0x00, 0x00, 0x00, 0x00,  // Total FAT sectors
        0x02, 0x00, 0x00, 0x00,  // First directory sector ID
        0x00, 0x00, 0x00, 0x00,  // First mini FAT sector ID
        0x00, 0x00, 0x00, 0x00,  // First DIFAT sector ID
        0x00, 0x00, 0x00, 0x00,  // Total DIFAT sectors
        // 109 DIFAT entries (all zero)
    };
    // Pad to at least 512 bytes
    uint8_t padded[512] = {};
    memcpy(padded, data, sizeof(data));
    auto result = FormatRegistry::instance().match(padded, sizeof(padded));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Document);
    EXPECT_STREQ(result->descriptor->extension, L"doc");
}

TEST(FormatRegistryTest, MatchFlacHeader) {
    // fLaC + STREAMINFO metadata block header
    uint8_t data[] = {
        0x66, 0x4C, 0x61, 0x43,  // "fLaC"
        0x00,                     // Block type: STREAMINFO (0), not last
        0x00, 0x00, 0x22,         // Block size: 34
        // 34 bytes of STREAMINFO data (zeros for simplicity)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Audio);
    EXPECT_STREQ(result->descriptor->extension, L"flac");
}

TEST(FormatRegistryTest, NoMatchGarbage) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

TEST(FormatRegistryTest, NoMatchEmpty) {
    auto result = FormatRegistry::instance().match(nullptr, 0);
    EXPECT_FALSE(result.has_value());
}

// ════════════════════════════════════════════════════════════════
// ValidateResult Tests
// ════════════════════════════════════════════════════════════════

TEST(ValidateResultTest, ComparisonOperators) {
    EXPECT_TRUE(ValidateResult::AcceptVerified >= ValidateResult::AcceptContainer);
    EXPECT_TRUE(ValidateResult::AcceptContainer >= ValidateResult::AcceptStructure);
    EXPECT_TRUE(ValidateResult::AcceptStructure >= ValidateResult::AcceptHeader);
    EXPECT_TRUE(ValidateResult::AcceptHeader >= ValidateResult::Reject);
    EXPECT_TRUE(ValidateResult::AcceptVerified > ValidateResult::Reject);
    EXPECT_TRUE(ValidateResult::Reject < ValidateResult::AcceptHeader);
}

TEST(ValidateResultTest, ConfidenceMapping) {
    EXPECT_EQ(validate_result_to_confidence(ValidateResult::Reject), 0u);
    EXPECT_EQ(validate_result_to_confidence(ValidateResult::AcceptHeader), 25u);
    EXPECT_EQ(validate_result_to_confidence(ValidateResult::AcceptStructure), 50u);
    EXPECT_EQ(validate_result_to_confidence(ValidateResult::AcceptContainer), 75u);
    EXPECT_EQ(validate_result_to_confidence(ValidateResult::AcceptVerified), 100u);
}

// ════════════════════════════════════════════════════════════════
// FileType Tests
// ════════════════════════════════════════════════════════════════

TEST(FileTypeTest, AllTypesExist) {
    EXPECT_EQ(static_cast<uint8_t>(FileType::Unknown), 0u);
    EXPECT_EQ(static_cast<uint8_t>(FileType::Image), 1u);
    EXPECT_EQ(static_cast<uint8_t>(FileType::Video), 2u);
    EXPECT_EQ(static_cast<uint8_t>(FileType::Audio), 3u);
    EXPECT_EQ(static_cast<uint8_t>(FileType::Document), 4u);
    EXPECT_EQ(static_cast<uint8_t>(FileType::Archive), 5u);
}
