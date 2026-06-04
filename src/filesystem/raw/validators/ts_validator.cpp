#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_ts(const uint8_t* data, size_t length) {
    // Minimum length for validation (need multiple packets for confidence)
    if (length < 188) return std::nullopt;

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
            for (int packet = 0; packet < 3 && offset + packet * 188 + 188 <= length; ++packet) {
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

    // Phase 2: Enhanced periodicity check (5-10 packets for high confidence)
    // This is critical for distinguishing TS from random data
    constexpr int MIN_PACKETS_FOR_HIGH_CONFIDENCE = 5;
    constexpr int MAX_PACKETS_TO_SCAN = 10;

    int sync_count_extended = 0;
    for (int packet = 0; packet < MAX_PACKETS_TO_SCAN; ++packet) {
        size_t pos = best_sync_offset + packet * packet_size;
        if (pos >= length) break;
        if (data[pos] == 0x47) {
            ++sync_count_extended;
        }
    }

    // Require minimum sync count for acceptance
    if (sync_count_extended < 2) {
        // Single sync - weak signal, may be false positive
        if (length >= 188 && data[0] == 0x47 && sync_count_extended == 1) {
            return MatchResult{
                {FileType::Video, L"mts", L"MPEG-TS"},
                25,  // Low confidence
                MatchFlags::HasHeader | MatchFlags::PartialMatch
            };
        }
        return std::nullopt;
    }

    float evidence = TS_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 3: Continuity counter validation (stronger evidence)
    int valid_packets = 0;
    int last_pid = -1;
    int last_continuity = -1;

    int packets_to_validate = (std::min)(sync_count_extended, MAX_PACKETS_TO_SCAN);
    for (int packet = 0; packet < packets_to_validate; ++packet) {
        size_t pos = best_sync_offset + packet * packet_size;

        // TS header structure:
        // Byte 0: sync (0x47)
        // Byte 1: error_indicator(1) + payload_start(1) + PID_high(5)
        // Byte 2: PID_low(8)
        // Byte 3: scrambling(2) + adaptation(2) + continuity(4)

        if (pos + 4 > length) break;

        uint8_t byte1 = data[pos + 1];
        uint8_t byte2 = data[pos + 2];
        uint8_t byte3 = data[pos + 3];

        // Check transport error indicator
        bool has_error = (byte1 & 0x80) != 0;
        if (has_error) {
            // Packet has error - still counts but less evidence
            continue;
        }

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

            // Check adaptation field flags
            if (pos + 5 + adaptation_len <= length) {
                uint8_t adapt_flags = data[pos + 5];
                // Random access indicator is a good sign
                if (adapt_flags & 0x40) {
                    evidence += 5.0f;
                }
            }
        }

        // Validate continuity counter for same PID
        if (pid == last_pid && pid != 0x1FFF) {  // Exclude null packets
            // Continuity should increment (or stay same for adaptation-only)
            int expected = (last_continuity + 1) % 16;
            if (adaptation == 1 || adaptation == 3) {  // Payload present
                if (continuity == expected) {
                    evidence += 10.0f;  // Continuity matches
                }
            }
            // Adaptation-only packets can repeat continuity
        }

        last_pid = pid;
        last_continuity = continuity;
        ++valid_packets;
    }

    // Phase 4: Calculate confidence with enhanced periodicity evidence
    // Sync count evidence - higher bar for high confidence
    if (sync_count_extended >= MIN_PACKETS_FOR_HIGH_CONFIDENCE) {
        evidence += 25.0f;
        flags |= MatchFlags::DeepValidated;
    } else if (sync_count_extended >= 3) {
        evidence += 15.0f;
    } else if (sync_count_extended >= 2) {
        evidence += 8.0f;
    }

    // Valid packet evidence
    if (valid_packets == sync_count_extended) {
        evidence += TS_WEIGHTS.container_weight;
        flags = flags | MatchFlags::ContainerParsed;
    } else if (valid_packets >= 2) {
        evidence += TS_WEIGHTS.container_weight * 0.5f;
    }

    // Return appropriate extension based on format
    if (is_m2ts) {
        return MatchResult{
            {FileType::Video, L"m2ts", L"Blu-ray M2TS"},
            normalize_confidence(evidence, TS_WEIGHTS),
            flags
        };
    } else {
        return MatchResult{
            {FileType::Video, L"mts", L"MPEG-TS/AVCHD"},
            normalize_confidence(evidence, TS_WEIGHTS),
            flags
        };
    }
}

} // namespace disk_recover