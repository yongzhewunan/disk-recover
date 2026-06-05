# 扫描不到文件且磁盘读取量过大 — 根因分析与修复计划

## 问题现象

- 全盘 RAW 扫描 PhysicalDrive1（256GB SSD），3分钟仅扫描 0.04%
- 扫描速度约 625 KB/s，严重低于预期
- 日志显示发现 10 个 MPEG-TS 文件，但最终恢复文件数 = 0
- `files_found` 日志值 = 180,388,627,019（1800亿），明显异常
- 实际有效文件 = 0

---

## 根因分析：三个互相关联的 Bug

### Bug 1：MPEG-TS 验证器假阳性爆炸（"扫描不到文件" + "磁盘读取量巨大"的根本原因）

**问题**：TS 验证器的签名只有单字节 `{0x47}`（`ts_validator.cpp:12`），且验证阈值过低。

| 环节 | 问题 | 影响 |
|------|------|------|
| 签名 | 仅匹配单字节 `0x47` | 随机数据中每 256 字节就出现一次，500M 扇区磁盘上有约 200 万个候选扇区 |
| header_check | 仅需 2 个间隔 188 字节的 0x47 即返回 AcceptHeader（`ts_validator.cpp:103-117`）| 随机数据中极大概率满足此条件 |
| data_check | **永不返回 Reject**，最低也返回 AcceptHeader（`ts_validator.cpp:205-210`）| 任何数据都能通过验证 |

**结果**：磁盘上约 0.1%~0.5% 的扇区被错误识别为 MPEG-TS 文件，产生海量假阳性。

### Bug 2：每个假阳性触发大量磁盘 I/O（"磁盘读取量巨大"的直接原因）

每个 TS 假阳性在 `try_recover_file()` 中触发 **两个昂贵的顺序读取循环**：

1. **渐进式雕刻**（`signature_scanner_impl.hpp:521-555`）：
   - 由于 `calculated_file_size = 0` 且 TS 有 `data_check`，循环向前读取 64 扇区块，最多读取 **102,400 扇区（50 MB）**
   - TS 的 `data_check` 永不返回 Reject 或 calc_size → **循环始终跑满 50 MB**

2. **下一文件头边界搜索**（`signature_scanner_impl.hpp:597-630`）：
   - 对视频文件，向前扫描最多 **204,800 扇区（100 MB）**，每个扇区调用 `FormatRegistry::match()`
   - 由于 `0x47` 假阳性无处不在，通常几个扇区内就找到"下一个文件头"并终止

**每个假阳性的 I/O 开销**：约 50 MB（渐进雕刻）+ 几百 KB（边界搜索）≈ **50 MB/次**

**全盘影响**：按每 10,000 扇区 1 个假阳性计算，50,000 个假阳性 × 50 MB = **2.5 TB 磁盘读取**来扫描 256 GB 磁盘。这就是日志中扫描速度极慢的原因。

### Bug 3：`files_found` 格式化字符串 Bug（日志显示异常数值）

**问题**：`signature_scanner_impl.hpp:478` 的日志格式：

```cpp
LOG_FMT(L"... files_found=%llu ...", progress.files_found);
```

- `%llu` 期望 `unsigned long long`（64位）
- 但 `ScanProgress::files_found` 是 `uint32_t`（32位，`types.hpp:104`）
- MSVC x64 调用约定下，32位参数用 `%llu` 读取时会从栈/寄存器读取 64 位，混入相邻数据（可能是 `bad_sectors_hit` 或 `sectors_scanned`）

**结果**：日志显示 180,388,627,019（不可能的值）。实际值约为 10（与日志中记录的 10 个 MPEG-TS 匹配一致）。

### 为什么最终恢复文件数 = 0

1. 10 个 MPEG-TS "文件"被收集到 `video_files` 向量中（`signature_scanner_impl.hpp:457`）
2. 仅在扫描循环结束后才通过 `on_file_found` 派发（`signature_scanner_impl.hpp:481-484`）
3. 这些文件的 `corruption_level = Severe`（无真实页脚，仅 AcceptHeader 级别验证）
4. `on_file_found` 回调跳过了所有 Severe 级别文件（`scan_recover_manager.cpp:379-387`）

---

## 修复计划

### 修复 1：强化 TS header_check 验证阈值

**文件**：`src/filesystem/raw/validators/ts_validator.cpp`

**改动**：
- 将 AcceptHeader 最低要求从 2 个同步字节提高到 **3 个连续同步字节**（188字节间隔）
- AcceptStructure 保持要求 5+ 个同步字节
- 在 header_check 阶段增加 **PID 一致性检查**：连续包的 PID 应相同或符合已知模式（如 PAT 0x000、PMT、null packet 0x1FFF）
- 单个 0x47 同步字节不再返回 AcceptHeader（删除 `ts_validator.cpp:105-106` 的特殊处理）

**预期效果**：假阳性率从 ~0.1% 降至 ~0.001% 以下

### 修复 2：TS data_check 增加 Reject 路径

**文件**：`src/filesystem/raw/validators/ts_validator.cpp`

**改动**：
- 当 `valid_packets == 0` 时返回 `ValidateResult::Reject`（而非 AcceptHeader）
- 要求至少 1 个有效包（sync=0x47 + 传输错误位=0 + 合法的 adaptation field）才算 AcceptHeader
- 连续性计数器不匹配时降低返回级别（AcceptHeader 而非 AcceptStructure）

**预期效果**：渐进式雕刻中能提前终止，减少无效 I/O

### 修复 3：TS 格式跳过渐进式雕刻

**文件**：`src/filesystem/raw/validators/ts_validator.cpp`（TS_DESCRIPTOR 定义）

**改动**：
- 将 TS_DESCRIPTOR 的 `data_check` 设为 `nullptr`
- TS 是流式格式，没有嵌入式文件大小，渐进式雕刻无法确定边界，只会浪费 I/O
- 仅依赖下一文件头边界搜索（Step 6）确定文件范围

**预期效果**：每个假阳性节省 ~50 MB 的渐进雕刻 I/O

### 修复 4：修正 files_found 格式化字符串

**文件**：`src/filesystem/raw/signature_scanner_impl.hpp`

**改动**：
```cpp
// 修改前
LOG_FMT(L"... files_found=%llu ...", progress.files_found);

// 修改后
LOG_FMT(L"... files_found=%llu ...", static_cast<unsigned long long>(progress.files_found));
```

**预期效果**：日志中 files_found 显示正确的数值

### 修复 5：视频文件定期派发，而非扫描结束后才派发

**文件**：`src/filesystem/raw/signature_scanner_impl.hpp`

**改动**：
- 当 `video_files` 向量超过 N 个条目（如 N=100）时，或在每 M 个扇区（如 M=1,000,000）时，执行 `merge_video_fragments` 并派发已合并的视频文件
- 这样用户能在扫描过程中看到视频文件的发现进度，恢复工作也能提前开始
- 扫描结束时仍需做一次最终派发（处理剩余的视频文件）

**预期效果**：扫描过程中即可看到和恢复视频文件

---

## 修复优先级

| 优先级 | 修复项 | 影响程度 | 实现复杂度 |
|--------|--------|----------|------------|
| 🔴 P0 | 修复 3：TS 跳过渐进式雕刻 | 立即消除 50MB/次 的无效 I/O | 极低（改 1 行） |
| 🔴 P0 | 修复 1：强化 TS header_check | 假阳性率降低 100 倍 | 中等 |
| 🟡 P1 | 修复 2：TS data_check 增加 Reject | 渐进雕刻能提前终止 | 低 |
| 🟢 P2 | 修复 4：files_found 格式化 | 仅影响日志显示 | 极低 |
| 🟢 P2 | 修复 5：视频文件定期派发 | 改善用户体验 | 中等 |

**建议实施顺序**：修复 3 → 修复 1 → 修复 2 → 修复 4 → 修复 5

修复 3 和修复 1 组合后，预期效果：
- 假阳性率从 ~0.1% 降至 <0.001%
- 每个假阳性的 I/O 从 ~50 MB 降至 ~几百 KB
- 整体扫描速度提升 **100 倍以上**
