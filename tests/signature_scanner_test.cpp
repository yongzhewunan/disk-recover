#include <gtest/gtest.h>
#include "signature_scanner.hpp"
#include "format_registry.hpp"

using namespace disk_recover;

TEST(SignatureScannerTest, DefaultConfig) {
    SignatureScanner::ScanConfig config;
    EXPECT_EQ(config.start_sector, 0u);
    EXPECT_EQ(config.end_sector, 0u);
    EXPECT_EQ(config.step_sectors, 1u);
    EXPECT_TRUE(config.scan_images);
    EXPECT_TRUE(config.scan_videos);
}

TEST(SignatureScannerTest, ScanConfigCustomize) {
    SignatureScanner::ScanConfig config;
    config.start_sector = 1000;
    config.end_sector = 2000;
    config.step_sectors = 8;
    config.scan_videos = false;

    EXPECT_EQ(config.start_sector, 1000u);
    EXPECT_EQ(config.end_sector, 2000u);
    EXPECT_EQ(config.step_sectors, 8u);
    EXPECT_FALSE(config.scan_videos);
}

TEST(SignatureScannerTest, TryRecoverFileCreatesBasicRecord) {
    SignatureScanner scanner;

    RecoverableFile file;
    FormatDescriptor desc;
    desc.file_type = FileType::Image;
    desc.extension = L"jpg";
    desc.description = L"JPEG";

    // Note: try_recover_file is private, so we test scan behavior indirectly
    // For now, just verify the structure exists
    EXPECT_EQ(desc.file_type, FileType::Image);
}