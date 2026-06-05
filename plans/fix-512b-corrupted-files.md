# 修复 HEIC/GIF 512字节损坏文件问题

## 背景

扫描恢复产生大量 512 字节的 HEIC 和 GIF 损坏文件。根因是 `signature_scanner_impl.hpp` 的 `try_recover_file()` 中，Step 1 的 `min_filesize` 提升逻辑污染了 `estimated_size`，导致后续的 `data_check`（Step 2）、`file_check`（Step 3）和默认值（Step 4）都被跳过。

## 根因因果链

```
header_check(data, 512) → calculated_file_size = 0
     ↓
Step 1: estimated_size = min_filesize (GIF=256, HEIC=32)
     ↓
Step 2: estimated_size != 0 → data_check 被跳过
     ↓
Step 3: estimated_size != 0 → file_check 被跳过
     ↓
Step 4: estimated_size != 0 → 默认值被跳过
     ↓
file_sectors = ceil(min_filesize / 512) = 1
     ↓
file.file_size = 512 字节
```

## 三个核心 Bug

| Bug | 位置 | 问题 |
|-----|------|------|
| Bug 1 | Step 1 (第550行) | `min_filesize` 覆盖 `estimated_size=0`，阻止后续步骤执行 |
| Bug 2 | Step 7 (第688行) | `file_check` 只允许缩小不允许放大文件大小 |
| Bug 3 | GIF data_check (第122行) | 盲目匹配任何 `0x3B` 字节作为 trailer，无上下文验证 |

## 修改计划

### 修改 1：Step 1 — 不再用 min_filesize 覆盖 estimated_size（核心修复）

**文件**：`src/filesystem/raw/signature_scanner_impl.hpp` 第 548-552 行

`min_filesize` 应仅用于最终验证（Step 8），不应影响大小估计。当 `header_check` 返回 `calculated_file_size=0` 时，`estimated_size` 应保持为 0，让 data_check/file_check/默认值阶段正常执行。

```cpp
// 旧代码：
if (desc->min_filesize > 0 && (estimated_size == 0 || estimated_size < desc->min_filesize)) {
    estimated_size = desc->min_filesize;
}

// 新代码：
// min_filesize 仅用于验证，不用于大小估计
// 如果 header_check 返回了非零但小于 min_filesize 的大小，说明头部损坏
if (desc->min_filesize > 0 && estimated_size > 0 && estimated_size < desc->min_filesize) {
    return false;  // Header-declared size below minimum → false positive
}
```

**影响**：
- HEIC/GIF/JPEG/PNG：`calculated_file_size=0` 不再被覆盖，Step 2/3/4 正常执行
- BMP/WebP/AVI：`header_check` 返回非零大小，若小于 `min_filesize` 则被拒绝（正确行为）
- Step 8 的 `min_filesize` 最终验证仍然有效

### 修改 2：Step 7 — 允许 file_check 放大文件大小

**文件**：`src/filesystem/raw/signature_scanner_impl.hpp` 第 687-693 行

HEIC 的 `file_check` 从 atom tree 计算出真实大小（可能几 MB），远大于 `estimated_size`，但当前只允许缩小。

```cpp
// 旧代码：
if (calc_size > 0 && calc_size < estimated_size) {

// 新代码：允许缩小和放大
if (calc_size > 0 && calc_size != estimated_size) {
```

### 修改 3：GIF data_check — 改进 trailer 上下文验证

**文件**：`src/filesystem/raw/validators/gif_validator.cpp` 第 119-133 行

当前盲目匹配任何 `0x3B` 字节。应检查前面是否有子块终止符 `0x00`。

```cpp
// 新逻辑：0x3B 前面必须是 0x00（子块终止符）或在块起始位置
bool valid_context = false;
if (i == 0) {
    valid_context = true;  // 块起始，假设前一块正确结束
} else if (data[i - 1] == 0x00) {
    valid_context = true;  // 子块终止符后 — 正确
}
if (valid_context) {
    calculated_file_size = offset_in_file + i + 1;
    return ValidateResult::AcceptVerified;
}
```

### 修改 4：GIF 添加 file_check

**文件**：`src/filesystem/raw/validators/gif_validator.cpp` + `gif_validator.hpp`

添加 `check_gif_file_impl`：从磁盘重新遍历整个 GIF 文件，验证签名+LSD+GCT，遍历所有图像描述符和扩展块，找到 trailer `0x3B`，返回精确文件大小。

更新 `GIF_DESCRIPTOR.file_check = check_gif_file_impl`。

## 关键文件

| 文件 | 修改内容 |
|------|----------|
| `src/filesystem/raw/signature_scanner_impl.hpp` | Step 1 逻辑 + Step 7 逻辑 |
| `src/filesystem/raw/validators/gif_validator.cpp` | data_check 改进 + file_check 添加 |
| `src/filesystem/raw/validators/gif_validator.hpp` | file_check 声明 |

## 验证

1. `cmake --build build --config Release` 编译通过
2. `./build/tests/Release/disk_recover_tests.exe` 全部测试通过
3. 对比修复前后：GIF/HEIC 文件大小不再是 512 字节
