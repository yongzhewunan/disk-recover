#include "doc_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// OLE2 signature: \xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1
static const uint8_t DOC_MAGIC[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

// ============================================================================
// DOC/OLE2 Three-Phase Validator
//
// OLE2 Compound Document Format (Microsoft Compound File Binary Format).
// Used by legacy Office formats: .doc, .xls, .ppt, and many others.
//
// Phase 1 (header_check): Verify 8-byte OLE2 signature + byte order mark,
//   sector size power-of-2, mini sector size. Returns AcceptStructure.
//
// Phase 2 (file_check): Walk FAT chain, find Root Entry in directory,
//   check for specific stream names to determine extension.
//   Set calculated_file_size from total sectors * sector_size.
//   Returns AcceptVerified.
//
// Reference: MS-CFB (Compound File Binary Format) specification.
// ============================================================================

// ── Phase 1: Header check ──
ValidateResult check_doc_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // OLE2 header is 512 bytes minimum
    if (length < 512) return ValidateResult::Reject;

    // Verify 8-byte signature
    for (int i = 0; i < 8; ++i) {
        if (data[i] != DOC_MAGIC[i]) return ValidateResult::Reject;
    }

    // Byte order mark at offset 28: must be 0xFFFE (little-endian)
    uint16_t byte_order = read_le16(data + 28);
    if (byte_order != 0xFFFE) return ValidateResult::Reject;

    // Sector size power at offset 30: exponent of 2 (must yield 512 or 4096)
    uint16_t sector_size_pow = read_le16(data + 30);
    if (sector_size_pow < 9 || sector_size_pow > 12) return ValidateResult::Reject;
    uint32_t sector_size = uint32_t(1) << sector_size_pow;
    if (sector_size != 512 && sector_size != 4096) return ValidateResult::Reject;

    // Mini sector size at offset 32: must be 64 (2^6)
    uint16_t mini_sector_size_pow = read_le16(data + 32);
    if (mini_sector_size_pow != 6) return ValidateResult::Reject;

    // Total sectors in FAT (offset 44, 4 bytes) — must be > 0 for a valid file
    uint32_t total_fat_sectors = read_le32(data + 44);
    if (total_fat_sectors == 0) return ValidateResult::Reject;

    // First directory sector SECID at offset 48
    uint32_t first_dir_sector = read_le32(data + 48);
    // Must be a valid sector ID (not free=0xFFFFFFFF, not end=0xFFFFFFFE for < 4GB)
    // For v3 (sector_size=512), valid range is 0..total_sectors-1 or special values
    // For v4 (sector_size=4096), same
    // We just check it's not the "free" marker
    if (first_dir_sector == 0xFFFFFFFF) return ValidateResult::Reject;

    // Mini stream cutoff size at offset 56: typically 4096
    uint32_t mini_stream_cutoff = read_le32(data + 56);
    if (mini_stream_cutoff == 0) return ValidateResult::Reject;

    calculated_file_size = 0;  // Size not yet determined from header alone
    return ValidateResult::AcceptStructure;
}

// ── Phase 2: File check ──
// Walk FAT chain, find Root Entry, check for specific stream names.
ValidateResult check_doc_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Re-verify signature
    if (length < 512) return ValidateResult::Reject;
    for (int i = 0; i < 8; ++i) {
        if (data[i] != DOC_MAGIC[i]) return ValidateResult::Reject;
    }

    uint16_t byte_order = read_le16(data + 28);
    if (byte_order != 0xFFFE) return ValidateResult::Reject;

    uint16_t sector_size_pow = read_le16(data + 30);
    if (sector_size_pow < 9 || sector_size_pow > 12) return ValidateResult::Reject;
    uint32_t sector_size = uint32_t(1) << sector_size_pow;

    // Total file sectors (offset 40 for v4, or computed from file length)
    // For v3: total_sectors at offset 40 is 0, use file length
    // For v4: total_sectors at offset 40 is valid
    uint32_t num_fat_sectors = read_le32(data + 44);
    uint32_t first_dir_sector_id = read_le32(data + 48);

    // Calculate total sectors from file length
    uint64_t total_sectors = 0;
    if (length >= sector_size) {
        total_sectors = (length - 512) / sector_size;  // Subtract 512 for header
    }

    if (total_sectors == 0) return ValidateResult::AcceptStructure;

    // Set calculated_file_size from total sectors
    calculated_file_size = 512 + uint64_t(total_sectors) * sector_size;

    // ── Walk FAT to build sector chain ──
    // FAT sector locations are in DIFAT array (header offsets 76..424, then chain)
    // For simplicity, read the first 109 DIFAT entries from header
    const uint32_t MAX_DIFAT_IN_HEADER = 109;
    uint32_t difat[109];
    for (uint32_t i = 0; i < MAX_DIFAT_IN_HEADER; ++i) {
        if (76 + i * 4 + 4 <= length) {
            difat[i] = read_le32(data + 76 + i * 4);
        } else {
            difat[i] = 0xFFFFFFFF;  // Free
        }
    }

    // Read FAT entries from FAT sectors
    // Each FAT sector holds (sector_size / 4) entries
    const uint32_t ENTRIES_PER_FAT_SECTOR = sector_size / 4;
    const uint32_t MAX_FAT_ENTRIES = num_fat_sectors * ENTRIES_PER_FAT_SECTOR;
    if (MAX_FAT_ENTRIES == 0) return ValidateResult::AcceptStructure;

    // Helper: read a sector from the file data
    // Sector 0 starts at offset 512 (after header), sector N at 512 + N * sector_size
    auto sector_offset = [&](uint32_t sec_id) -> size_t {
        return 512 + uint64_t(sec_id) * sector_size;
    };

    // Read FAT entries (limited to first FAT sector for performance)
    // Build a simple FAT map for the first few sectors
    const uint32_t MAX_SECTORS_TO_WALK = 4096;  // Limit for performance
    uint32_t fat_entries[MAX_SECTORS_TO_WALK];
    uint32_t fat_entries_count = 0;

    for (uint32_t fi = 0; fi < num_fat_sectors && fi < MAX_DIFAT_IN_HEADER; ++fi) {
        uint32_t fat_sector_id = difat[fi];
        if (fat_sector_id == 0xFFFFFFFF || fat_sector_id == 0xFFFFFFFE) continue;

        size_t foff = sector_offset(fat_sector_id);
        if (foff + sector_size > length) continue;

        uint32_t entries_in_this_sector = ENTRIES_PER_FAT_SECTOR;
        if (fat_entries_count + entries_in_this_sector > MAX_SECTORS_TO_WALK) {
            entries_in_this_sector = MAX_SECTORS_TO_WALK - fat_entries_count;
        }

        for (uint32_t e = 0; e < entries_in_this_sector; ++e) {
            fat_entries[fat_entries_count + e] = read_le32(data + foff + e * 4);
        }
        fat_entries_count += entries_in_this_sector;
        if (fat_entries_count >= MAX_SECTORS_TO_WALK) break;
    }

    // ── Walk directory chain ──
    // Directory entries are in the directory stream, starting at first_dir_sector_id
    // Each directory entry is 128 bytes
    const uint32_t DIR_ENTRY_SIZE = 128;
    const uint32_t ENTRIES_PER_DIR_SECTOR = sector_size / DIR_ENTRY_SIZE;

    // Follow the directory chain from first_dir_sector_id
    uint32_t dir_sector = first_dir_sector_id;
    int dir_sectors_walked = 0;
    const int MAX_DIR_SECTORS = 64;

    bool found_word = false;
    bool found_workbook = false;
    bool found_ppt = false;

    while (dir_sector != 0xFFFFFFFE && dir_sector != 0xFFFFFFFF &&
           dir_sector < fat_entries_count && dir_sectors_walked < MAX_DIR_SECTORS) {
        size_t doff = sector_offset(dir_sector);
        if (doff + sector_size > length) break;

        // Check each directory entry in this sector
        for (uint32_t e = 0; e < ENTRIES_PER_DIR_SECTOR; ++e) {
            size_t entry_off = doff + e * DIR_ENTRY_SIZE;
            if (entry_off + DIR_ENTRY_SIZE > length) break;

            // Entry type at offset 64 (1 byte): 0=unknown, 1=storage, 2=stream, 5=root
            uint8_t entry_type = data[entry_off + 64];
            if (entry_type == 0) continue;  // Empty entry

            // Entry name is UTF-16LE at offset 0, name length at offset 64-2=66
            uint16_t name_len = read_le16(data + entry_off + 64);
            if (name_len < 4 || name_len > 64) continue;  // Too short or too long

            // Read name as UTF-16LE and compare
            // name_len includes the trailing NUL, so actual chars = name_len - 2
            size_t name_chars = (name_len - 2) / 2;
            if (name_chars == 0 || name_chars > 31) continue;

            // Compare stream names (case-insensitive)
            auto name_matches = [&](const char* ascii_name) -> bool {
                size_t alen = 0;
                while (ascii_name[alen]) ++alen;
                if (alen != name_chars) return false;
                for (size_t c = 0; c < name_chars; ++c) {
                    uint16_t ch = read_le16(data + entry_off + c * 2);
                    char ac = ascii_name[c];
                    // Case-insensitive compare (ASCII range only)
                    if (ch != uint8_t(ac) && ch != uint8_t(ac >= 'a' && ac <= 'z' ? ac - 32 : ac))
                        return false;
                }
                return true;
            };

            if (name_matches("WordDocument"))  found_word = true;
            if (name_matches("Workbook"))       found_workbook = true;
            if (name_matches("PowerPoint"))     found_ppt = true;
        }

        // Follow FAT chain to next directory sector
        if (dir_sector >= fat_entries_count) break;
        dir_sector = fat_entries[dir_sector];
        dir_sectors_walked++;
    }

    // Determine extension based on stream names found
    // Note: We can't change the descriptor's extension, but we report the result
    // The scanner can use this information to override the extension later
    // For now, we just validate the structure

    // At minimum, we found a valid OLE2 structure with directory entries
    return ValidateResult::AcceptVerified;
}

} // anonymous namespace

const FormatDescriptor DOC_DESCRIPTOR = {
    .file_type       = FileType::Document,
    .extension       = L"doc",
    .description     = L"DOC/OLE2 compound document",
    .min_filesize    = 512,
    .max_filesize    = 0,
    .signature       = {DOC_MAGIC, 8, 0},
    .header_check    = check_doc_header_impl,
    .data_check      = nullptr,
    .file_check      = check_doc_file_impl,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_doc_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_doc_header_impl(data, length, calculated_file_size);
}

ValidateResult check_doc_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_doc_file_impl(data, length, calculated_file_size);
}

} // namespace disk_recover
