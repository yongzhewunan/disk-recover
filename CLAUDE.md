# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (run from project root)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Run tests
./build/tests/Release/disk_recover_tests.exe

# Run single test (GoogleTest filter)
./build/tests/Release/disk_recover_tests.exe --gtest_filter=SignatureScannerTest*
```

## Architecture

This is a Windows disk data recovery tool written in C++20. It recovers files from damaged or formatted disks using filesystem metadata and/or RAW signature scanning.

### Data Flow
```
PhysicalDisk -> SectorReader -> FileSystemParser/SignatureScanner -> RecoverableFile -> ScanCacheDB -> RecoveryManager -> Output
```

### Core Modules

**disk-io/**: Low-level disk access. `SectorReader` reads sectors via `DiskHandle` (Win32 `CreateFile`). Uses `AlignedBuffer` for proper memory alignment. `BadSectorManager` tracks failed sectors with configurable policies (skip/retry/force).

**filesystem/**: Three parallel parser implementations:
- `ntfs/`: MFT record parsing with data run extraction
- `fat/`: FAT12/16/32 chain traversal
- `exfat/`: exFAT cluster chain
- `raw/`: Signature-based file carving with confidence scoring

**business/**: Orchestration layer.
- `ScanManager`: Coordinates scanning, handles pause/resume via `ScanProgress` state
- `RecoveryManager`: Writes recovered files with multi-target and extension grouping
- `ScanCacheDB`: SQLite persistence for scan results and progress
- `PreviewManager`: Thumbnail generation via Windows Imaging Component and FFmpeg

### Key Types

`RecoverableFile` is the central data structure containing fragments (`DiskExtent` list), file type, corruption flag, and optional MFT ID.

### Scan Modes

- `Quick`: Parse filesystem metadata only (MFT, FAT tables)
- `Deep`: Metadata + RAW signature scan
- `Full`: Full disk RAW signature scan (no filesystem)

### File Signature Validation

`file_signatures.hpp` defines the signature matching interface. Each format has a dedicated validator in `raw/validators/` returning `MatchResult` with confidence score (0-100) and `MatchFlags` indicating validation depth.

### Threading Model

`ScanManager` and `RecoveryManager` each own a worker thread. Progress is communicated via callbacks and atomic flags. UI should use `take_found_files()` for batch updates to avoid lock contention.
