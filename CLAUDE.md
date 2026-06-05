# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 提供代码库工作指南。

## 限制

**制定计划时必须使用中文输出到文件。** 在进行任何需要规划的实现任务时，请将计划以中文形式写入文件。

## 构建命令

```bash
# 配置 (在项目根目录运行)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake

# 构建
cmake --build build --config Release

# 运行测试
./build/tests/Release/disk_recover_tests.exe

# 运行单个测试 (GoogleTest 过滤)
./build/tests/Release/disk_recover_tests.exe --gtest_filter=SignatureScannerTest*
```

## 架构概览

这是一个用 C++20 编写的 Windows 磁盘数据恢复工具。它通过文件系统元数据和/或 RAW 签名扫描从损坏或格式化的磁盘中恢复文件。

### 数据流
```
PhysicalDisk -> SectorReader -> FileSystemParser/SignatureScanner -> RecoverableFile -> ScanCacheDB -> RecoveryManager -> Output
```

### 核心模块

**disk-io/**: 底层磁盘访问
- `DiskHandle`: Win32 `CreateFile` 封装，提供磁盘句柄管理
- `SectorReader`: 扇区读取器，支持错误处理和坏扇区管理
- `BufferedSectorReader`: 大缓冲区预读取读取器（默认 16MB），减少系统调用
- `AlignedBuffer`: 内存对齐缓冲区，确保 DMA 对齐
- `BadSectorManager`: 坏扇区跟踪管理器，支持可配置策略（跳过/重试/强制读取）

**filesystem/**: 三种并行解析器实现:
- `ntfs/`: NTFS 文件系统，MFT 记录解析与 data run 提取
- `fat/`: FAT12/16/32 文件系统，FAT 链遍历
- `exfat/`: exFAT 文件系统，簇链遍历
- `raw/`: 基于签名的文件雕刻，支持置信度评分

**business/**: 业务编排层
- `ScanManager`: 扫描协调器，通过 `ScanProgress` 状态处理暂停/恢复
- `ScanAndRecoverManager`: 扫描并立即恢复的一体化管理器，支持实时恢复
- `RecoveryManager`: 恢复文件写入管理，支持多目标目录和扩展名分组
- `ScanCacheDB`: SQLite 持久化，存储扫描结果和进度
- `MultiTargetWriter`: 多目标写入器，支持目录切换和空间检查
- `PreviewManager`: 缩略图生成，使用 Windows Imaging Component 和 FFmpeg

### 文件签名验证系统

新的三阶段验证模型（灵感来自 PhotoRec）:

**ValidateResult 枚举** (`validation.hpp`):
- `Reject`: 确定不是此格式
- `AcceptHeader`: 魔数匹配，结构未验证
- `AcceptStructure`: 内部结构部分验证（如 JPEG 标记已解析）
- `AcceptContainer`: 容器已解析（atoms、IFDs、chunks 已遍历）
- `AcceptVerified`: 完整验证，包括大小边界/页脚已找到

**FormatDescriptor** (`format_descriptor.hpp`):
```cpp
struct FormatDescriptor {
    FileType        file_type;
    const wchar_t*  extension;       // 默认文件扩展名
    const wchar_t*  description;     // 人类可读描述
    uint64_t        min_filesize;    // 最小有效文件大小
    uint64_t        max_filesize;    // 最大有效文件大小
    SignaturePattern signature;      // 签名模式（首字节索引用）

    // 三阶段验证函数
    HeaderCheckFn   header_check;    // 阶段1：头部检查
    DataCheckFn     data_check;      // 阶段2：数据检查（渐进雕刻）
    FileCheckFn     file_check;      // 阶段3：文件检查（完整验证）
};
```

**FormatRegistry** (`format_registry.hpp`):
- 单例注册表，管理所有格式描述符
- 首字节索引加速签名匹配
- 验证器通过静态初始化器自动注册

**支持的文件格式验证器** (`raw/validators/`):
- 图片: BMP, JPEG, PNG, GIF, TIFF, WebP
- 视频: MP4/MOV (BMFF), AVI (RIFF), MKV/WebM (EBML), FLV, WMV, TS
- 音频: MP3, FLAC
- 文档: PDF, DOC/XLS/PPT (OLE2/ZIP)
- 压缩包: ZIP, 7Z, RAR

### 关键类型

**RecoverableFile** (`types.hpp`):
- 核心数据结构，包含片段（`DiskExtent` 列表）
- 文件类型、损坏级别、置信度
- 可选 MFT ID

**CorruptionLevel 损坏级别**:
- `None`: 文件完整（头部+页脚+高置信度）
- `Minor`: 缺少页脚或部分匹配，但头部已验证
- `Moderate`: 缺少头部验证或低置信度匹配
- `Severe`: 读取错误、合并失败或极低置信度

### 扫描模式

- `Quick`: 仅解析文件系统元数据（MFT、FAT 表）
- `Deep`: 元数据 + RAW 签名扫描
- `Full`: 全盘 RAW 签名扫描（无文件系统）

### 坏扇区处理策略

**BadSectorPolicy**:
- `Skip`: 跳过坏扇区继续扫描
- `Retry`: 重试读取
- `ForceRead`: 强制读取（可能返回损坏数据）

**SkipAheadConfig**:
- `consecutive_bad_threshold`: 连续坏批次阈值（默认 4）
- `skip_distance_sectors`: 跳过距离（默认 1024 扇区 = 512KB）
- 支持指数退避跳过策略

**ReadTimeoutConfig**:
- `timeout_ms`: 读取超时（默认 5000ms）
- `retry_count`: 重试次数（默认 3 次）

### 线程模型

- `ScanManager` 和 `RecoveryManager` 各自拥有工作线程
- 进度通过回调和原子标志通信
- UI 应使用 `take_found_files()` 进行批量更新以避免锁竞争
- 支持暂停/恢复操作，状态持久化到 SQLite

### 视频片段合并

签名扫描器支持视频片段自动合并:
- MP4/MOV: 4MB 间隔内合并
- AVI: 2MB 间隔内合并
- MKV/WebM: 2MB 间隔内合并
- 其他视频: 1MB 间隔内合并

### UI 模块

- `ui/cli/`: 命令行界面
- `ui/gui/`: Windows GUI（Win32 API），包含主窗口和保存目录对话框

## 代码风格

- 使用 C++20 特性
- 命名空间: `disk_recover`
- 文件命名: 蛇形命名法 (`xxx_yyy.hpp`)
- 类名: 大驼峰命名法 (`MyClass`)
- 函数/变量: 蛇形命名法 (`my_function`, `my_variable`)
- 成员变量: 尾部下划线 (`member_`)
- 常量: 全大写蛇形命名法 (`MY_CONSTANT`)
