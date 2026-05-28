#include <gtest/gtest.h>
#include "mft_parser.hpp"

using namespace disk_recover::ntfs;

TEST(NtfsTypesTest, BootSectorSize) {
    EXPECT_EQ(sizeof(NtfsBootSector), 82u);
}

TEST(NtfsTypesTest, MftRecordHeaderSize) {
    EXPECT_GE(sizeof(MftRecordHeader), 24u);
}

TEST(NtfsTypesTest, AttributeHeaderSize) {
    EXPECT_EQ(sizeof(AttributeHeader), 16u);
}

TEST(MftParserTest, DefaultState) {
    MftParser parser;
    EXPECT_EQ(parser.mft_record_size(), 1024u);
    EXPECT_EQ(parser.cluster_size(), 4096u);
}
