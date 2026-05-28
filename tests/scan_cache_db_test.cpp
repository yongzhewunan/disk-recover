#include <gtest/gtest.h>
#include "business/scan_cache_db.hpp"
#include <filesystem>

using namespace disk_recover;

class ScanCacheDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_path_ = std::filesystem::temp_directory_path() / "scan_cache_test.db";
        // Remove any existing test db
        std::filesystem::remove(test_path_);
    }
    void TearDown() override {
        std::filesystem::remove(test_path_);
    }
    std::filesystem::path test_path_;
};

TEST_F(ScanCacheDBTest, OpenAndClose) {
    ScanCacheDB db;
    EXPECT_TRUE(db.open(test_path_.wstring()));
    db.close();
}

TEST_F(ScanCacheDBTest, CreateSession) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    EXPECT_TRUE(db.create_session("test_session"));
    db.close();
}

TEST_F(ScanCacheDBTest, InsertAndQueryFile) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    RecoverableFile file;
    file.file_name = L"test.jpg";
    file.file_size = 1024;
    file.file_type = FileType::Image;
    file.is_corrupted = false;
    file.fragments.push_back({1000, 2});

    EXPECT_TRUE(db.insert_file("test_session", file));
    EXPECT_EQ(db.query_file_count("test_session"), 1u);

    auto files = db.query_files("test_session", 10, 0);
    EXPECT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].file_name, L"test.jpg");
    EXPECT_EQ(files[0].fragments.size(), 1u);

    db.close();
}

TEST_F(ScanCacheDBTest, BulkInsertFiles) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    std::vector<RecoverableFile> files;
    for (int i = 0; i < 100; ++i) {
        RecoverableFile file;
        file.file_name = L"file_" + std::to_wstring(i) + L".jpg";
        file.file_size = i * 1024;
        file.file_type = FileType::Image;
        file.is_corrupted = (i % 10 == 0);
        file.fragments.push_back({static_cast<uint64_t>(i * 100), 2});
        files.push_back(file);
    }

    EXPECT_TRUE(db.insert_files_bulk("test_session", files));
    EXPECT_EQ(db.query_file_count("test_session"), 100u);

    // Test pagination
    auto page1 = db.query_files("test_session", 10, 0);
    EXPECT_EQ(page1.size(), 10u);

    auto page2 = db.query_files("test_session", 10, 10);
    EXPECT_EQ(page2.size(), 10u);

    db.close();
}

TEST_F(ScanCacheDBTest, SaveAndLoadProgress) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    ScanProgress progress;
    progress.sectors_scanned = 1000;
    progress.total_sectors = 5000;
    progress.files_found = 10;
    progress.bad_sectors_hit = 2;
    progress.is_paused = false;
    progress.is_complete = false;

    EXPECT_TRUE(db.save_progress("test_session", progress));

    ScanProgress loaded;
    EXPECT_TRUE(db.load_progress("test_session", loaded));
    EXPECT_EQ(loaded.sectors_scanned, 1000u);
    EXPECT_EQ(loaded.total_sectors, 5000u);
    EXPECT_EQ(loaded.files_found, 10u);
    EXPECT_EQ(loaded.bad_sectors_hit, 2u);
    EXPECT_FALSE(loaded.is_paused);
    EXPECT_FALSE(loaded.is_complete);

    db.close();
}

TEST_F(ScanCacheDBTest, SaveAndLoadBadSectors) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    std::vector<uint64_t> sectors = {100, 200, 300, 400, 500};
    EXPECT_TRUE(db.save_bad_sectors("test_session", sectors));

    auto loaded = db.load_bad_sectors("test_session");
    EXPECT_EQ(loaded.size(), 5u);
    EXPECT_EQ(loaded[0], 100u);
    EXPECT_EQ(loaded[4], 500u);

    // Test duplicate handling (should be ignored)
    std::vector<uint64_t> more_sectors = {100, 600, 700};  // 100 is duplicate
    EXPECT_TRUE(db.save_bad_sectors("test_session", more_sectors));

    loaded = db.load_bad_sectors("test_session");
    EXPECT_EQ(loaded.size(), 7u);  // 5 + 2 new (100 duplicate ignored)

    db.close();
}

TEST_F(ScanCacheDBTest, FileWithMftId) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    RecoverableFile file;
    file.file_name = L"ntfs_file.txt";
    file.file_size = 2048;
    file.file_type = FileType::Unknown;
    file.is_corrupted = false;
    file.mft_id = 12345;
    file.fragments.push_back({5000, 4});

    EXPECT_TRUE(db.insert_file("test_session", file));

    auto files = db.query_files("test_session", 10, 0);
    EXPECT_EQ(files.size(), 1u);
    EXPECT_TRUE(files[0].mft_id.has_value());
    EXPECT_EQ(*files[0].mft_id, 12345u);

    db.close();
}

TEST_F(ScanCacheDBTest, MultipleFragments) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    RecoverableFile file;
    file.file_name = L"fragmented.dat";
    file.file_size = 8192;
    file.file_type = FileType::Unknown;
    file.is_corrupted = false;
    file.fragments.push_back({100, 2});
    file.fragments.push_back({200, 3});
    file.fragments.push_back({500, 1});

    EXPECT_TRUE(db.insert_file("test_session", file));

    auto files = db.query_files("test_session", 10, 0);
    EXPECT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].fragments.size(), 3u);
    EXPECT_EQ(files[0].fragments[0].start_sector, 100u);
    EXPECT_EQ(files[0].fragments[0].sector_count, 2u);
    EXPECT_EQ(files[0].fragments[1].start_sector, 200u);
    EXPECT_EQ(files[0].fragments[1].sector_count, 3u);
    EXPECT_EQ(files[0].fragments[2].start_sector, 500u);
    EXPECT_EQ(files[0].fragments[2].sector_count, 1u);

    db.close();
}

TEST_F(ScanCacheDBTest, UnicodeFileName) {
    ScanCacheDB db;
    db.open(test_path_.wstring());
    db.create_session("test_session");

    RecoverableFile file;
    file.file_name = L"中文文件.jpg";  // Chinese characters
    file.file_size = 1024;
    file.file_type = FileType::Image;
    file.is_corrupted = false;

    EXPECT_TRUE(db.insert_file("test_session", file));

    auto files = db.query_files("test_session", 10, 0);
    EXPECT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].file_name, L"中文文件.jpg");

    db.close();
}

TEST_F(ScanCacheDBTest, NonexistentSession) {
    ScanCacheDB db;
    db.open(test_path_.wstring());

    // Query non-existent session should return empty
    EXPECT_EQ(db.query_file_count("nonexistent"), 0u);
    auto files = db.query_files("nonexistent", 10, 0);
    EXPECT_TRUE(files.empty());

    ScanProgress progress;
    EXPECT_FALSE(db.load_progress("nonexistent", progress));

    auto sectors = db.load_bad_sectors("nonexistent");
    EXPECT_TRUE(sectors.empty());

    db.close();
}
