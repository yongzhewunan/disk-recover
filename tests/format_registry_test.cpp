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
    EXPECT_GE(registry.formats().size(), 18u);
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

// Test: JPEG with APP1 Exif header should match
TEST(FormatRegistryTest, MatchJpegExifHeader) {
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0xE1,   // APP1 marker
        0x00, 0x10,   // Length: 16 (minimum for Exif header)
        0x45, 0x78, 0x69, 0x66, 0x00, 0x00,  // "Exif\0\0"
        0x49, 0x49,   // TIFF header: little-endian "II"
        0x2A, 0x00,   // TIFF magic: 42
        0x08, 0x00, 0x00, 0x00,  // Offset to first IFD
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

// Test: JPEG with invalid fourth byte should be rejected
TEST(FormatRegistryTest, RejectJpegInvalidFourthByte) {
    // FF D8 FF followed by invalid marker (0x00)
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0x00,   // Invalid: FF 00 is byte stuffing, not a marker
        0x00, 0x10,   // Random data
        0x4A, 0x46, 0x49, 0x46, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

// Test: JPEG APP0 without JFIF identifier should be rejected
TEST(FormatRegistryTest, RejectJpegApp0NoJfif) {
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0xE0,   // APP0 marker
        0x00, 0x10,   // Length: 16
        'X', 'Y', 'Z', 'W', 0x00,  // Invalid: not "JFIF"
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0xFF, 0xD9    // EOI
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

// Test: JPEG APP1 without Exif identifier should be rejected
TEST(FormatRegistryTest, RejectJpegApp1NoExif) {
    uint8_t data[] = {
        0xFF, 0xD8,   // SOI
        0xFF, 0xE1,   // APP1 marker
        0x00, 0x10,   // Length: 16
        'X', 'Y', 'Z', 'W', 0x00, 0x00,  // Invalid: not "Exif"
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0xFF, 0xD9    // EOI
    };
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

TEST(FormatRegistryTest, MatchPngHeader) {
    // PNG signature + valid IHDR chunk (TestDisk's header_check_png requires IHDR)
    // PNG signature (8 bytes) + chunk length (4 bytes) + "IHDR" (4 bytes) + IHDR data (13 bytes) + CRC (4 bytes)
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
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Image);
    EXPECT_STREQ(result->descriptor->extension, L"png");
}

TEST(FormatRegistryTest, MatchBmpHeader) {
    // BMP with all fields valid per TestDisk's header_check_bmp requirements:
    // - size >= 65, offset < size, hdr_size < size
    // - hdr_size in {12,40,52,56,64,108,124}
    // - reserved fields at offset 6-9 must be zero
    // Note: TestDisk's BMP validator only checks the file header (14 bytes) +
    // hdr_size field; it doesn't validate DIB fields beyond that.
    uint8_t data[70] = {};  // 14 (file header) + 40 (DIB) + 16 (pixel data)
    data[0] = 0x42; data[1] = 0x4D;             // "BM"
    // File size: 70 (LE32 at offset 2) — must be >= 65 per TestDisk
    data[2] = 0x46; data[3] = 0x00; data[4] = 0x00; data[5] = 0x00;
    // Reserved1+2 (offsets 6-9) — already zero (required by TestDisk check)
    data[10] = 0x36; data[11] = 0x00; data[12] = 0x00; data[13] = 0x00; // Pixel offset: 54
    data[14] = 0x28; data[15] = 0x00; data[16] = 0x00; data[17] = 0x00; // DIB header size: 40
    data[18] = 0x01; data[19] = 0x00; data[20] = 0x00; data[21] = 0x00; // Width: 1
    data[22] = 0x01; data[23] = 0x00; data[24] = 0x00; data[25] = 0x00; // Height: 1
    data[26] = 0x01; data[27] = 0x00;  // Planes: 1
    data[28] = 0x18; data[29] = 0x00;  // BPP: 24
    // Compression: BI_RGB (0) at offset 30 — already zero
    // Image size at offset 34: 4 (1 row × 4 bytes aligned)
    data[34] = 0x04; data[35] = 0x00; data[36] = 0x00; data[37] = 0x00;
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Image);
    EXPECT_STREQ(result->descriptor->extension, L"bmp");
    // TestDisk's BMP validator sets calculated_file_size = file_size field (70)
    EXPECT_EQ(result->calculated_file_size, 70u);
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
    // Minimal ZIP local file header per TestDisk's header_check_zip requirements:
    // - filename_length > 0 and <= 4096
    // - version >= 10
    // The ZIP local file header structure (after PK\x03\x04):
    //   offset 4:  version_needed (2 bytes, LE)
    //   offset 6:  flags (2 bytes)
    //   offset 8:  compression (2 bytes)
    //   offset 10: mod_time (2 bytes)
    //   offset 12: mod_date (2 bytes)
    //   offset 14: crc32 (4 bytes)
    //   offset 18: compressed_size (4 bytes)
    //   offset 22: uncompressed_size (4 bytes)
    //   offset 26: filename_length (2 bytes, LE)
    //   offset 28: extra_length (2 bytes, LE)
    //   offset 30: filename (filename_length bytes)
    uint8_t data[] = {
        0x50, 0x4B, 0x03, 0x04,  // Local file header signature
        0x14, 0x00,               // Version needed: 20 (>= 10)
        0x00, 0x00,               // General purpose flags
        0x08, 0x00,               // Compression: deflate
        0x00, 0x00,               // Last mod time
        0x00, 0x00,               // Last mod date
        0x00, 0x00, 0x00, 0x00,  // CRC-32
        0x00, 0x00, 0x00, 0x00,  // Compressed size
        0x00, 0x00, 0x00, 0x00,  // Uncompressed size
        0x01, 0x00,               // Filename length: 1 (> 0)
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
    // Minimal valid 7z header per TestDisk's header_check_7z requirements:
    // - majorversion == 0
    // - nextHeaderSize != 0
    // - nextHeaderOffset and nextHeaderSize < 0x7000000000000000
    // 7z signature header: 6 bytes sig + 1 byte majorVer + 1 byte minorVer +
    //   4 bytes StartHeaderCRC + 8 bytes NextHeaderOffset + 8 bytes NextHeaderSize +
    //   8 bytes NextHeaderCRC = 32 bytes total
    uint8_t data[32] = {};
    // 7z signature
    data[0] = 0x37; data[1] = 0x7A; data[2] = 0xBC; data[3] = 0xAF;
    data[4] = 0x27; data[5] = 0x1C;
    // Version: 0.4
    data[6] = 0x00; data[7] = 0x04;
    // StartHeaderCRC (4 bytes at offset 8) — don't care about value
    data[8] = 0x01; data[9] = 0x00; data[10] = 0x00; data[11] = 0x00;
    // NextHeaderOffset (8 bytes at offset 12): 32 (point past the start header)
    data[12] = 0x20; data[13] = 0x00; data[14] = 0x00; data[15] = 0x00;
    data[16] = 0x00; data[17] = 0x00; data[18] = 0x00; data[19] = 0x00;
    // NextHeaderSize (8 bytes at offset 20): must be non-zero
    data[20] = 0x01; data[21] = 0x00; data[22] = 0x00; data[23] = 0x00;
    data[24] = 0x00; data[25] = 0x00; data[26] = 0x00; data[27] = 0x00;
    // NextHeaderCRC (8 bytes at offset 28) — don't care
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
    // OLE2/DOC header per TestDisk's header_check_doc requirements:
    // - uByteOrder == 0xFFFE (little-endian) at offset 28
    // - uDllVersion == 3 or 4 at offset 26
    // - reserved == 0 at offset 24, reserved1 == 0 at offset 46
    // - uMiniSectorShift == 6 at offset 32
    // - uSectorShift == 9 (for v3) at offset 30
    // - num_FAT_blocks > 0 at offset 44
    // - csectDir == 0 (for v3) at offset 48
    // - num_extra_FAT_blocks <= 50 at offset 42
    // The OLE2 header is 512 bytes minimum.
    uint8_t data[512] = {};
    // 8-byte OLE2 signature at offset 0
    data[0] = 0xD0; data[1] = 0xCF; data[2] = 0x11; data[3] = 0xE0;
    data[4] = 0xA1; data[5] = 0xB1; data[6] = 0x1A; data[7] = 0xE1;
    // Minor version at offset 24: 0x003E (62) — typical value
    data[24] = 0x3E; data[25] = 0x00;
    // Major version (uDllVersion) at offset 26: 3
    data[26] = 0x03; data[27] = 0x00;
    // Byte order at offset 28: 0xFFFE (little-endian)
    data[28] = 0xFE; data[29] = 0xFF;
    // Sector size power (uSectorShift) at offset 30: 9 (512 bytes, for v3)
    data[30] = 0x09; data[31] = 0x00;
    // Mini sector size power (uMiniSectorShift) at offset 32: 6 (64 bytes)
    data[32] = 0x06; data[33] = 0x00;
    // Reserved at offset 34: 0 (already zero)
    // num_extra_FAT_blocks at offset 42: 0 (must be <= 50)
    data[42] = 0x00; data[43] = 0x00;
    // Total FAT sectors (num_FAT_blocks) at offset 44: 1 (must be > 0)
    data[44] = 0x01; data[45] = 0x00; data[46] = 0x00; data[47] = 0x00;
    // First directory sector SECID at offset 48: 0 (valid for v3)
    data[48] = 0x00; data[49] = 0x00; data[50] = 0x00; data[51] = 0x00;
    // Mini stream cutoff at offset 56: 4096 (0x1000)
    data[56] = 0x00; data[57] = 0x10; data[58] = 0x00; data[59] = 0x00;
    // First mini FAT sector at offset 60: ENDOFCHAIN (0xFFFFFFFE) = -2
    data[60] = 0xFE; data[61] = 0xFF; data[62] = 0xFF; data[63] = 0xFF;
    // Add "WordDocument" signature so the validator assigns "doc" extension
    // The header_check_doc searches for "WordDocument" in the buffer via td_memmem
    const char* word_sig = "WordDocument";
    memcpy(&data[128], word_sig, strlen(word_sig));
    auto result = FormatRegistry::instance().match(data, sizeof(data));
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->result, ValidateResult::Reject);
    EXPECT_EQ(result->descriptor->file_type, FileType::Document);
    EXPECT_STREQ(result->descriptor->extension, L"doc");
}

TEST(FormatRegistryTest, MatchFlacHeader) {
    // fLaC + STREAMINFO metadata block header
    uint8_t data[] = {
        0x66, 0x4C, 0x61, 0x43,  // "fLaC"
        0x80,                     // Block type: STREAMINFO (0), LAST block flag set
        0x00, 0x00, 0x22,         // Block size: 34
        // STREAMINFO block (34 bytes) with valid fields:
        // bytes 0-1: min block size: 16
        0x00, 0x10,
        // bytes 2-3: max block size: 4096
        0x10, 0x00,
        // bytes 4-6: min frame size: 0 (24 bits)
        0x00, 0x00, 0x00,
        // bytes 7-9: max frame size: 0 (24 bits)
        0x00, 0x00, 0x00,
        // bytes 10-13: sample_rate (20 bits): 44100, channels-1 (3 bits): 1, bps-1 (5 bits): 15
        // (44100 << 12) | (1 << 9) | (15 << 4) = 0x0AC442F0
        0x0A, 0xC4, 0x42, 0xF0,
        // bytes 14-17: total samples lower 32 bits: 0
        0x00, 0x00, 0x00, 0x00,
        // bytes 18-33: MD5 signature: 16 bytes of zeros
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
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
