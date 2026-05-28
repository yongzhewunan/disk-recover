# Disk Recover - 磁盘数据恢复软件设计文档

**日期**: 2026-05-28
**状态**: 设计评审通过，已补充评审意见

---

## 1. 背景

开发一款运行于 Windows 平台的磁盘数据恢复软件，支持 HDD 硬盘的 NTFS、FAT、exFAT 分区格式，主要恢复图片和视频文件。目标用户为技术人员，需要提供 GUI 和 CLI 两种界面，支持多种恢复模式和文件预览功能。**需支持 WinPE 环境**。

---

## 2. 目标

- 支持四种恢复模式：删除文件恢复、格式化恢复、分区丢失恢复、RAW 扫描恢复
- 支持 NTFS、FAT、exFAT 三种文件系统
- 主要恢复图片和视频文件
- 提供图形界面（Win32）和命令行界面
- 提供文件预览功能（图片缩略图、视频片段、元数据）
- 以恢复率为优先目标，同时提供多种扫描策略供用户选择
- **支持 WinPE 环境运行**（轻量依赖、小体积）

---

## 3. 整体架构

采用分层单体架构，分为四层：

```
┌─────────────────────────────────────────────┐
│                 UI Layer                     │
│   ┌─────────────┐    ┌─────────────────┐    │
│   │  Win32 GUI  │    │   CLI Module    │    │
│   │  (Lightweight)   │   (Console)     │    │
│   └─────────────┘    └─────────────────┘    │
├─────────────────────────────────────────────┤
│              Business Layer                  │
│   ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│   │ Scan     │ │ Recover  │ │ Preview  │   │
│   │ Manager  │ │ Manager  │ │ Manager  │   │
│   └──────────┘ └──────────┘ └──────────┘   │
├─────────────────────────────────────────────┤
│            File System Layer                │
│   ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│   │  NTFS    │ │   FAT    │ │  exFAT   │   │
│   │ Parser   │ │  Parser  │ │ Parser   │   │
│   └──────────┘ └──────────┘ └──────────┘   │
│   ┌──────────────────────────────────────┐  │
│   │         RAW Signature Scanner         │  │
│   └──────────────────────────────────────┘  │
├─────────────────────────────────────────────┤
│              Disk I/O Layer                 │
│   ┌──────────────────────────────────────┐  │
│   │    Windows Disk API Wrapper          │  │
│   │    (Win32 API: CreateFile, ReadFile) │  │
│   └──────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

---

## 4. 核心模块设计

### 4.1 磁盘 I/O 层

**职责**: 封装 Windows 底层磁盘访问 API，确保无缓冲读取和内存对齐

**功能**:
- 通过 `CreateFile` 打开物理磁盘 (`\\.\PhysicalDriveX`) 或逻辑分区 (`\\.\X:`)
- **必须启用无缓冲标志**: `FILE_FLAG_NO_BUFFERING` + `FILE_FLAG_WRITE_THROUGH`，绕过系统页缓存
- 使用 `DeviceIoControl` 获取磁盘几何参数和分区信息
- 支持以管理员权限运行时的直接扇区读取
- 提供统一的扇区读取接口

**关键 API**:
- `CreateFile` - 打开磁盘设备（必须添加 `FILE_FLAG_NO_BUFFERING`）
- `ReadFile` - 读取扇区数据
- `DeviceIoControl` - 获取磁盘信息
- `SetFilePointer` - 定位读取位置

**内存对齐要求 (CRITICAL)**:
在开启无缓冲模式后，`ReadFile` 的缓冲区首地址、读取起始位置和读取长度都必须是磁盘物理扇区大小（512 或 4096 字节）的整数倍：
- 使用 `VirtualAlloc` 分配页对齐的内存缓冲区
- 或使用 C++17 的 `std::aligned_alloc`
- **禁止使用** `std::vector<char>` 或 `new char[]` 作为读取缓冲区

**坏道处理机制**:
- 扫描过程中自动记录坏道扇区位置
- 智能跳过坏道区域，避免长时间卡死
- 坏道信息持久化保存，下次扫描可复用
- 提供坏道重试策略配置（快速跳过 / 多次重试 / 强制读取）

### 4.2 文件系统解析层

**职责**: 解析不同文件系统结构，提取文件元数据

#### 4.2.0 核心数据结构

所有文件系统解析器必须输出统一的结构，以支持碎片化文件恢复：

```cpp
// 磁盘物理区间（扇区级别）
struct DiskExtent {
    uint64_t start_sector;  // 起始扇区号
    uint64_t sector_count;  // 扇区数量
};

// 可恢复文件描述
struct RecoverableFile {
    std::wstring file_name;          // 文件名
    uint64_t file_size;              // 文件大小（字节）
    std::vector<DiskExtent> fragments; // 碎片物理分布列表（核心！）
    bool is_corrupted;               // 是否损坏（头部/尾部不完整）
    FileType file_type;             // 图片/视频
    std::optional<uint64_t> mft_id;  // NTFS MFT 记录号（可选）
};
```

#### 4.2.1 NTFS Parser
- 解析 $MFT（Master File Table）
- 恢复删除的 MFT 记录（检查已删除标记）
- 处理属性列表（$STANDARD_INFORMATION, $FILE_NAME, $DATA）
- 支持 MFT 碎片整理场景

#### 4.2.2 FAT Parser
- 解析 FAT12/FAT16/FAT32 表
- 解析目录项（短文件名 + 长文件名）
- 恢复删除的目录项（检查删除标记 0xE5）
- 处理文件碎片

#### 4.2.3 exFAT Parser
- 解析分配位图
- 解析大写转换表
- 解析目录项（文件项、流扩展项、文件名项）
- 恢复删除的文件

#### 4.2.4 RAW Signature Scanner
- 基于文件签名扫描（不依赖文件系统）
- 支持文件头签名和文件尾签名匹配
- 支持文件大小推断（基于文件格式特征）
- **智能碎片重组**: 可选功能，识别 MP4/MOV 等容器格式的视频碎片，尝试智能重组

### 4.3 业务逻辑层

**职责**: 协调扫描、恢复、预览流程

#### 4.3.1 Scan Manager
- 管理扫描任务生命周期
- 支持三种扫描模式：
  - **快速扫描**: 仅扫描文件系统元数据
  - **深度扫描**: 快速扫描 + 空闲空间 RAW 签名扫描
  - **完整扫描**: 全盘扇区扫描
- **扫描状态持久化**:
  - 扫描进度实时保存到 SQLite 数据库
  - 支持暂停/继续，断电重启后可恢复
  - 坏道信息独立存储，可复用于后续扫描
- 实时进度报告

#### 4.3.2 Scan Cache Database (新增)
**目的**: 解决大规模扫描（4TB+ HDD，数百万文件）的内存压力

**设计**:
- 扫描线程（生产者）每发现 1000 个文件批量写入 SQLite
- UI 层通过 `LIMIT`/`OFFSET` 实现虚拟化分页加载
- 数据库结构：
  ```sql
  CREATE TABLE scan_result (
      id INTEGER PRIMARY KEY,
      file_name TEXT,
      file_size INTEGER,
      file_type INTEGER,
      fragments BLOB,        -- 序列化的 DiskExtent 列表
      is_corrupted INTEGER,
      scan_session_id TEXT
  );
  CREATE INDEX idx_session ON scan_result(scan_session_id);
  ```

#### 4.3.3 Recover Manager
- 从扫描结果中选择文件进行恢复
- 支持批量恢复和选择性恢复
- **多目标空间支持**:
  - 支持配置多个恢复目标路径
  - 当前目标空间不足时自动切换到下一个目标
  - 实时监控各目标磁盘剩余空间
- 恢复进度报告
- 恢复完成报告（成功/失败统计）

#### 4.3.4 Preview Manager
- 图片缩略图生成
- 视频片段预览（提取关键帧）
- 文件元数据显示

### 4.4 UI 层

#### 4.4.1 Win32 GUI（轻量级，支持 WinPE）
**设计目标**: 体积小（500KB-1MB）、依赖少、可在任何 Windows 环境运行

**技术选型**:
- 纯 Win32 API + Common Controls（ComCtl32）
- 或使用 WTL（Windows Template Library）简化开发
- 静态链接，无需额外运行时

**界面功能**:
- 磁盘/分区选择界面
- 扫描配置界面（模式选择、文件类型过滤、坏道策略）
- 扫描进度界面（进度条、坏道统计、已发现文件数）
- 可恢复文件列表（树形视图，虚拟化分页）
- 文件预览面板
- 多目标恢复路径配置界面
- 恢复进度界面
- 坏道信息面板

**Win32 GUI 优势**:
- 几乎零依赖，WinPE 原生支持
- 体积极小（< 1MB）
- 启动快，内存占用低
- 可完全静态链接

#### 4.4.2 CLI Module
```
disk-recover.exe <command> [options]

Commands:
  list-disks              列出所有可用磁盘和分区
  scan <disk>             扫描指定磁盘/分区
    --mode <quick|deep|full>    扫描模式
    --types <images,videos>     文件类型过滤
    --output <file>             保存扫描结果
    --resume <session>          继续上次扫描会话
    --bad-sector <skip|retry|force>  坏道处理策略
  recover <result>        从扫描结果恢复文件
    --output <dir>              恢复目标目录（可指定多个，逗号分隔）
    --filter <pattern>          文件名过滤
    --auto-switch               空间不足自动切换目标
  preview <file>          预览扫描结果中的文件
  bad-sectors <session>   显示指定扫描会话的坏道信息
```

#### 4.4.3 GUI 补充界面
- 扫描进度保存提示（支持断点续扫）
- 坏道统计显示（已跳过扇区数、重试次数）
- 多目标恢复路径配置界面
- 恢复空间监控面板（各目标磁盘剩余空间）
- 智能碎片重组开关（扫描配置）

---

## 5. 支持的文件格式

### 5.1 图片格式
- JPEG (JPG, JPEG)
- PNG
- BMP
- GIF
- TIFF (TIF, TIFF)
- WEBP
- RAW (CR2, NEF, ARW, DNG, ORF, RW2)
- HEIC

### 5.2 视频格式
- MP4
- AVI
- MOV
- MKV
- WMV
- FLV
- WEBM
- MTS, M2TS

---

## 6. 扫描与恢复流程

### 6.1 扫描流程
1. 用户选择目标磁盘/分区
2. 用户选择扫描模式（快速/深度/完整）
3. 用户选择要恢复的文件类型（图片/视频）
4. 用户选择坏道处理策略（快速跳过/多次重试/强制读取）
5. 系统执行扫描，实时显示进度和已发现文件
6. 扫描过程中自动记录坏道信息
7. 扫描进度实时保存，支持暂停/继续
8. 扫描完成后展示可恢复文件列表
9. 扫描结果可保存到文件（SQLite 数据库）

### 6.2 恢复流程
1. 用户从列表中选择要恢复的文件/文件夹
2. 用户配置多个恢复目标路径
3. 系统检测各目标磁盘剩余空间
4. 用户确认恢复配置
5. 系统执行文件恢复，显示进度
6. 当前目标空间不足时自动切换到下一个目标
7. 恢复完成，生成恢复报告（成功/失败文件统计、各目标写入量）

---

## 7. 技术选型

| 组件 | 技术选型 | 说明 |
|------|----------|------|
| 编程语言 | C++ 20 | 性能最优，与 Windows 集成度高 |
| GUI 框架 | Win32 API / WTL | 轻量级，零依赖，WinPE 兼容，体积极小 |
| CLI 框架 | CLI11 | 现代 C++ 命令行解析库 |
| 构建系统 | CMake + vcpkg | 跨平台构建，依赖管理 |
| 图片解码 | WIC (Windows Imaging Component) | 原生 Windows 图片处理，WinPE 内置 |
| 视频解码 | FFmpeg (libavcodec/libavformat) | 强容错机制，可解码损坏视频片段 |
| 持久化存储 | SQLite | 扫描结果缓存，支持百万级文件 |
| 单元测试 | Google Test | C++ 单元测试框架 |

**GUI 框架选型说明**:
选择 Win32/WTL 而非 WinUI 3 的原因：
- **WinPE 兼容性**: WinPE 缺少 .NET 6+ 和 Windows App SDK 运行时
- **体积优势**: Win32 GUI 体积仅 500KB-1MB，WinUI 3 应用体积 50MB+
- **零依赖**: 纯 Win32 API 调用，无需额外安装运行时
- **静态链接**: 可完全静态链接，单一 exe 即可运行
- WTL 提供现代 C++ 封装，开发效率接近 MFC，但无 MFC 的运行时依赖

**视频解码选型说明**:
选择 FFmpeg 而非 Windows Media Foundation 的原因：
- 数据恢复场景下，视频文件往往是损坏、残缺的
- Windows Media Foundation 对损坏视频兼容性差，常抛异常或拒绝解析
- FFmpeg 拥有极强的容错机制，即使视频缺少尾部索引（如 moov atom），仍能解码出前几帧画面
- 这对于"让用户确认视频是否可恢复"至关重要

---

## 8. 项目结构

```
disk-recover/
├── src/
│   ├── disk-io/           # 磁盘 I/O 层
│   │   ├── disk_handle.cpp      # 磁盘句柄管理
│   │   ├── disk_info.cpp        # 磁盘信息查询
│   │   ├── sector_reader.cpp    # 扇区读取（内存对齐）
│   │   ├── aligned_buffer.cpp   # 对齐内存分配器
│   │   └── bad_sector_manager.cpp # 坏道检测与管理
│   ├── filesystem/        # 文件系统解析层
│   │   ├── ntfs/
│   │   │   ├── mft_parser.cpp
│   │   │   ├── attribute.cpp
│   │   │   └── record.cpp
│   │   ├── fat/
│   │   │   ├── fat_parser.cpp
│   │   │   └── directory_entry.cpp
│   │   ├── exfat/
│   │   │   └── exfat_parser.cpp
│   │   └── raw/
│   │       ├── signature_scanner.cpp
│   │       ├── file_signatures.cpp
│   │       └── fragment_reassembler.cpp # 智能碎片重组
│   ├── business/          # 业务逻辑层
│   │   ├── scan_manager.cpp
│   │   ├── scan_cache_db.cpp    # SQLite 持久化
│   │   ├── recover_manager.cpp
│   │   ├── multi_target_writer.cpp # 多目标空间写入器
│   │   └── preview_manager.cpp
│   ├── ui/
│   │   ├── gui/           # Win32/WTL GUI（轻量级）
│   │   │   ├── main_window.cpp
│   │   │   ├── scan_view.cpp
│   │   │   ├── recover_view.cpp
│   │   │   ├── bad_sector_panel.cpp
│   │   │   ├── preview_panel.cpp
│   │   │   └── resource.rc        # 资源文件
│   │   └── cli/           # CLI
│   │       └── main.cpp
│   └── common/
│       ├── types.hpp            # 核心数据结构（DiskExtent, RecoverableFile）
│       └── utils.cpp
├── tests/
│   ├── ntfs_test.cpp
│   ├── fat_test.cpp
│   ├── raw_test.cpp
│   └── aligned_buffer_test.cpp
├── docs/
│   └── specs/
├── winpe/                       # WinPE 部署包
│   ├── README.md                # WinPE 使用说明
│   └── build_pe_package.bat     # 打包脚本
├── CMakeLists.txt
├── vcpkg.json
└── README.md
```

---

## 11. WinPE 支持方案

### 11.1 WinPE 环境特点
- 精简的 Windows 环境，缺少现代应用框架
- 无 .NET 运行时、无 Windows App SDK
- 仅包含基础 Win32 API 和少量内置组件
- WIC（Windows Imaging Component）内置可用

### 11.2 WinPE 兼容性设计
- **GUI**: Win32 API + WTL，零依赖，静态链接
- **视频解码**: FFmpeg 需打包 DLL（约 15-20MB）
- **SQLite**: 静态链接 amalgamation 版本
- **CLI11**: Header-only，无额外依赖

### 11.3 WinPE 部署包结构
```
winpe_package/
├── disk-recover.exe      # 主程序（约 500KB）
├── ffmpeg.dll            # FFmpeg 解码库（约 15MB）
├── avcodec-60.dll
├── avformat-60.dll
├── avutil-58.dll
├── swscale-7.dll
└── README.txt            # 使用说明
```

总体积约 20-25MB，可直接在 WinPE 中运行。

---

## 9. 开发阶段规划

### 阶段一：基础框架（预计 2 周）
- 项目结构搭建
- **vcpkg.json 依赖架构设计**（SQLite, FFmpeg, CLI11, GoogleTest）
- 磁盘 I/O 层实现（含内存对齐缓冲区）
- 坏道检测与管理模块
- 基础 CLI 框架

### 阶段二：文件系统解析（预计 4 周）
- NTFS 解析器实现
- FAT/exFAT 解析器实现
- RAW 签名扫描器实现
- 智能碎片重组模块

### 阶段三：业务逻辑与持久化（预计 3 周）
- 扫描管理器实现
- **Scan Cache Database 实现**（SQLite 持久化）
- 多目标恢复管理器实现
- 扫描会话恢复功能

### 阶段四：用户界面（预计 3 周）
- CLI 完整实现
- Win32/WTL GUI 实现（含坏道面板、多目标配置、预览面板）
- 分页虚拟化列表视图
- WinPE 部署包打包脚本

### 阶段五：预览与优化（预计 2 周）
- 文件预览功能实现（FFmpeg 集成）
- 性能优化（内存管理、IO 批量处理）
- 测试与修复

---

## 10. 验证方案

### 10.1 单元测试
- 使用 Google Test 对各模块进行单元测试
- 测试文件系统解析正确性
- 测试 RAW 签名匹配准确性

### 10.2 集成测试
- 使用测试镜像文件进行恢复测试
- 测试各种恢复场景：
  - 正常删除文件恢复
  - 格式化分区恢复
  - 分区表损坏恢复
  - 文件系统损坏 RAW 恢复

### 10.3 手动验证
- 在真实 HDD 上测试各种恢复场景
- 验证 GUI 和 CLI 功能
- 验证预览功能正确性
