# 真实文件验证测试系统设计

## 概述

创建一个系统，从 C:/D:/ 盘扫描真实文件，复制到项目 `tests/data/real_samples/` 目录，用于验证所有文件格式验证器的正确性。

## 目录结构

```
tests/data/
├── real_samples/           # 新目录：真实文件样本
│   ├── jpg/                # JPEG 文件
│   │   ├── sample_001.jpg
│   │   ├── sample_002.jpg (Exif)
│   │   └── sample_003.jpg
│   ├── png/
│   ├── gif/
│   ├── bmp/
│   ├── tiff/
│   ├── webp/
│   ├── mp4/
│   ├── avi/
│   ├── mkv/
│   ├── mov/                # BMFF 视频变体
│   ├── flv/
│   ├── ts/
│   ├── wmv/
│   ├── mp3/
│   ├── flac/
│   ├── wav/
│   ├── pdf/
│   ├── zip/
│   ├── 7z/
│   ├── rar/
│   └── doc/                # OLE2 文档
└── (现有的测试文件保留)
```

每子目录存放 3-5 个该格式的真实文件。

## 扫描工具 (`tools/sample_collector.cpp`)

### 命令行接口

```bash
sample_collector.exe [选项]

选项：
  --drives C,D        要扫描的驱动器（默认：C,D）
  --output <dir>      输出目录（默认：tests/data/real_samples）
  --max-size <MB>     单文件最大大小（默认：100）
  --samples <N>       每格式样本数（默认：5）
  --formats <list>    指定格式的式（默认：全部）
  --dry-run           只列出找到的文件，不复制
  --verbose           详细输出
```

### 扫描策略

1. 使用 `std::filesystem::recursive_directory_iterator` 遍历
2. 排除系统目录：
   - `C:\Windows\`
   - `C:\Program Files\`
   - `C:\Program Files (x86)\`
   - `C:\$Recycle.Bin\`
   - `C:\System Volume Information\`
3. 按扩展名匹配格式（从 `FormatRegistry` 获取所有注册扩展名）
4. 记录每个扩展名找到的文件，保留前 N 个变体（按文件大小/路径哈希区分）
5. 复制文件到目标目录，保留原文件名（冲突时加序号）

### 输出

- 控制台实时输出扫描进度
- 最终生成 JSON 报告：`collection_report.json`，包含：
  - 每种格式找到的文件数
  - 实际复制的文件列表
  - 未找到的格式列表

## GoogleTest 测试用例 (`tests/real_file_validation_test.cpp`)

### 测试结构

```cpp
// 基础测试类，负责加载样本文件
class RealFileValidationTest : public ::testing::Test {
protected:
    static std::map<std::string, std::vector<std::filesystem::path>> samples_;

    static void SetUpTestSuite();  // 扫描 real_samples/ 目录，按格式加载文件路径
};

// 每种格式一个测试用例
TEST_F(RealFileValidationTest, JpegFiles) { /* ... */ }
TEST_F(RealFileValidationTest, PngFiles) { /* ... */ }
TEST_F(RealFileValidationTest, BmpFiles) { /* ... */ }
// ... 共 18+ 个测试
```

### 单格式测试逻辑

```cpp
TEST_F(RealFileValidationTest, JpegFiles) {
    auto& files = samples_["jpg"];
    if (files.empty()) {
        GTEST_SKIP() << "No JPEG samples found in real_samples/jpg/";
    }

    for (const auto& file : files) {
        auto data = read_file(file);
        auto match = FormatRegistry::instance().match(data.data(), data.size());

        ASSERT_TRUE(match.has_value()) << "No match for " << file.filename();
        EXPECT_NE(match->result, ValidateResult::Reject) << file.filename();
        EXPECT_EQ(match->descriptor->file_type, FileType::Image);
        EXPECT_STREQ(match->descriptor->extension, L"jpg");

        // 可选：验证等级（至少 AcceptHeader）
        EXPECT_GE(match->result, ValidateResult::AcceptHeader);
    }
}
```

### 测试输出

- 每个文件验证通过/失败
- 失败时显示具体错误（文件名、验证结果、期望值）
- 最终汇总：多少格式有样本、多少文件通过

## CMake 集成

### 新增构建目标

```cmake
# tools/CMakeLists.txt
add_executable(sample_collector sample_collector.cpp)
target_link_libraries(sample_collector PRIVATE disk_recover_filesystem_raw)

# tests/CMakeLists.txt
disk_recover_add_test(real_file_validation_test
    SOURCES real_file_validation_test.cpp
    LINK_LIBRARIES disk_recover_filesystem_raw
)
```

### 自定义目标（可选）

```cmake
# 一键收集 + 测试
add_custom_target(collect_and_test
    COMMAND sample_collector.exe --drives C,D --verbose
    COMMAND ${CMAKE_CTEST_COMMAND} -R RealFileValidation --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Collect samples and run validation tests"
)
```

## 错误处理

### 扫描工具

- 无法访问的目录（权限不足）：跳过并记录警告
- 符号链接/快捷方式：跳过，避免重复或无限循环
- 损坏文件：复制时不做验证，由测试阶段处理
- 目标磁盘空间不足：提前检查，给出警告

### 测试边界情况

- `real_samples/` 目录不存在或为空：测试跳过并输出提示
- 样本文件读取失败：标记为失败但继续测试其他文件
- 文件实际格式与扩展名不符：验证器应正确识别真实格式

## 运行流程

```bash
# 1. 构建项目
cmake --build build --config Release

# 2. 运行扫描工具收集样本
./build/tools/Release/sample_collector.exe --drives C,D --verbose

# 3. 运行验证测试
./build/tests/Release/disk_recover_tests.exe --gtest_filter=RealFileValidation*

# 4. 查看结果
# - 测试通过：所有验证器工作正常
# - 测试失败：检查具体文件，可能需要修复验证器
# - 跳过：该格式无样本，可能需要手动补充
```

## 文件清单

| 文件 | 操作 | 描述 |
|------|------|------|
| `tools/sample_collector.cpp` | 新增 | 扫描工具主程序 |
| `tools/CMakeLists.txt` | 更新 | 添加 sample_collector 目标 |
| `tests/real_file_validation_test.cpp` | 新增 | 真实文件验证测试 |
| `tests/CMakeLists.txt` | 更新 | 添加新测试 |
| `tests/data/real_samples/` | 新增目录 | 存放收集的文件样本 |

## 支持的格式

根据 `validator_linkage.cpp`，共注册 18+ 种格式：

- **图片**: JPG, PNG, GIF, BMP, TIFF (LE/BE), WebP
- **视频**: MP4/MOV (BMFF), AVI, MKV/WebM (EBML), FLV, TS, WMV
- **音频**: MP3, FLAC, WAV
- **文档**: PDF, DOC (OLE2)
- **压缩**: ZIP, 7Z, RAR
- **其他**: ORF (Olympus RAW)
