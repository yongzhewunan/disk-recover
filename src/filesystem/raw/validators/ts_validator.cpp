#include "../validators.hpp"
#include "../binary_reader.hpp"
#include "../evidence_weights.hpp"

namespace disk_recover {

std::optional<MatchResult> validate_ts(const uint8_t* data, size_t length) {
    // Minimum length for validation
    if (length < 188) return std::nullopt;

    // Phase 1: Sliding sync search (0-2048 bytes)
    // TS packets are 188 bytes, sync byte is 0x47
    int best_sync_count = 0;
    int best_sync_offset = -1;

    // Search window: 0 to 2048 bytes (allows for offset alignment)
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

    // Need at least 2 syncs for reasonable confidence
    if (best_sync_count < 2) {
        // Single sync - weak signal, but may be valid for short streams
        if (length >= 188 && data[0] == 0x47) {
            return MatchResult{
                {FileType::Video, L"mts", L"MPEG-TS"},
                30,  // Low confidence
                MatchFlags::HasHeader | MatchFlags::PartialMatch
            };
        }
        return std::nullopt;
    }

    float evidence = TS_WEIGHTS.header_weight;
    MatchFlags flags = MatchFlags::HasHeader;

    // Phase 2: Continuity counter validation (stronger evidence)
    int valid_packets = 0;
    int last_pid = -1;
    int last_continuity = -1;

    for (int packet = 0; packet < best_sync_count; ++packet) {
        size_t pos = best_sync_offset + packet * 188;

        // TS header structure:
        // Byte 0: sync (0x47)
        // Byte 1: error_indicator(1) + payload_start(1) + PID_high(5)
        // Byte 2: PID_low(8)
        // Byte 3: scrambling(2) + adaptation(2) + continuity(4)

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

    // Phase 3: Calculate confidence
    // Sync count evidence
    if (best_sync_count == 3) {
        evidence += 20.0f;
    } else if (best_sync_count == 2) {
        evidence += 10.0f;
    }

    // Valid packet evidence
    if (valid_packets == best_sync_count) {
        evidence += TS_WEIGHTS.container_weight;
        flags = flags | MatchFlags::ContainerParsed;
    } else if (valid_packets >= 2) {
        evidence += TS_WEIGHTS.container_weight * 0.5f;
    }

    flags = flags | MatchFlags::DeepValidated;

    return MatchResult{
        {FileType::Video, L"mts", L"MPEG-TS/AVCHD"},
        normalize_confidence(evidence, TS_WEIGHTS),
        flags
    };
}

} // namespace disk_recover