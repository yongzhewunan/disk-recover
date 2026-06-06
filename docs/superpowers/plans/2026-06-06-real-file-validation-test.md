# 真实文件验证测试系统实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建扫描工具和测试用例，从本地磁盘收集真实文件样本并验证所有文件格式验证器。

**Architecture:** 扫描工具独立于测试运行，收集文件到 `tests/data/real_samples/`；测试用例读取这些文件并调用 `FormatRegistry::match()` 验证。

**Tech Stack:** C++20, std::filesystem, GoogleTest, nlohmann/json (可选，用于报告)

---

## 文件结构

| 文件 | 操作 | 描述 |
|------|------|------|
| `tools/CMakeLists.txt` | 创建 | 工具目录构建配置 |
| `tools/sample_collector.cpp` | 创建 | 文件扫描收集工具 |
| `CMakeLists.txt` | 修改 | 添加 tools 子目录 |
| `tests/real_file_validation_test.cpp` | 创建 | 真实文件验证测试 |
| `tests/CMakeLists.txt` | 修改 | 添加新测试文件 |
| `tests/data/real_samples/` | 创建目录 | 存放收集的样本文件 |

---

## Task 1: 创建 tools 目录和 CMakeLists.txt

**Files:**
- Create: `tools/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 创建 tools/CMakeLists.txt**

```cmake
# Tools for disk-recover project

add_executable(sample_collector sample_collector.cpp)
target_link_libraries(sample_collector PRIVATE
    disk_recover_filesystem_raw
    disk_recover_common
)
```

- [ ] **Step 2: 修改根 CMakeLists.txt 添加 tools 子目录**

在 `add_subdirectory(tests)` 后添加：

```cmake
add_subdirectory(tools)
```

- [ ] **Step 3: 验证构建配置**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 构建失败（sample_collector.cpp 不存在）

---

## Task 2: 实现扫描工具核心框架

**Files:**
- Create: `tools/sample_collector.cpp`

- [ ] **Step 1: 创建 sample_collector.cpp 基础框架**

```cpp
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "format_registry.hpp"

using namespace disk_recover;
namespace fs = std::filesystem;

// Configuration
struct Config {
    std::vector<std::string> drives = {"C", "D"};
    fs::path output_dir = "tests/data/real_samples";
    uint64_t max_file_size_mb = 100;
    size_t samples_per_format = 5;
    bool dry_run = false;
    bool verbose = false;
    std::set<std::string> specific_formats;
};

// 格式扩展名映射（从 FormatRegistry 获取）
struct FormatInfo {
    std::string extension;      // 小写扩展名
    FileType file_type;
    std::string description;
    std::vector<fs::path> found_files;
};

// 全局状态
std::map<std::string, FormatInfo> g_formats;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --drives <C,D>      Drives to scan (default: C,D)\n"
              << "  --output <dir>      Output directory (default: tests/data/real_samples)\n"
              << "  --max-size <MB>     Max file size in MB (default: 100)\n"
              << "  --samples <N>       Samples per format (default: 5)\n"
              << "  --formats <list>    Specific formats (default: all)\n"
              << "  --dry-run           List files only, don't copy\n"
              << "  --verbose           Verbose output\n"
              << "  --help              Show this help\n";
}

bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--drives" && i + 1 < argc) {
            std::string drives = argv[++i];
            cfg.drives.clear();
            size_t pos = 0;
            while ((pos = drives.find(',')) != std::string::npos) {
                cfg.drives.push_back(drives.substr(0, pos));
                drives.erase(0, pos + 1);
            }
            if (!drives.empty()) cfg.drives.push_back(drives);
        } else if (arg == "--output" && i + 1 < argc) {
            cfg.output_dir = argv[++i];
        } else if (arg == "--max-size" && i + 1 < argc) {
            cfg.max_file_size_mb = std::stoull(argv[++i]);
        } else if (arg == "--samples" && i + 1 < argc) {
            cfg.samples_per_format = std::stoul(argv[++i]);
        } else if (arg == "--formats" && i + 1 < argc) {
            std::string formats = argv[++i];
            cfg.specific_formats.clear();
            size_t pos = 0;
            while ((pos = formats.find(',')) != std::string::npos) {
                cfg.specific_formats.insert(formats.substr(0, pos));
                formats.erase(0, pos + 1);
            }
            if (!formats.empty()) cfg.specific_formats.insert(formats);
        } else if (arg == "--dry-run") {
            cfg.dry_run = true;
        } else if (arg == "--verbose") {
            cfg.verbose = true;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        return 1;
    }

    std::cout << "Sample Collector for Disk Recover\n";
    std::cout << "==================================\n";
    std::cout << "Drives: ";
    for (const auto& d : cfg.drives) std::cout << d << ": ";
    std::cout << "\n";
    std::cout << "Output: " << cfg.output_dir << "\n";
    std::cout << "Max size: " << cfg.max_file_size_mb << " MB\n";
    std::cout << "Samples per format: " << cfg.samples_per_format << "\n";

    // TODO: 初始化格式列表
    // TODO: 扫描驱动器
    // TODO: 复制文件
    // TODO: 生成报告

    std::cout << "\nDone.\n";
    return 0;
}
```

- [ ] **Step 2: 构建验证框架可编译**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 编译成功

---

## Task 3: 实现格式列表初始化

**Files:**
- Modify: `tools/sample_collector.cpp`

- [ ] **Step 1: 添加初始化格式列表函数**

在 `main()` 之前添加：

```cpp
// 需要扫描的格式列表（扩展名 -> 格式信息）
void init_format_list(const Config& cfg) {
    // 图片格式
    g_formats["jpg"] = {"jpg", FileType::Image, "JPEG Image"};
    g_formats["jpeg"] = {"jpg", FileType::Image, "JPEG Image"};  // jpeg 扩展名也归入 jpg
    g_formats["png"] = {"png", FileType::Image, "PNG Image"};
    g_formats["gif"] = {"gif", FileType::Image, "GIF Image"};
    g_formats["bmp"] = {"bmp", FileType::Image, "BMP Image"};
    g_formats["tiff"] = {"tiff", FileType::Image, "TIFF Image"};
    g_formats["tif"] = {"tiff", FileType::Image, "TIFF Image"};
    g_formats["webp"] = {"webp", FileType::Image, "WebP Image"};
    g_formats["heic"] = {"heic", FileType::Image, "HEIC Image"};
    g_formats["heif"] = {"heic", FileType::Image, "HEIF Image"};
    g_formats["orf"] = {"orf", FileType::Image, "Olympus RAW"};

    // 视频格式
    g_formats["mp4"] = {"mp4", FileType::Video, "MP4 Video"};
    g_formats["mov"] = {"mp4", FileType::Video, "QuickTime Movie"};
    g_formats["avi"] = {"avi", FileType::Video, "AVI Video"};
    g_formats["mkv"] = {"mkv", FileType::Video, "Matroska Video"};
    g_formats["webm"] = {"mkv", FileType::Video, "WebM Video"};
    g_formats["flv"] = {"flv", FileType::Video, "Flash Video"};
    g_formats["mts"] = {"mts", FileType::Video, "MPEG Transport Stream"};
    g_formats["m2ts"] = {"mts", FileType::Video, "MPEG Transport Stream"};
    g_formats["wmv"] = {"wmv", FileType::Video, "Windows Media Video"};

    // 音频格式
    g_formats["mp3"] = {"mp3", FileType::Audio, "MP3 Audio"};
    g_formats["flac"] = {"flac", FileType::Audio, "FLAC Audio"};
    g_formats["wav"] = {"wav", FileType::Audio, "WAV Audio"};
    g_formats["m4a"] = {"m4a", FileType::Audio, "M4A Audio"};

    // 文档格式
    g_formats["pdf"] = {"pdf", FileType::Document, "PDF Document"};
    g_formats["doc"] = {"doc", FileType::Document, "Word Document"};
    g_formats["xls"] = {"doc", FileType::Document, "Excel Document"};
    g_formats["ppt"] = {"doc", FileType::Document, "PowerPoint Document"};

    // 压缩格式
    g_formats["zip"] = {"zip", FileType::Archive, "ZIP Archive"};
    g_formats["7z"] = {"7z", FileType::Archive, "7-Zip Archive"};
    g_formats["rar"] = {"rar", FileType::Archive, "RAR Archive"};

    // 如果指定了特定格式，过滤列表
    if (!cfg.specific_formats.empty()) {
        std::map<std::string, FormatInfo> filtered;
        for (const auto& [ext, info] : g_formats) {
            if (cfg.specific_formats.count(ext) || cfg.specific_formats.count(info.extension)) {
                filtered[ext] = info;
            }
        }
        g_formats = std::move(filtered);
    }
}
```

- [ ] **Step 2: 在 main() 中调用初始化**

在 `main()` 的 "TODO: 初始化格式列表" 处替换为：

```cpp
    // 初始化格式列表
    init_format_list(cfg);
    std::cout << "Tracking " << g_formats.size() << " format extensions\n";
```

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 编译成功

---

## Task 4: 实现目录扫描逻辑

**Files:**
- Modify: `tools/sample_collector.cpp`

- [ ] **Step 1: 添加排除目录列表**

在 `Config` 结构体后添加：

```cpp
// 需要排除的目录
const std::set<std::string> EXCLUDED_DIRS = {
    "Windows",
    "Program Files",
    "Program Files (x86)",
    "ProgramData",
    "$Recycle.Bin",
    "System Volume Information",
    "$RECYCLE.BIN",
    "Recovery",
    "boot",
    "efi",
    ".git",
    "node_modules",
    ".cache",
    "Thumbs.db",
    "pagefile.sys",
    "hiberfil.sys",
    "swapfile.sys"
};
```

- [ ] **Step 2: 添加扫描函数**

在 `init_format_list` 之后添加：

```cpp
bool should_skip_dir(const fs::path& path) {
    std::string name = path.filename().string();
    // 跳过隐藏目录（以 . 开头）
    if (!name.empty() && name[0] == '.') return true;
    // 跳过排除列表中的目录
    return EXCLUDED_DIRS.count(name) > 0;
}

bool should_skip_entry(const fs::directory_entry& entry) {
    // 跳过符号链接
    if (entry.is_symlink()) return true;
    // 跳过临时文件
    std::string name = entry.path().filename().string();
    if (name.find('~') != std::string::npos) return true;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".tmp") return true;
    return false;
}

void scan_directory(const fs::path& root, const Config& cfg, size_t& total_scanned) {
    if (!fs::exists(root)) {
        if (cfg.verbose) std::cout << "  Skipping non-existent: " << root << "\n";
        return;
    }

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         ++it) {
        try {
            const auto& entry = *it;

            if (should_skip_entry(entry)) {
                continue;
            }

            if (entry.is_directory()) {
                if (should_skip_dir(entry.path())) {
                    if (cfg.verbose) std::cout << "  Skipping dir: " << entry.path() << "\n";
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!entry.is_regular_file()) continue;

            total_scanned++;

            // 检查扩展名
            std::string ext = entry.path().extension().string();
            if (ext.empty()) continue;

            // 去掉点号并转小写
            ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            // 查找格式
            auto fit = g_formats.find(ext);
            if (fit == g_formats.end()) continue;

            // 检查文件大小
            uint64_t file_size = entry.file_size();
            if (file_size > cfg.max_file_size_mb * 1024 * 1024) continue;
            if (file_size == 0) continue;

            // 获取目标扩展名（处理 jpg/jpeg 等别名）
            std::string target_ext = fit->second.extension;
            auto& files = g_formats[target_ext].found_files;

            // 检查是否已收集足够样本
            if (files.size() >= cfg.samples_per_format) continue;

            // 避免重复文件（按路径去重）
            if (std::find(files.begin(), files.end(), entry.path()) != files.end()) continue;

            files.push_back(entry.path());
            if (cfg.verbose) {
                std::cout << "  Found [" << target_ext << "]: " << entry.path()
                          << " (" << file_size / 1024 << " KB)\n";
            }

        } catch (const std::exception& e) {
            if (cfg.verbose) {
                std::cout << "  Error: " << e.what() << "\n";
            }
        }
    }
}

void scan_drives(const Config& cfg) {
    size_t total_scanned = 0;

    for (const auto& drive : cfg.drives) {
        fs::path root(drive + ":\\");
        std::cout << "\nScanning " << root << "...\n";
        scan_directory(root, cfg, total_scanned);
    }

    std::cout << "\nTotal files scanned: " << total_scanned << "\n";
}
```

- [ ] **Step 3: 在 main() 中调用扫描**

替换 "TODO: 扫描驱动器" 为：

```cpp
    // 扫描驱动器
    scan_drives(cfg);
```

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 编译成功

---

## Task 5: 实现文件复制逻辑

**Files:**
- Modify: `tools/sample_collector.cpp`

- [ ] **Step 1: 添加复制函数**

在 `scan_drives` 之后添加：

```cpp
void copy_files(const Config& cfg) {
    if (cfg.dry_run) {
        std::cout << "\n[DRY RUN] Would copy:\n";
    } else {
        // 创建输出目录
        std::error_code ec;
        fs::create_directories(cfg.output_dir, ec);

        if (ec) {
            std::cerr << "Error creating output directory: " << ec.message() << "\n";
            return;
        }
    }

    size_t total_copied = 0;
    size_t total_skipped = 0;

    for (const auto& [ext, info] : g_formats) {
        // 只处理目标扩展名（跳过别名如 jpeg->jpg）
        if (ext != info.extension) continue;

        if (info.found_files.empty()) {
            std::cout << "  [" << ext << "] No files found\n";
            continue;
        }

        // 创建格式子目录
        fs::path format_dir = cfg.output_dir / ext;
        if (!cfg.dry_run) {
            fs::create_directories(format_dir);
        }

        std::cout << "  [" << ext << "] Copying " << info.found_files.size() << " files...\n";

        for (const auto& src : info.found_files) {
            fs::path dst = format_dir / src.filename();

            // 处理文件名冲突
            if (fs::exists(dst)) {
                // 添加序号
                int counter = 1;
                std::string stem = src.stem().string();
                std::string extension = src.extension().string();
                while (fs::exists(dst)) {
                    dst = format_dir / (stem + "_" + std::to_string(counter) + extension);
                    counter++;
                }
            }

            if (cfg.dry_run) {
                std::cout << "    " << src << " -> " << dst << "\n";
            } else {
                std::error_code ec;
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::cout << "    Error copying " << src << ": " << ec.message() << "\n";
                    total_skipped++;
                } else {
                    if (cfg.verbose) {
                        std::cout << "    Copied: " << src.filename() << "\n";
                    }
                    total_copied++;
                }
            }
        }
    }

    std::cout << "\nCopied: " << total_copied << " files\n";
    if (total_skipped > 0) {
        std::cout << "Skipped (errors): " << total_skipped << " files\n";
    }
}
```

- [ ] **Step 2: 在 main() 中调用复制**

替换 "TODO: 复制文件" 为：

```cpp
    // 复制文件
    copy_files(cfg);
```

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 编译成功

---

## Task 6: 实现报告生成

**Files:**
- Modify: `tools/sample_collector.cpp`

- [ ] **Step 1: 添加报告生成函数**

在 `copy_files` 之后添加：

```cpp
void generate_report(const Config& cfg) {
    fs::path report_path = cfg.output_dir / "collection_report.txt";

    std::ofstream report(report_path);
    if (!report) {
        std::cerr << "Error: Could not create report file\n";
        return;
    }

    report << "Sample Collection Report\n";
    report << "========================\n\n";

    report << "Configuration:\n";
    report << "  Drives: ";
    for (const auto& d : cfg.drives) report << d << ": ";
    report << "\n";
    report << "  Max file size: " << cfg.max_file_size_mb << " MB\n";
    report << "  Samples per format: " << cfg.samples_per_format << "\n\n";

    report << "Results:\n";
    report << "--------\n\n";

    size_t formats_found = 0;
    size_t formats_missing = 0;

    for (const auto& [ext, info] : g_formats) {
        if (ext != info.extension) continue;  // 跳过别名

        report << "[" << ext << "] " << info.description << "\n";

        if (info.found_files.empty()) {
            report << "  Status: NO SAMPLES FOUND\n";
            formats_missing++;
        } else {
            report << "  Status: Found " << info.found_files.size() << " files\n";
            for (const auto& f : info.found_files) {
                report << "  - " << f.filename() << "\n";
            }
            formats_found++;
        }
        report << "\n";
    }

    report << "Summary:\n";
    report << "  Formats with samples: " << formats_found << "\n";
    report << "  Formats missing: " << formats_missing << "\n";

    report.close();
    std::cout << "\nReport saved to: " << report_path << "\n";
}
```

- [ ] **Step 2: 添加必要的头文件**

在文件顶部添加：

```cpp
#include <fstream>
```

- [ ] **Step 3: 在 main() 中调用报告生成**

替换 "TODO: 生成报告" 为：

```cpp
    // 生成报告
    generate_report(cfg);
```

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --config Release --target sample_collector
```

Expected: 编译成功

- [ ] **Step 5: 测试运行**

```bash
./build/tools/Release/sample_collector.exe --dry-run --verbose
```

Expected: 显示将要扫描的驱动器和格式列表

---

## Task 7: 创建真实文件验证测试框架

**Files:**
- Create: `tests/real_file_validation_test.cpp`

- [ ] **Step 1: 创建测试文件基础框架**

```cpp
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

        // 扫描所有子目录，按格式名收集文件
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

    // 读取文件内容
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

// 静态成员初始化
std::map<std::string, std::vector<fs::path>> RealFileValidationTest::samples_;
fs::path RealFileValidationTest::samples_dir_;

// 格式验证测试模板
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
        ASSERT_TRUE(match.has_value()) << "No match for " << file.filename(); \
        EXPECT_NE(match->result, ValidateResult::Reject) \
            << file.filename() << " was rejected"; \
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
```

- [ ] **Step 2: 更新 tests/CMakeLists.txt 添加新测试文件**

在 `add_executable(disk_recover_tests` 中添加 `real_file_validation_test.cpp`：

```cmake
add_executable(disk_recover_tests
    aligned_buffer_test.cpp
    disk_handle_test.cpp
    bad_sector_test.cpp
    file_signatures_test.cpp
    signature_scanner_test.cpp
    format_registry_test.cpp
    ntfs_test.cpp
    fat_test.cpp
    exfat_test.cpp
    scan_cache_db_test.cpp
    recover_manager_test.cpp
    real_file_validation_test.cpp
)
```

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --config Release --target disk_recover_tests
```

Expected: 编译成功

---

## Task 8: 创建样本目录结构

**Files:**
- Create: `tests/data/real_samples/` 目录

- [ ] **Step 1: 创建 .gitkeep 文件**

创建目录并添加占位文件：

```bash
mkdir -p tests/data/real_samples
echo "# Real file samples directory" > tests/data/real_samples/.gitkeep
```

- [ ] **Step 2: 更新 .gitignore（可选）**

如果不想将样本文件提交到 git，在 `.gitignore` 中添加：

```
# Real file samples (optional - uncomment to exclude)
# tests/data/real_samples/*
# !tests/data/real_samples/.gitkeep
```

---

## Task 9: 构建并测试完整流程

**Files:**
- 无新文件

- [ ] **Step 1: 完整构建**

```bash
cmake --build build --config Release
```

Expected: 构建成功

- [ ] **Step 2: 运行扫描工具收集样本**

```bash
./build/tools/Release/sample_collector.exe --drives C,D --verbose
```

Expected: 扫描 C:/D:/ 盘并复制文件到 `tests/data/real_samples/`

- [ ] **Step 3: 运行验证测试**

```bash
./build/tests/Release/disk_recover_tests.exe --gtest_filter=RealFileValidation*
```

Expected: 测试运行，报告各格式验证结果

---

## Task 10: 提交代码

- [ ] **Step 1: 查看变更**

```bash
git status
```

- [ ] **Step 2: 添加所有新文件**

```bash
git add tools/ tests/real_file_validation_test.cpp tests/data/real_samples/.gitkeep CMakeLists.txt
```

- [ ] **Step 3: 提交**

```bash
git commit -m "feat: 添加真实文件验证测试系统

- 新增 sample_collector 工具，扫描本地磁盘收集文件样本
- 新增 real_file_validation_test 测试用例，验证所有格式验证器
- 支持 18+ 种格式：JPG/PNG/GIF/BMP/TIFF/WebP/MP4/AVI/MKV/FLV/MTS/WMV/MP3/FLAC/WAV/M4A/PDF/DOC/ZIP/7Z/RAR
"
```

---

## 验证清单

- [ ] `sample_collector.exe` 可以正常扫描并收集文件
- [ ] 收集的文件存放在 `tests/data/real_samples/{format}/` 目录
- [ ] 测试 `RealFileValidationTest` 可以运行
- [ ] 每种格式的测试用例正确验证文件
- [ ] 无样本的格式测试被正确跳过
- [ ] 报告文件 `collection_report.txt` 正确生成
