# JPG 文件恢复验证器修复计划

## 问题分析

对比 PhotoRec 的 `file_jpg.c` 实现，当前 `jpeg_validator.cpp` 存在以下关键问题导致匹配不准：

### 1. 第四字节验证缺失（关键问题）

PhotoRec 在 `header_check_jpg` 函数（第 1021-1047 行）有严格的第四字节验证：

```c
switch(buffer[3])
{
  case 0xe0:	/* APP0 */
    if(buffer[6]!='J' || buffer[7]!='F')	/* Should be JFIF/JFXX */
    {
      header_ignored(file_recovery_new);
      return 0;
    }
    break;
  case 0xe1:		/* APP1 */
    if(buffer[6]!='E' || buffer[7]!='x' || buffer[8]!='i'|| buffer[9]!='f')	/* Should be Exif */
    {
      header_ignored(file_recovery_new);
      return 0;
    }
    break;
  case 0xfe:		/* COM */
    if(!isprint(buffer[6]) || !isprint(buffer[7]))
    {
      header_ignored(file_recovery_new);
      return 0;
    }
    break;
  default:
    header_ignored(file_recovery_new);
    return 0;
}
```

**当前实现问题**：只检查 `FF D8 FF`，对第四字节没有任何验证，导致大量误匹配。

### 2. 缺少容器内嵌入 JPEG 检测

PhotoRec 会检测并跳过以下容器内嵌入的 JPEG：
- AVI 容器内的 JPEG（匹配 `jpg_header_app0_avi` 模式）
- MOV 容器内的 JPEG（匹配 `jpg_header_app0_jfif11_null` 和 `jpg_header_app0_jfif11_com` 模式）
- RW2 容器内的 JPEG（文件大小 <= 8192 字节）

### 3. 缺少缩略图误提取防护

PhotoRec 会检测小尺寸 JPEG 缩略图（width < 200 && height < 200）并跳过：
```c
if(file_recovery->file_size <= 16384 &&
    buffer[3]==0xe0 &&
    width>0 && width<200 && height>0 && height<200)
{
  if(header_ignored_adv(file_recovery, file_recovery_new)==0)
    return 0;
}
```

## 修复方案

### 第一阶段：添加第四字节验证（高优先级）

在 `check_jpeg_header_impl` 函数中添加严格的第四字节验证：

1. 验证 `buffer[3]` 是否为有效的 APP 标记：
   - `0xE0` → APP0，必须验证 JFIF 标识符（`buffer[6-7] == 'JF'`）
   - `0xE1` → APP1，必须验证 Exif 标识符（`buffer[6-9] == 'Exif'`）
   - `0xFE` → COM，必须验证可打印字符
   - 其他标记（如 `0xC0-0xCF` SOF、`0xDB` DQT）可能直接出现，需要继续解析

2. 对于纯 SOF 标记开头（无 APP0/APP1）的情况：
   - 检查 SOF 标记有效性（precision=8/12, 非零尺寸）

### 第二阶段：改进标记解析逻辑

1. 在标记解析循环中增加更严格的有效性检查
2. 参考 PhotoRec 的 `is_marker_valid` 函数
3. 对 DHT 内容进行深度验证

### 第三阶段：添加嵌入 JPEG 检测（可选）

1. 添加对连续 JPEG 文件场景的检测
2. 检测缩略图特征（小尺寸、短文件）
3. 可能需要扫描器层面的配合

## 具体代码修改

### jpeg_validator.cpp 第 32-206 行

需要在 `check_jpeg_header_impl` 函数开头添加：

```cpp
// 严格验证第四字节（借鉴 PhotoRec）
if (length < 10) return ValidateResult::Reject;  // 需要至少 10 字节

uint8_t fourth_byte = data[3];

// 第四字节必须是有意义的标记
switch (fourth_byte) {
  case 0xE0:  // APP0 - JFIF
    if (length < 8) return ValidateResult::Reject;
    if (data[6] != 'J' || data[7] != 'F') return ValidateResult::Reject;
    break;
  case 0xE1:  // APP1 - Exif
    if (length < 10) return ValidateResult::Reject;
    if (data[6] != 'E' || data[7] != 'x' || data[8] != 'i' || data[9] != 'f')
      return ValidateResult::Reject;
    break;
  case 0xFE:  // COM - Comment
    if (length < 8) return ValidateResult::Reject;
    // 注释内容必须是可打印 ASCII 或扩展 ASCII
    if (!is_printable(data[6]) && !is_printable(data[7]))
      return ValidateResult::Reject;
    break;
  case 0xE2:  // APP2 - 可能是 MPF（多图像格式）
    // 允许通过，后续检查 MPF 标识符
    break;
  case 0xDB:  // DQT - Quantization Table
    // 允许通过，纯 SOF/DQT 开头的 JPEG 罕见但有效
    break;
  case 0xC0: case 0xC1: case 0xC2: case 0xC3:  // SOF0-SOF3
  case 0xC5: case 0xC6: case 0xC7:             // SOF5-SOF7
  case 0xC9: case 0xCA: case 0xCB:             // SOF9-SOF11
  case 0xCD: case 0xCE: case 0xCF:             // SOF13-SOF15
    // 允许通过，直接 SOF 开头（无 APP）
    break;
  case 0xC4:  // DHT
    // 允许通过，需要深度验证
    break;
  case 0xDD:  // DRI
    // 允许通过
    break;
  default:
    // 其他标记几乎不可能作为第四字节
    return ValidateResult::Reject;
}
```

### 辅助函数

添加 `is_printable` 函数：
```cpp
static bool is_printable(uint8_t c) {
    return (c >= 0x20 && c <= 0x7E) || (c >= 0xA1 && c <= 0xFE);
}
```

## 测试验证

修复后需要验证：

1. 有效的 JFIF JPEG 文件能正常匹配
2. 有效的 Exif JPEG 文件能正常匹配
3. 无效的第四字节被正确拒绝
4. 纯 SOF 开头的 JPEG（无 APP）能正常匹配（罕见但存在）
5. 容器内嵌入的假 JPEG 签名被正确拒绝或降低置信度

## 参考

- PhotoRec `file_jpg.c` 第 882-1058 行的 `header_check_jpg` 函数
- PhotoRec 第 1021-1047 行的第四字节验证逻辑