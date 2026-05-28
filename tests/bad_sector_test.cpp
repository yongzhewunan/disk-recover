#include <gtest/gtest.h>
#include "bad_sector_manager.hpp"
#include <filesystem>

using namespace disk_recover;

class BadSectorManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() / "bad_sector_test.db";
    }
    void TearDown() override {
        std::filesystem::remove(test_path_);
    }
    std::filesystem::path test_path_;
};

TEST_F(BadSectorManagerTest, RecordAndQuery) {
    BadSectorManager mgr;
    mgr.open(test_path_.wstring());
    mgr.record(1000, 1);
    mgr.record(2000, 2);
    EXPECT_TRUE(mgr.is_bad(1000));
    EXPECT_TRUE(mgr.is_bad(2000));
    EXPECT_TRUE(mgr.is_bad(2001));
    EXPECT_FALSE(mgr.is_bad(3000));
    EXPECT_EQ(mgr.total_bad_sectors(), 3u);
}

TEST_F(BadSectorManagerTest, PersistAndReload) {
    {
        BadSectorManager mgr;
        mgr.open(test_path_.wstring());
        mgr.record(5000, 5);
        mgr.close();
    }
    {
        BadSectorManager mgr;
        mgr.open(test_path_.wstring());
        EXPECT_TRUE(mgr.is_bad(5000));
        EXPECT_TRUE(mgr.is_bad(5004));
        EXPECT_EQ(mgr.total_bad_sectors(), 5u);
    }
}