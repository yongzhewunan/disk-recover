# Development Specification

This document describes the development standards and conventions for the disk-recover project.

## Build & Test

```bash
# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Run all tests
./build/tests/Release/disk_recover_tests.exe

# Run specific test
./build/tests/Release/disk_recover_tests.exe --gtest_filter=<TestName>*
```

## Code Conventions

### Language & Comments
- Use C++20 standard
- All comments and documentation must be in **English**
- Use `//` for single-line comments, `/* */` for multi-line

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Namespaces | snake_case | `disk_recover`, `disk_recover::ntfs` |
| Classes/Structs | PascalCase | `ScanManager`, `RecoverableFile` |
| Functions/Methods | snake_case | `start_scan()`, `parse_boot_sector()` |
| Variables | snake_case | `sector_size_`, `total_sectors` |
| Constants | UPPER_SNAKE_CASE | `FLUSH_THRESHOLD`, `MAX_RETRY_COUNT` |
| Enum values | PascalCase | `ScanMode::Quick`, `FileType::Image` |
| Files | snake_case | `scan_manager.hpp`, `mft_parser.cpp` |

### Member Variables
- Private member variables use trailing underscore: `sector_size_`
- Use `mutable` for mutex in const methods

### Header Files
- Use `#pragma once` for header guards
- Include order: related header → project headers → system headers → third-party headers
- Example:
  ```cpp
  #pragma once
  #include "types.hpp"
  #include "disk_handle.hpp"
  #include <vector>
  #include <string>
  #include <windows.h>
  ```

### Code Style
- Indent with 4 spaces (no tabs)
- Opening brace on same line for functions and control structures
- Maximum line length: 120 characters
- Single statement per line
- Use `auto` when type is obvious or to avoid redundancy

### Error Handling
- Return `bool` for success/failure in simple operations
- Use `std::optional` for functions that may not return a value
- Log errors via `Logger` before returning failure:
  ```cpp
  if (!reader.read_sectors(sector, 1, buf)) {
      LOG_FMT(L"[MftParser] Failed to read sector %llu", sector);
      return false;
  }
  ```

### Logging
- Use `LOG_MSG()` for simple messages, `LOG_FMT()` for formatted output
- Prefix log messages with `[ClassName]` or `[ModuleName]`
- Example: `LOG_FMT(L"[ScanManager] Starting scan mode=%d", mode);`

### Thread Safety
- Use `std::mutex` and `std::lock_guard` for shared state
- Use `std::atomic` for flags and counters
- Document thread ownership in class comments:
  ```cpp
  // Non-copyable: owns a thread
  ScanManager(const ScanManager&) = delete;
  ScanManager& operator=(const ScanManager&) = delete;
  ```

## Module Organization

### Header Structure
```
src/
├── common/       # Types, logger, utilities (no external dependencies)
├── disk-io/      # Low-level disk access (Win32 API)
├── filesystem/   # Filesystem parsers (ntfs/, fat/, exfat/, raw/)
├── business/     # Business logic (depends on all above)
└── ui/           # User interfaces (cli/, gui/)
```

### Dependency Rules
- `common` has no project dependencies
- `disk-io` depends only on `common`
- `filesystem/*` depends on `disk-io` and `common`
- `business` depends on all filesystem modules
- `ui` depends on `business`

### Adding New Files
1. Create header in appropriate module directory
2. Create implementation file with same name
3. Add to corresponding `CMakeLists.txt`
4. Follow existing include patterns

## Git Workflow

### Commit Timing
- Commit immediately after code compiles successfully
- One logical change per commit
- Write meaningful commit messages:
  ```
  feat: add file validation infrastructure for accuracy improvement
  fix: CLI list-disks/list-files/review commands not working
  perf: optimize for large disks (5TB+) and massive files (2M+)
  ```

### Commit Message Format
Use conventional commits:
- `feat:` for new features
- `fix:` for bug fixes
- `perf:` for performance improvements
- `refactor:` for code restructuring
- `test:` for test additions/changes
- `docs:` for documentation

### Branch Strategy
- `main` is the stable branch
- Create feature branches for development
- Merge via PR after testing

## Testing

### Test File Naming
- Test files: `<module>_test.cpp` (e.g., `ntfs_test.cpp`)
- Test class: `<Module>Test` (e.g., `NtfsTest`)

### Writing Tests
```cpp
TEST(NtfsTest, ParsesBootSector) {
    // Arrange
    // Act
    // Assert
}
```

### Before Committing
1. Build in Release mode
2. Run all tests
3. Fix any failures before committing
