#include <gtest/gtest.h>
#include "disk_handle.hpp"

using namespace disk_recover;

TEST(DiskHandleTest, DefaultConstructIsInvalid) {
    DiskHandle handle;
    EXPECT_FALSE(handle.is_open());
}

TEST(DiskHandleTest, OpenInvalidPathFails) {
    DiskHandle handle;
    auto result = handle.open(L"\\\\.\\InvalidDevice999");
    EXPECT_FALSE(result);
}

TEST(DiskHandleTest, CloseAfterOpenIsClean) {
    DiskHandle handle;
    handle.close();
    EXPECT_FALSE(handle.is_open());
}