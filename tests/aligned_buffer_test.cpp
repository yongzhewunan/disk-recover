#include <gtest/gtest.h>
#include "aligned_buffer.hpp"

using namespace disk_recover;

TEST(AlignedBufferTest, DefaultConstructIsEmpty) {
    AlignedBuffer buf;
    EXPECT_EQ(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 0);
}

TEST(AlignedBufferTest, Allocate4096Aligned) {
    AlignedBuffer buf(4096, 4096);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 4096);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(buf.data()) % 4096, 0);
}

TEST(AlignedBufferTest, Allocate512Aligned) {
    AlignedBuffer buf(8192, 512);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 8192);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(buf.data()) % 512, 0);
}

TEST(AlignedBufferTest, MoveConstruct) {
    AlignedBuffer buf(4096, 4096);
    uint8_t* ptr = buf.data();
    AlignedBuffer moved(std::move(buf));
    EXPECT_EQ(moved.data(), ptr);
    EXPECT_EQ(moved.size(), 4096);
    EXPECT_EQ(buf.data(), nullptr);
}

TEST(AlignedBufferTest, MoveAssign) {
    AlignedBuffer buf1(4096, 4096);
    AlignedBuffer buf2(8192, 4096);
    uint8_t* ptr1 = buf1.data();
    buf2 = std::move(buf1);
    EXPECT_EQ(buf2.data(), ptr1);
    EXPECT_EQ(buf2.size(), 4096);
}

TEST(AlignedBufferTest, ResetAndReuse) {
    AlignedBuffer buf(4096, 4096);
    EXPECT_NE(buf.data(), nullptr);
    buf.reset();
    EXPECT_EQ(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 0);
    buf.allocate(8192, 4096);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 8192);
}