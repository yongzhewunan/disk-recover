#include "ts_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

#include <algorithm>

namespace disk_recover {
namespace {

// MPEG-TS sync byte
static const uint8_t TS_MAGIC[] = {0x47};

// ── Phase 1: Header check ──
// Detects M2TS (192-byte packets with 4-byte timestamp prefix) vs MTS (188-byte packets).
// Uses sliding sync search for 0x47 at 188-byte intervals.
// Requires minimum 8 consecutive syncs for AcceptStructure (reduced false positives).
// Includes PID consistency check to reduce false positives.
// Returns AcceptStructure if periodicity and PID patterns confirmed.
ValidateResult check_ts_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Minimum length for validation (need multiple packets for confidence)
    if (length < 188) return ValidateResult::Reject;

    calculated_file_size = 0;  // No size in MPEG-TS structure

    // Phase 0: Detect M2TS vs MTS format
    // M2TS: 4-byte timestamp prefix before each 188-byte TS packet (192 bytes total)
    // MTS:  Standard 188-byte TS packets
    bool is_m2ts = false;
    int sync_offset = 0;
    int packet_size = 188;

    // Check for M2TS pattern: 4-byte timestamp + 0x47 sync at offset 4
    // Need at least 2 packets to confirm pattern
    if (length >= 384) {
        // M2TS pattern: data[4] == 0x47, data[196] == 0x47 (192-byte intervals)
        if (data[4] == 0x47 && data[196] == 0x47) {
            // Verify it's not a false positive by checking one more packet
            if (length >= 576 && data[388] == 0x47) {
                is_m2ts = true;
                sync_offset = 4;
                packet_size = 192;
            } else if (data[196] == 0x47) {
                // Two packets match M2TS pattern - likely M2TS
                is_m2ts = true;
                sync_offset = 4;
                packet_size = 192;
            }
        }
    }

    // Phase 1: Enhanced sliding sync search
    // For MTS (not M2TS), search for best sync offset
    int best_sync_count = 0;
    int best_sync_offset = sync_offset;

    if (!is_m2ts) {
        // For MTS, search window: 0 to 2048 bytes
        int max_search_offset = length >= 2048 + 564 ? 2048 : static_cast<int>(length - 564);
        if (max_search_offset < 0) max_search_offset = 0;

        for (int offset = 0; offset <= max_search_offset; ++offset) {
            int sync_count = 0;

            // Check for sync bytes at 188-byte intervals
            for (int packet = 0; packet < 3 && offset + packet * 188 + 188 <= static_cast<int>(length); ++packet) {
                if (data[offset + packet * 188] == 0x47) {
                    ++sync_count;
                }
            }

            if (sync_count > best_sync_count) {
                best_sync_count = sync_count;
                best_sync_offset = offset;
            }

            // Early exit if we found 3 syncs
            if (best_sync_count == 3) break;
        }
    } else {
        // For M2TS, count syncs at 192-byte intervals starting at offset 4
        for (int packet = 0; packet < 10 && sync_offset + packet * packet_size < static_cast<int>(length); ++packet) {
            if (data[sync_offset + packet * packet_size] == 0x47) {
                ++best_sync_count;
            }
        }
    }

    // Phase 2: Enhanced periodicity check (8+ packets for high confidence)
    // This is critical for distinguishing TS from random data
    constexpr int MIN_PACKETS_FOR_STRUCTURE = 8;  // Increased from 5 to reduce false positives
    constexpr int MIN_PACKETS_FOR_HEADER = 5;    // Increased from 3 to reduce false positives
    constexpr int MAX_PACKETS_TO_SCAN = 15;

    int sync_count_extended = 0;
    for (int packet = 0; packet < MAX_PACKETS_TO_SCAN; ++packet) {
        size_t pos = best_sync_offset + packet * packet_size;
        if (pos >= length) break;
        if (data[pos] == 0x47) {
            ++sync_count_extended;
        }
    }

    // Phase 3: PID consistency check
    // Known valid PIDs: 0x0000 (PAT), 0x0001 (CAT), 0x0010-0x001F (NIT/PMT range), 0x1FFF (null packet)
    // Real TS streams typically have consistent PIDs or known patterns
    bool has_valid_pid_pattern = false;
    int valid_pid_count = 0;
    int unique_pids = 0;
    int last_pid = -1;
    constexpr int KNOWN_PIDS[] = {0x0000, 0x0001, 0x0010, 0x0011, 0x0012, 0x001F, 0x1FFF};

    for (int packet = 0; packet < sync_count_extended && best_sync_offset + packet * packet_size + 4 <= static_cast<int>(length); ++packet) {
        size_t pos = best_sync_offset + packet * packet_size;
        if (data[pos] != 0x47) continue;

        // Parse PID from TS header (bytes 1-2)
        uint8_t byte1 = data[pos + 1];
        uint8_t byte2 = data[pos + 2];
        int pid = ((byte1 & 0x1F) << 8) | byte2;

        // Check for transport error indicator (bit 7 of byte1)
        bool has_error = (byte1 & 0x80) != 0;
        if (!has_error) {
            ++valid_pid_count;

            // Check if PID is known pattern
            for (int known_pid : KNOWN_PIDS) {
                if (pid == known_pid) {
                    has_valid_pid_pattern = true;
                    break;
                }
            }

            // Count unique PIDs (excluding null packets 0x1FFF)
            if (pid != last_pid && pid != 0x1FFF) {
                ++unique_pids;
                last_pid = pid;
            }
        }
    }

    // Require minimum sync count for acceptance
    // Less than MIN_PACKETS_FOR_HEADER is insufficient (reject false positives)
    if (sync_count_extended < MIN_PACKETS_FOR_HEADER) {
        return ValidateResult::Reject;  // Insufficient syncs - likely false positive
    }

    // Check PID pattern quality
    // Good patterns: known PIDs present, or few unique PIDs (consistent stream)
    bool good_pid_quality = has_valid_pid_pattern || (unique_pids <= 3 && valid_pid_count >= sync_count_extended - 1);

    // Periodicity confirmed with 8+ packets AND good PID pattern
    if (sync_count_extended >= MIN_PACKETS_FOR_STRUCTURE && good_pid_quality) {
        return ValidateResult::AcceptStructure;
    }

    // 5-7 packets with good PID pattern: partial confidence
    if (sync_count_extended >= MIN_PACKETS_FOR_HEADER && good_pid_quality) {
        return ValidateResult::AcceptHeader;
    }

    // 5-7 packets but poor PID pattern: still AcceptHeader but lower confidence
    // This allows damaged TS streams to be detected
    if (sync_count_extended >= MIN_PACKETS_FOR_HEADER) {
        return ValidateResult::AcceptHeader;
    }

    return ValidateResult::Reject;
}

// ── Phase 2: Data check ──
// Validates packet continuity counters as more data arrives.
// Returns AcceptStructure if counters are consistent.
ValidateResult check_ts_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    (void)offset_in_file;  // Not used for streaming format

    calculated_file_size = 0;  // No size in MPEG-TS structure

    // Determine packet size from data pattern
    int packet_size = 188;
    int sync_offset = 0;

    // Check for M2TS pattern
    if (length >= 384 && data[4] == 0x47 && data[196] == 0x47) {
        packet_size = 192;
        sync_offset = 4;
    }

    // Validate continuity counters across packets
    int valid_packets = 0;
    int last_pid = -1;
    int last_continuity = -1;

    constexpr int MAX_PACKETS_TO_VALIDATE = 10;
    int packets_to_validate = (std::min)(static_cast<int>(length / packet_size), MAX_PACKETS_TO_VALIDATE);

    for (int packet = 0; packet < packets_to_validate; ++packet) {
        size_t pos = sync_offset + packet * packet_size;

        // TS header structure:
        // Byte 0: sync (0x47)
        // Byte 1: error_indicator(1) + payload_start(1) + PID_high(5)
        // Byte 2: PID_low(8)
        // Byte 3: scrambling(2) + adaptation(2) + continuity(4)

        if (pos + 4 > length) break;

        // Verify sync byte
        if (data[pos] != 0x47) continue;

        uint8_t byte1 = data[pos + 1];
        uint8_t byte2 = data[pos + 2];
        uint8_t byte3 = data[pos + 3];

        // Check transport error indicator
        bool has_error = (byte1 & 0x80) != 0;
        if (has_error) continue;

        // Get PID (13-bit value)
        int pid = ((byte1 & 0x1F) << 8) | byte2;

        // Get adaptation field control (2-bit value)
        uint8_t adaptation = (byte3 >> 4) & 0x03;

        // Get continuity counter (4-bit value)
        uint8_t continuity = byte3 & 0x0F;

        // Validate adaptation field control (0-3)
        if (adaptation > 3) continue;  // Invalid

        // Validate adaptation field length if present
        if (adaptation >= 2 && pos + 5 <= length) {
            uint8_t adaptation_len = data[pos + 4];
            if (adaptation_len > 183) continue;  // Invalid length
        }

        // Validate continuity counter for same PID
        if (pid == last_pid && pid != 0x1FFF) {  // Exclude null packets
            // Continuity should increment (or stay same for adaptation-only)
            int expected = (last_continuity + 1) % 16;
            if (adaptation == 1 || adaptation == 3) {  // Payload present
                if (continuity != expected) {
                    // Continuity mismatch — could be packet loss, not a rejection
                    // Still count as valid packet but don't boost confidence
                }
            }
            // Adaptation-only packets can repeat continuity
        }

        last_pid = pid;
        last_continuity = continuity;
        ++valid_packets;
    }

    // If we validated at least some packets with consistent structure
    if (valid_packets >= 2) {
        return ValidateResult::AcceptStructure;
    }

    // Single valid packet: AcceptHeader
    if (valid_packets >= 1) {
        return ValidateResult::AcceptHeader;
    }

    // No valid packets found - reject
    return ValidateResult::Reject;
}

} // anonymous namespace

const FormatDescriptor TS_DESCRIPTOR = {
    .file_type       = FileType::Video,
    .extension       = L"mts",
    .description     = L"MPEG-TS/AVCHD video",
    .min_filesize    = 188,
    .max_filesize    = 0,
    .signature       = {TS_MAGIC, 1, 0},
    .header_check    = check_ts_header_impl,
    .data_check      = nullptr,  // TS is streaming format, no embedded size - skip progressive carving to save ~50MB I/O per false positive
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_ts_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_ts_header_impl(data, length, calculated_file_size);
}

ValidateResult check_ts_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_ts_data_impl(data, length, offset_in_file, calculated_file_size);
}

} // namespace disk_recover
