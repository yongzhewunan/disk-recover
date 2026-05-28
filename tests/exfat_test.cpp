#include <gtest/gtest.h>
#include "exfat_parser.hpp"

using namespace disk_recover::exfat;

TEST(ExfatTypesTest, BootSectorSize) {
    EXPECT_EQ(sizeof(ExfatBootSector), 512u);
}

TEST(ExfatTypesTest, EntryTypeConstants) {
    EXPECT_EQ(ENTRY_TYPE_FILE, 0x85);
    EXPECT_EQ(ENTRY_TYPE_STREAM, 0xC0);
    EXPECT_EQ(ENTRY_TYPE_NAME, 0xC1);
}

TEST(ExfatParserTest, DefaultState) {
    ExfatParser parser;
    EXPECT_EQ(parser.sector_size(), 512u);
    EXPECT_EQ(parser.cluster_size(), 512u);
}
