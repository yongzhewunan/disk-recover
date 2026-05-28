#include <gtest/gtest.h>
#include "fat_parser.hpp"

using namespace disk_recover::fat;

TEST(FatTypesTest, BootSectorSize) {
    EXPECT_GE(sizeof(FatBootSector), 80u);
}

TEST(FatParserTest, DefaultState) {
    FatParser parser;
    EXPECT_EQ(parser.fat_type(), FatType::Unknown);
    EXPECT_EQ(parser.cluster_size(), 512u);
}

TEST(FatParserTest, ClusterTypeDetection) {
    FatParser parser;
    // These are just compile checks for the constants
    EXPECT_GE(FAT12_EOC, 0x0FF8u);
    EXPECT_GE(FAT16_EOC, 0xFFF8u);
    EXPECT_GE(FAT32_EOC, 0x0FFFFFF8u);
}
