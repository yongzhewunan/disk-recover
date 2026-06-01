#include <gtest/gtest.h>
#include "business/multi_target_writer.hpp"
#include "business/recovery_manager.hpp"
#include <filesystem>

using namespace disk_recover;

class MultiTargetWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "recover_test";
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::filesystem::path test_dir_;
};

TEST_F(MultiTargetWriterTest, AddTarget) {
    MultiTargetWriter writer;
    writer.add_target(test_dir_.wstring());
    EXPECT_EQ(writer.targets().size(), 1u);
    EXPECT_FALSE(writer.current_target().empty());
}

TEST_F(MultiTargetWriterTest, OpenAndWriteFile) {
    MultiTargetWriter writer;
    writer.add_target(test_dir_.wstring());

    EXPECT_TRUE(writer.open_file(L"subdir/test.txt"));
    const char* data = "Hello, World!";
    EXPECT_EQ(writer.write(reinterpret_cast<const uint8_t*>(data), 13), 13u);
    writer.close_file();

    std::filesystem::path file_path = test_dir_ / "subdir" / "test.txt";
    EXPECT_TRUE(std::filesystem::exists(file_path));
}

TEST_F(MultiTargetWriterTest, HasSpace) {
    MultiTargetWriter writer;
    writer.add_target(test_dir_.wstring());
    EXPECT_TRUE(writer.has_space(1024));
}

TEST_F(MultiTargetWriterTest, AutoSwitchEnabled) {
    MultiTargetWriter writer;
    EXPECT_TRUE(writer.auto_switch_enabled());
    writer.set_auto_switch(false);
    EXPECT_FALSE(writer.auto_switch_enabled());
}

TEST(RecoveryStatsTest, DefaultValues) {
    RecoveryManager::RecoveryStats stats;
    EXPECT_EQ(stats.files_recovered, 0u);
    EXPECT_EQ(stats.bytes_recovered, 0u);
    EXPECT_EQ(stats.total_files, 0u);
    EXPECT_EQ(stats.files_failed, 0u);
}

TEST(RecoveryConfigTest, DefaultValues) {
    RecoveryConfig config;
    EXPECT_TRUE(config.save_dirs.empty());
    EXPECT_EQ(config.sector_size, 512u);
    EXPECT_EQ(config.min_free_bytes, 1ULL << 30);
    EXPECT_EQ(config.hwnd, nullptr);
}
