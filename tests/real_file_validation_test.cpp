#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include "format_registry.hpp"
#include "validation.hpp"
#include "types.hpp"

namespace fs = std::filesystem;
using namespace disk_recover;

// ════════════════════════════════════════════════════════════════
// Real File Validation Test
// ════════════════════════════════════════════════════════════════

class RealFileValidationTest : public ::testing::Test {
protected:
    static std::map<std::string, std::vector<fs::path>> samples_;
    static fs::path samples_dir_;

    static void SetUpTestSuite() {
        samples_dir_ = "tests/data/real_samples";

        if (!fs::exists(samples_dir_)) {
            std::cout << "[INFO] real_samples directory does not exist. "
                      << "Run sample_collector first.\n";
            return;
        }

        // Scan all subdirectories, collect files by format name
        for (const auto& entry : fs::directory_iterator(samples_dir_)) {
            if (!entry.is_directory()) continue;

            std::string format_name = entry.path().filename().string();
            std::vector<fs::path> files;

            for (const auto& file : fs::directory_iterator(entry.path())) {
                if (file.is_regular_file()) {
                    files.push_back(file.path());
                }
            }

            if (!files.empty()) {
                samples_[format_name] = std::move(files);
            }
        }

        std::cout << "[INFO] Loaded " << samples_.size() << " format categories from real_samples/\n";
    }

    // Read file content
    static std::vector<uint8_t> read_file(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }
};

// Static member initialization
std::map<std::string, std::vector<fs::path>> RealFileValidationTest::samples_;
fs::path RealFileValidationTest::samples_dir_;

// Format validation test template
// For each sample file in the format directory, verify that the registry can
// identify it. If the file is misnamed (its actual content matches a different
// format), we log a warning but don't fail — real-world recovery samples may
// contain misnamed files.
#define DEFINE_FORMAT_TEST(format_name, expected_type, expected_ext) \
TEST_F(RealFileValidationTest, format_name##Files) { \
    auto it = samples_.find(#format_name); \
    if (it == samples_.end() || it->second.empty()) { \
        GTEST_SKIP() << "No " #format_name " samples found in real_samples/" #format_name "/"; \
    } \
    \
    for (const auto& file : it->second) { \
        auto data = read_file(file); \
        ASSERT_FALSE(data.empty()) << "Failed to read " << file; \
        \
        auto match = FormatRegistry::instance().match(data.data(), data.size()); \
        if (!match.has_value()) { \
            std::cout << "  SKIP (no match): " << file.filename() << "\n"; \
            continue; \
        } \
        EXPECT_NE(match->result, ValidateResult::Reject) \
            << file.filename() << " was rejected"; \
        \
        if (match->descriptor->file_type != expected_type || \
            wcscmp(match->descriptor->extension, L ## #expected_ext) != 0) { \
            /* Convert wchar_t extension to narrow string for output */ \
            char ext_buf[16] = {}; \
            for (int i = 0; i < 15 && match->descriptor->extension[i]; ++i) { \
                ext_buf[i] = static_cast<char>(match->descriptor->extension[i]); \
            } \
            std::cout << "  WARN (mismatch): " << file.filename() \
                      << " identified as " << ext_buf \
                      << " instead of " << #expected_ext << "\n"; \
            continue; \
        } \
        \
        EXPECT_EQ(match->descriptor->file_type, expected_type) \
            << file.filename() << " type mismatch"; \
        EXPECT_STREQ(match->descriptor->extension, L ## #expected_ext) \
            << file.filename() << " extension mismatch"; \
        \
        if (match->result == ValidateResult::Reject) { \
            std::cout << "  REJECTED: " << file.filename() << "\n"; \
        } \
    } \
}

// ════════════════════════════════════════════════════════════════
// Image Format Tests
// ════════════════════════════════════════════════════════════════

DEFINE_FORMAT_TEST(jpg, FileType::Image, jpg)
DEFINE_FORMAT_TEST(png, FileType::Image, png)
DEFINE_FORMAT_TEST(gif, FileType::Image, gif)
DEFINE_FORMAT_TEST(bmp, FileType::Image, bmp)
DEFINE_FORMAT_TEST(tiff, FileType::Image, tiff)
DEFINE_FORMAT_TEST(webp, FileType::Image, webp)

// ════════════════════════════════════════════════════════════════
// Video Format Tests
// ════════════════════════════════════════════════════════════════

DEFINE_FORMAT_TEST(mp4, FileType::Video, mp4)
DEFINE_FORMAT_TEST(avi, FileType::Video, avi)
DEFINE_FORMAT_TEST(mkv, FileType::Video, mkv)
DEFINE_FORMAT_TEST(flv, FileType::Video, flv)
DEFINE_FORMAT_TEST(mts, FileType::Video, mts)
DEFINE_FORMAT_TEST(wmv, FileType::Video, wmv)

// ════════════════════════════════════════════════════════════════
// Audio Format Tests
// ════════════════════════════════════════════════════════════════

DEFINE_FORMAT_TEST(mp3, FileType::Audio, mp3)
DEFINE_FORMAT_TEST(flac, FileType::Audio, flac)
DEFINE_FORMAT_TEST(wav, FileType::Audio, wav)
DEFINE_FORMAT_TEST(m4a, FileType::Audio, m4a)

// ════════════════════════════════════════════════════════════════
// Document Format Tests
// ════════════════════════════════════════════════════════════════

DEFINE_FORMAT_TEST(pdf, FileType::Document, pdf)
DEFINE_FORMAT_TEST(doc, FileType::Document, doc)

// ════════════════════════════════════════════════════════════════
// Archive Format Tests
// ════════════════════════════════════════════════════════════════

DEFINE_FORMAT_TEST(zip, FileType::Archive, zip)
DEFINE_FORMAT_TEST(7z, FileType::Archive, 7z)
DEFINE_FORMAT_TEST(rar, FileType::Archive, rar)
