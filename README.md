# Disk Recover - 磁盘数据恢复工具

一款专业的 Windows 磁盘数据恢复工具，支持从损坏或格式化的磁盘中恢复丢失的文件。支持 NTFS、FAT32、exFAT 文件系统，以及 RAW 签名扫描恢复。

## 功能特性

- **多种扫描模式**：快速扫描（元数据）、深度扫描（元数据+签名）、完整扫描（全盘签名）
- **多文件系统支持**：NTFS、FAT12/16/32、exFAT
- **RAW 签名扫描**：支持 JPG、PNG、GIF、BMP、MP4、MOV、AVI、MKV、MTS 等主流图片和视频格式
- **多目标目录恢复**：支持同时恢复到多个目录，自动切换和空间管理
- **扩展名分组**：按文件类型自动分组存放，每类每目录最多 500 个文件
- **扫描恢复**：支持中断后继续扫描，进度自动保存
- **坏道处理**：支持跳过、重试、强制读取三种坏道处理策略
- **图形界面**：提供 Windows 原生 GUI 界面，支持预览图片和视频
- **命令行界面**：支持完整的命令行操作，便于脚本化和自动化

## 项目结构

```
disk-recover/
├── src/
│   ├── common/                 # 公共模块
│   │   ├── types.hpp           # 核心类型定义
│   │   ├── logger.hpp          # 日志系统
│   │   └── utils.hpp           # 工具函数
│   │
│   ├── disk-io/                # 磁盘 I/O 模块
│   │   ├── disk_handle.hpp     # 磁盘句柄管理
│   │   ├── disk_info.hpp       # 磁盘信息查询
│   │   ├── sector_reader.hpp   # 扇区读取器
│   │   ├── aligned_buffer.hpp  # 对齐缓冲区
│   │   └── bad_sector_manager.hpp  # 坏道管理器
│   │
│   ├── filesystem/             # 文件系统解析模块
│   │   ├── ntfs/               # NTFS 解析
│   │   │   ├── mft_parser.hpp  # MFT 解析器
│   │   │   └── ntfs_types.hpp  # NTFS 类型定义
│   │   ├── fat/                # FAT 解析
│   │   │   ├── fat_parser.hpp  # FAT 解析器
│   │   │   └── fat_types.hpp   # FAT 类型定义
│   │   ├── exfat/              # exFAT 解析
│   │   │   ├── exfat_parser.hpp
│   │   │   └── exfat_types.hpp
│   │   └── raw/                # RAW 签名扫描
│   │       ├── signature_scanner.hpp
│   │       └── file_signatures.hpp
│   │
│   ├── business/               # 业务逻辑模块
│   │   ├── scan_manager.hpp    # 扫描管理器
│   │   ├── recovery_manager.hpp # 恢复管理器
│   │   ├── scan_cache_db.hpp   # SQLite 缓存数据库
│   │   ├── multi_target_writer.hpp # 多目标写入器
│   │   └── preview_manager.hpp # 文件预览管理
│   │
│   └── ui/                     # 用户界面
│       ├── gui/                # 图形界面
│       │   ├── main_window.hpp
│       │   └── save_dirs_dialog.hpp
│       └── cli/                # 命令行界面
│           └── main.cpp
│
├── tests/                      # 单元测试
├── build/                      # 构建输出目录
└── CMakeLists.txt              # CMake 配置
```

## 构建要求

- **编译器**：支持 C++20 的 MSVC (Visual Studio 2022)
- **CMake**：3.21 或更高版本
- **vcpkg**：用于管理依赖库

### 依赖库

- [CLI11](https://github.com/CLIUtils/CLI11) - 命令行解析
- [SQLite3](https://www.sqlite.org/) - 扫描缓存数据库
- [FFmpeg](https://ffmpeg.org/) - 视频预览（开发中）
- [Google Test](https://github.com/google/googletest) - 单元测试

## 构建步骤

```bash
# 1. 安装 vcpkg（如果尚未安装）
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat

# 2. 安装依赖
vcpkg install cli11:x64-windows
vcpkg install sqlite3:x64-windows
vcpkg install ffmpeg:x64-windows
vcpkg install gtest:x64-windows

# 3. 配置 CMake
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg路径]/scripts/buildsystems/vcpkg.cmake

# 4. 构建
cmake --build build --config Release
```

构建完成后，可执行文件位于：
- GUI 版本：`build/src/ui/gui/Release/disk-recover.exe`
- CLI 版本：`build/src/ui/cli/Release/disk-recover-cli.exe`

## 使用方法

### 图形界面 (GUI)

直接运行 `disk-recover.exe`：

1. **选择磁盘**：从下拉列表中选择要扫描的物理磁盘
2. **选择分区**：选择要扫描的分区（或选择整个磁盘）
3. **配置选项**：
   - 扫描模式：快速/深度/完整
   - 坏道策略：跳过/重试/强制读取
   - 文件类型：图片/视频
4. **开始扫描**：点击"扫描"按钮
5. **预览文件**：扫描完成后可预览找到的文件
6. **恢复文件**：选择保存目录，点击"恢复"

### 命令行界面 (CLI)

#### 列出磁盘

```bash
disk-recover-cli list-disks
```

输出示例：
```
磁盘 0: ST500DM009-2F110A (465.76 GB)
  扇区大小: 512  总扇区数: 976773168
  分区 0: NTFS 起始=2048 大小=500.00 GB
磁盘 1: WDC PC SN730 (238.47 GB)
  分区 0: NTFS 起始=2048 大小=100.00 GB
```

#### 扫描磁盘

```bash
# 基本扫描
disk-recover-cli scan "\\.\PhysicalDrive1" --session myscan

# 指定模式和坏道策略
disk-recover-cli scan "\\.\PhysicalDrive1" --mode full --bad-sector retry

# 恢复中断的扫描
disk-recover-cli scan "\\.\PhysicalDrive1" --resume myscan
```

参数说明：
- `device`：磁盘设备路径（必需）
- `--session`：扫描会话ID，用于保存和恢复进度
- `--db`：缓存数据库路径（默认：scan_cache.db）
- `--mode`：扫描模式
  - `quick`：快速扫描，仅解析文件系统元数据
  - `deep`：深度扫描，元数据 + RAW 签名扫描（默认）
  - `full`：完整扫描，全盘 RAW 签名扫描
- `--bad-sector`：坏道处理策略
  - `skip`：跳过坏道（默认）
  - `retry`：重试读取 3 次
  - `force`：强制读取，尽可能恢复数据
- `--resume`：恢复中断的扫描会话ID
- `--no-images`：不扫描图片文件
- `--no-videos`：不扫描视频文件

#### 恢复文件

```bash
disk-recover-cli recover --session myscan --output "D:\Recovered"
```

参数说明：
- `--session`：扫描会话ID（必需）
- `--output`：输出目录（必需）
- `--db`：缓存数据库路径
- `--device`：源磁盘设备路径（恢复 RAW 扫描结果时需要）
- `--filter`：文件名过滤器（支持通配符 * 和 ?）

#### 查看扫描结果

```bash
disk-recover-cli list-files --session myscan
```

#### 生成预览缩略图

```bash
disk-recover-cli preview --session myscan --output "D:\thumbnails"
```

## 扫描模式详解

### 快速扫描 (Quick)
- 仅解析文件系统元数据（MFT、FAT 表等）
- 速度最快，适合文件系统完好的情况
- 无法恢复已删除文件

### 深度扫描 (Deep)
- 先进行快速扫描，再进行 RAW 签名扫描
- 适合文件系统部分损坏的情况
- 可以恢复部分已删除文件

### 完整扫描 (Full)
- 全盘 RAW 签名扫描，不依赖文件系统
- 耗时最长，但恢复率最高
- 适合严重损坏或格式化的磁盘

## 性能优化

本项目针对大容量磁盘（5TB+）和大量文件（200万+）进行了专门优化：

| 优化项 | 说明 |
|--------|------|
| 批量 I/O | 4MB 批量读取，减少系统调用 |
| 游标分页 | 数据库查询使用游标，避免 OFFSET 性能问题 |
| MFT 批量解析 | 一次读取 64 条 MFT 记录 |
| FAT 缓存 | LRU 缓存 FAT 扇区，减少重复读取 |
| 写入缓冲 | 4MB 写入缓冲区，提升大文件恢复速度 |

预估性能（5TB 磁盘，200万文件）：
- 扫描：2.5-3.5 小时
- 恢复：4-7 小时

## 注意事项

1. **管理员权限**：需要以管理员身份运行才能访问物理磁盘
2. **不要写入源磁盘**：恢复时不要将文件保存到正在扫描的磁盘
3. **及时停止**：发现磁盘有异响时应立即停止扫描
4. **备份重要数据**：在使用本工具前，建议对重要数据进行镜像备份

## 开发说明

### 运行测试

```bash
cmake --build build --config Release
./build/tests/Release/disk_recover_tests.exe
```

### 代码结构

- 使用 C++20 标准
- 模块化设计，各模块职责清晰
- 使用 SQLite 存储扫描进度和结果
- 支持多线程扫描和恢复

## 许可证

本项目仅供学习和研究使用。

## 致谢

- [NTFS Documentation](https://docs.microsoft.com/en-us/windows/win32/fileio/ntfs-technical-reference)
- [FAT File System Specification](https://staff.washington.edu/dittrich/miscreading/fatgen103.pdf]
- [exFAT File System Specification](https://docs.microsoft.com/en-us/windows/win32/fileio/exfat-specification)
