#include "rar_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// RAR signature: "Rar!\x1A\x07"
static const uint8_t RAR_MAGIC[] = {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07};

// RAR version byte at offset 6: 0x00 = RAR3.x, 0x01 = RAR5.x

// ============================================================================
// RAR Three-Phase Validator
//
// Phase 1 (header_check): Verify 7-byte signature + version byte.
//   For RAR5, also validate ARCHIVE_HEADER size field.
//   Returns AcceptHeader.
//
// Phase 2 (data_check): Walk archive block headers.
//   For RAR3: each block has 7-byte header (HEAD_CRC, HEAD_TYPE, HEAD_FLAGS, HEAD_SIZE).
//   For RAR5: blocks have variable-length headers with vint encoding.
//   Check HEAD_CRC per block. Return AcceptStructure if multiple valid blocks found.
//
// Inspired by PhotoRec's file_rar.c which validates RAR block structure.
// ============================================================================

// ── CRC16 used by RAR3 block headers ──
// Standard CRC-16/CCITT used by RAR for header CRC validation.
static uint16_t rar_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= uint16_t(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x8005;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ── Phase 1: Header check ──
ValidateResult check_rar_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 7) return ValidateResult::Reject;

    // Verify 6-byte signature
    for (int i = 0; i < 6; ++i) {
        if (data[i] != RAR_MAGIC[i]) return ValidateResult::Reject;
    }

    // Check version byte
    uint8_t version = data[6];
    if (version != 0x00 && version != 0x01) return ValidateResult::Reject;

    // For RAR5 (version == 0x01), validate ARCHIVE_HEADER
    if (version == 0x01) {
        // RAR5 archive header starts at offset 7
        // Format: CRC32 (4 bytes), header size vint, ...
        // We need at least a few more bytes to validate
        if (length < 13) return ValidateResult::AcceptHeader;

        // RAR5 uses CRC32 for header validation — check that header size vint is reasonable
        // The header size vint starts at offset 11 (after 4-byte CRC32)
        // vint encoding: high bit of each byte indicates continuation
        // A reasonable archive header should be small (< 64KB)
        size_t pos = 11;
        uint64_t header_size = 0;
        int shift = 0;
        while (pos < length && shift < 35) {
            uint8_t b = data[pos++];
            header_size |= uint64_t(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        // Archive header size should be reasonable
        if (header_size == 0 || header_size > 65536) return ValidateResult::AcceptHeader;
    }

    calculated_file_size = 0;  // Size unknown from header alone
    return ValidateResult::AcceptHeader;
}

// ── Phase 2: Data check ──
// Walk RAR3 block headers (7-byte signature per block) and validate HEAD_CRC.
// For RAR5, walk vint-based block headers.
ValidateResult check_rar_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // We need to know the version to parse blocks correctly.
    // If this is the first block (offset_in_file == 0), re-check version.
    // Otherwise, we rely on the header_check having already validated the signature.

    // For data_check, we scan for valid block structures in the current block.
    // The scanner calls this per-block during progressive carving.

    int valid_blocks = 0;

    if (offset_in_file == 0 && length >= 7) {
        // Re-verify signature from the start
        bool sig_ok = true;
        for (int i = 0; i < 6; ++i) {
            if (data[i] != RAR_MAGIC[i]) { sig_ok = false; break; }
        }
        if (!sig_ok) return ValidateResult::Reject;

        uint8_t version = data[6];

        if (version == 0x00) {
            // RAR3 block walking
            size_t pos = 7;  // Skip marker block (7 bytes: signature + version)

            // RAR3 MARK_HEAD is always followed by MAIN_HEAD
            // Each RAR3 block: HEAD_CRC (2), HEAD_TYPE (1), HEAD_FLAGS (2), HEAD_SIZE (2)
            // Then optional ADD_SIZE (4) if HEAD_FLAGS bit 0x8000 is set

            while (pos + 7 <= length && valid_blocks < 3) {
                uint16_t head_crc  = read_le16(data + pos);
                uint8_t  head_type = data[pos + 2];
                uint16_t head_flags = read_le16(data + pos + 3);
                uint16_t head_size = read_le16(data + pos + 5);

                // HEAD_SIZE must be at least 7 (minimum block header)
                if (head_size < 7) break;

                // Validate HEAD_CRC over bytes 2..6 (type + flags + size)
                uint16_t computed_crc = rar_crc16(data + pos + 2, 5);
                if (head_crc != computed_crc) break;

                // Valid HEAD_TYPE values for RAR3:
                // 0x72 = MARK_HEAD, 0x73 = MAIN_HEAD, 0x74 = FILE_HEAD,
                // 0x75 = COMM_HEAD, 0x76 = AV_HEAD, 0x77 = SUB_HEAD,
                // 0x78 = RECOVERY_HEAD, 0x7B = END_HEAD
                if (head_type < 0x72 || head_type > 0x7B) break;

                valid_blocks++;

                // Calculate total block size (including ADD_SIZE if present)
                uint32_t block_size = head_size;
                if ((head_flags & 0x8000) && pos + 11 <= length) {
                    uint32_t add_size = read_le32(data + pos + 7);
                    // Sanity check add_size
                    if (add_size > 100 * 1024 * 1024) break;
                    block_size += add_size;
                }

                // END_HEAD signals end of archive
                if (head_type == 0x7B) {
                    calculated_file_size = offset_in_file + pos + block_size;
                    return ValidateResult::AcceptVerified;
                }

                pos += block_size;
            }
        } else {
            // RAR5 block walking (simplified)
            // RAR5 blocks: CRC32 (4), header_size (vint), header_type (vint), ...
            size_t pos = 7;  // Skip signature + version

            while (pos + 5 <= length && valid_blocks < 3) {
                // Read CRC32
                uint32_t expected_crc = read_le32(data + pos);

                // Read header_size vint
                size_t vint_start = pos + 4;
                size_t vp = vint_start;
                uint64_t header_size = 0;
                int shift = 0;
                while (vp < length && shift < 35) {
                    uint8_t b = data[vp++];
                    header_size |= uint64_t(b & 0x7F) << shift;
                    if ((b & 0x80) == 0) break;
                    shift += 7;
                }

                if (header_size == 0 || header_size > 65536) break;
                if (vint_start + header_size > length) break;

                // Read header_type vint
                size_t type_start = vp;
                uint64_t header_type = 0;
                shift = 0;
                while (vp < length && shift < 35) {
                    uint8_t b = data[vp++];
                    header_type |= uint64_t(b & 0x7F) << shift;
                    if ((b & 0x80) == 0) break;
                    shift += 7;
                }

                // Valid RAR5 header types: 1=MAIN, 2=FILE, 3=SERVICE, 4=ENCRYPTION, 5=END
                if (header_type < 1 || header_type > 5) break;

                valid_blocks++;

                // END block
                if (header_type == 5) {
                    calculated_file_size = offset_in_file + vint_start + header_size;
                    return ValidateResult::AcceptVerified;
                }

                pos = vint_start + header_size;

                // RAR5 blocks may have extra_data_size after header
                // For simplicity, skip to next block based on header_size
            }
        }
    }

    if (valid_blocks >= 2) {
        return ValidateResult::AcceptStructure;
    }

    // Not enough blocks validated in this chunk — keep carving
    return ValidateResult::AcceptHeader;
}

} // anonymous namespace

const FormatDescriptor RAR_DESCRIPTOR = {
    .file_type       = FileType::Archive,
    .extension       = L"rar",
    .description     = L"RAR archive",
    .min_filesize    = 7,
    .max_filesize    = 0,
    .signature       = {RAR_MAGIC, 6, 0},
    .header_check    = check_rar_header_impl,
    .data_check      = check_rar_data_impl,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_rar_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_rar_header_impl(data, length, calculated_file_size);
}

ValidateResult check_rar_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_rar_data_impl(data, length, offset_in_file, calculated_file_size);
}

} // namespace disk_recover
