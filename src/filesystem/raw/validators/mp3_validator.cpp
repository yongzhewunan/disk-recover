#include "mp3_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// MP3 signature: MPEG audio frame sync
// First byte is always 0xFF, second byte has bits 7-5 = 111 (0xE0 mask)
// Common patterns: 0xFF 0xFB (MPEG1 Layer3), 0xFF 0xF3 (MPEG2 Layer3), 0xFF 0xF2 (MPEG2.5 Layer3)
static const uint8_t MP3_MAGIC[] = {0xFF, 0xE0};  // Pattern: sync + minimum version/layer bits
static const uint8_t MP3_MASK[] = {0xFF, 0xE0};   // Mask: only check sync word bits

// ID3v2 tag header: "ID3" followed by version (2 bytes) and flags
static const uint8_t ID3_MAGIC[] = {0x49, 0x44, 0x33};  // "ID3"

// ============================================================================
// MP3 Three-Phase Validator
//
// MP3 files consist of a sequence of MPEG audio frames, each with a 4-byte header.
// The signature is ambiguous (0xFF can start many formats), so header_check must
// be very strict — verify full frame header fields, not just sync bytes.
//
// Phase 1 (header_check): Verify 11-bit sync + valid MPEG version (1, 2, 2.5)
//   + valid layer (I, II, III) + valid bitrate index (not 0 or 15)
//   + valid sample rate index (not 3). Return AcceptHeader.
//
// Phase 2 (data_check): Validate 5+ consecutive MPEG frames with consistent
//   version/layer/sample rate. Parse XING/VBRI header for frame count if present.
//   Return AcceptStructure after 5+ consistent frames.
//   If XING header found with total bytes, set calculated_file_size and return AcceptVerified.
//
// Reference: ISO/IEC 11172-3 (MPEG-1 Audio), ISO/IEC 13818-3 (MPEG-2 Audio).
// ============================================================================

// MPEG audio version index → actual version
// bits [4:3] of 2nd byte: 0=MPEG2.5, 1=reserved, 2=MPEG2, 3=MPEG1
static const int MPEG_VERSION[] = {25, -1, 20, 10};  // 2.5, reserved, 2.0, 1.0

// MPEG layer index
// bits [2:1] of 2nd byte: 0=reserved, 1=LayerIII, 2=LayerII, 3=LayerI
static const int MPEG_LAYER[] = {-1, 3, 2, 1};

// Bitrate table (kbps) — indexed by [version][layer][bitrate_index]
// version: 0=MPEG1, 1=MPEG2/2.5
// layer: 0=I, 1=II, 2=III
static const int BITRATE_TABLE[2][3][16] = {
    // MPEG1
    {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1},  // Layer I
        {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, -1},  // Layer II
        {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, -1},  // Layer III
    },
    // MPEG2/2.5
    {
        {0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, -1},  // Layer I
        {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1},  // Layer II
        {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1},  // Layer III
    },
};

// Sample rate table (Hz) — indexed by [version][sr_index]
// version: 0=MPEG1, 1=MPEG2, 2=MPEG2.5
static const int SAMPLE_RATE_TABLE[3][4] = {
    {44100, 48000, 32000, -1},  // MPEG1
    {22050, 24000, 16000, -1},  // MPEG2
    {11025, 12000,  8000, -1},  // MPEG2.5
};

// Samples per frame — indexed by [version_idx][layer_idx]
// version_idx: 0=MPEG1, 1=MPEG2/2.5
// layer_idx: 0=I, 1=II, 2=III
static const int SAMPLES_PER_FRAME[2][3] = {
    {384, 1152, 1152},  // MPEG1: LayerI, LayerII, LayerIII
    {384, 1152,  576},  // MPEG2/2.5: LayerI, LayerII, LayerIII
};

// ── Parse a single MPEG frame header ──
// Returns frame size in bytes, or 0 if invalid.
// Sets out_version (10, 20, 25), out_layer (1, 2, 3), out_sample_rate.
struct FrameInfo {
    int version;       // 10, 20, 25
    int layer;         // 1, 2, 3
    int sample_rate;   // Hz
    int bitrate;       // kbps
    uint32_t frame_size;  // bytes
    bool padding;
};

static bool parse_mpeg_frame(const uint8_t* data, size_t length, size_t offset, FrameInfo& info) {
    if (offset + 4 > length) return false;

    uint8_t b0 = data[offset];
    uint8_t b1 = data[offset + 1];

    // 11-bit sync: all 1s
    if (b0 != 0xFF) return false;
    if ((b1 & 0xE0) != 0xE0) return false;

    // Version index (bits 4-3 of b1)
    int ver_idx = (b1 >> 3) & 0x03;
    if (ver_idx == 1) return false;  // Reserved version
    info.version = MPEG_VERSION[ver_idx];
    if (info.version < 0) return false;

    // Layer index (bits 2-1 of b1)
    int layer_idx = (b1 >> 1) & 0x03;
    if (layer_idx == 0) return false;  // Reserved layer
    info.layer = MPEG_LAYER[layer_idx];
    if (info.layer < 0) return false;

    // Protection bit (bit 0 of b1) — 1=has CRC, 0=no CRC
    // (not needed for validation, just for frame size calc)

    uint8_t b2 = data[offset + 2];

    // Bitrate index (bits 7-4 of b2)
    int bitrate_idx = (b2 >> 4) & 0x0F;
    if (bitrate_idx == 0 || bitrate_idx == 15) return false;  // Invalid

    // Sample rate index (bits 3-2 of b2)
    int sr_idx = (b2 >> 2) & 0x03;
    if (sr_idx == 3) return false;  // Reserved

    // Padding bit (bit 1 of b2)
    info.padding = (b2 >> 1) & 0x01;

    // Private bit (bit 0 of b2) — ignored

    // Look up bitrate
    int ver_table = (info.version == 10) ? 0 : 1;  // 0=MPEG1, 1=MPEG2/2.5
    int layer_table = info.layer - 1;  // 0=I, 1=II, 2=III
    info.bitrate = BITRATE_TABLE[ver_table][layer_table][bitrate_idx];
    if (info.bitrate <= 0) return false;

    // Look up sample rate
    int sr_table = (info.version == 10) ? 0 : (info.version == 20) ? 1 : 2;
    info.sample_rate = SAMPLE_RATE_TABLE[sr_table][sr_idx];
    if (info.sample_rate <= 0) return false;

    // Calculate frame size
    if (info.layer == 1) {
        // Layer I: frame_size = (12 * bitrate * 1000 / sample_rate + padding) * 4
        info.frame_size = (12 * info.bitrate * 1000 / info.sample_rate + info.padding) * 4;
    } else {
        // Layer II/III: frame_size = 144 * bitrate * 1000 / sample_rate + padding
        info.frame_size = 144 * info.bitrate * 1000 / info.sample_rate + info.padding;
    }

    // Frame size must be reasonable (at least 4 bytes for header, max 4KB for Layer I)
    if (info.frame_size < 4 || info.frame_size > 4096) return false;

    return true;
}

// ── Check for XING/VBRI header ──
// XING header is in the first MPEG frame's side information area.
// For MPEG1 Layer III: starts at offset 36 from frame start (32 side info + 4 header)
// For MPEG2/2.5 Layer III: starts at offset 21 from frame start (17 side info + 4 header)
// Returns total bytes if XING header found with TOC, 0 otherwise.
static uint64_t check_xing_header(const uint8_t* data, size_t length, size_t frame_offset, const FrameInfo& first_frame) {
    // Only valid for Layer III
    if (first_frame.layer != 3) return 0;

    size_t xing_offset;
    if (first_frame.version == 10) {
        // MPEG1: side info is 32 bytes, XING at offset 36
        xing_offset = frame_offset + 36;
    } else {
        // MPEG2/2.5: side info is 17 bytes, XING at offset 21
        xing_offset = frame_offset + 21;
    }

    if (xing_offset + 8 > length) return 0;

    // Check for "Xing" or "Info" tag
    if (has_str(data, length, xing_offset, "Xing") ||
        has_str(data, length, xing_offset, "Info")) {
        // Flags at offset +4
        uint32_t flags = read_be32(data + xing_offset + 4);

        // Flag bit 1: number of frames
        if (flags & 0x01) {
            if (xing_offset + 12 > length) return 0;
            uint32_t num_frames = read_be32(data + xing_offset + 8);

            // Calculate total file size from frame count
            if (num_frames > 0 && num_frames < 10000000) {
                int ver_table = (first_frame.version == 10) ? 0 : 1;
                int samples = SAMPLES_PER_FRAME[ver_table][2];  // Layer III = index 2
                double total_samples = double(num_frames) * samples;
                double total_bytes = total_samples * first_frame.bitrate * 1000.0 / (8.0 * first_frame.sample_rate);
                return static_cast<uint64_t>(total_bytes) + first_frame.frame_size;  // Add first frame for XING overhead
            }
        }

        // Flag bit 2: total bytes in file
        if (flags & 0x02) {
            size_t bytes_offset = xing_offset + 8;
            if (flags & 0x01) bytes_offset += 4;  // Skip frames field
            if (bytes_offset + 4 > length) return 0;
            uint32_t total_bytes = read_be32(data + bytes_offset);
            if (total_bytes > 0 && total_bytes < 200 * 1024 * 1024) {
                return total_bytes;
            }
        }

        return 0;  // XING found but no size info
    }

    // Check for VBRI header (Fraunhofer encoder)
    // VBRI is always at offset 36 from frame start (MPEG1 only)
    if (first_frame.version == 10) {
        size_t vbri_offset = frame_offset + 36;
        if (vbri_offset + 26 > length) return 0;

        if (has_str(data, length, vbri_offset, "VBRI")) {
            // VBRI structure: "VBRI" (4), version (2), delay (2), quality (2),
            //   bytes (4), frames (4), num_entries (2), ...
            uint32_t total_bytes = read_be32(data + vbri_offset + 14);
            if (total_bytes > 0 && total_bytes < 200 * 1024 * 1024) {
                return total_bytes;
            }
        }
    }

    return 0;
}

// ── Skip ID3v2 tag if present ──
// Returns the offset past the ID3 tag, or 0 if no ID3 tag.
static size_t skip_id3_tag(const uint8_t* data, size_t length) {
    if (length < 10) return 0;
    if (data[0] != 0x49 || data[1] != 0x44 || data[2] != 0x33) return 0;  // Not "ID3"

    // ID3v2 header: "ID3" (3), version (2), flags (1), size (4 syncsafe int)
    uint8_t flags = data[5];

    // Size is stored as 4-byte syncsafe integer (each byte < 128)
    size_t id3_size = ((data[6] & 0x7F) << 21) |
                      ((data[7] & 0x7F) << 14) |
                      ((data[8] & 0x7F) << 7) |
                       (data[9] & 0x7F);

    // Total tag size = 10 (header) + id3_size
    size_t total = 10 + id3_size;

    // If footer bit is set (flags bit 4), add 10 more bytes
    if (flags & 0x10) total += 10;

    // Sanity check: ID3 tags shouldn't be > 1MB
    if (total > 1024 * 1024) return 0;

    return total;
}

// ── Phase 1: Header check ──
ValidateResult check_mp3_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    if (length < 4) return ValidateResult::Reject;

    // Skip ID3v2 tag if present — many MP3 files start with ID3 metadata
    size_t frame_offset = skip_id3_tag(data, length);
    if (frame_offset > 0 && frame_offset + 4 > length) {
        // ID3 tag takes up entire buffer — can't check first frame
        // Still accept as MP3 since we confirmed ID3 header
        calculated_file_size = 0;
        return ValidateResult::AcceptHeader;
    }

    // If no ID3 tag, frame_offset is 0 — check sync word directly
    // (the masked signature already verified 0xFF 0xE0 match)
    FrameInfo first_frame;
    if (!parse_mpeg_frame(data, length, frame_offset, first_frame)) {
        // If we have an ID3 tag but can't parse the first frame,
        // still accept as MP3 (ID3 is definitive for MP3)
        if (frame_offset > 0) {
            calculated_file_size = 0;
            return ValidateResult::AcceptHeader;
        }
        return ValidateResult::Reject;
    }

    calculated_file_size = 0;  // Size unknown from single frame
    return ValidateResult::AcceptHeader;
}

// ── Phase 2: Data check ──
// Validate 5+ consecutive MPEG frames with consistent version/layer/sample rate.
// Parse XING/VBRI header for frame count if present.
ValidateResult check_mp3_data_impl(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    // For the first block, start from offset 0 in the file
    // For subsequent blocks, try to find frame sync
    size_t pos = 0;
    FrameInfo first_frame;
    int consistent_frames = 0;

    if (offset_in_file == 0) {
        // Skip ID3v2 tag if present
        size_t id3_skip = skip_id3_tag(data, length);

        // Parse first frame (after ID3 tag if present)
        if (!parse_mpeg_frame(data, length, id3_skip, first_frame)) {
            // If we have an ID3 tag, accept as MP3 even without valid first frame
            // (ID3 is definitive for MP3 classification)
            if (id3_skip > 0) return ValidateResult::AcceptHeader;
            return ValidateResult::Reject;
        }
        consistent_frames = 1;

        // Check for XING/VBRI header
        uint64_t xing_size = check_xing_header(data, length, id3_skip, first_frame);
        if (xing_size > 0) {
            calculated_file_size = xing_size;
            return ValidateResult::AcceptVerified;
        }

        pos = id3_skip + first_frame.frame_size;
    } else {
        // For subsequent blocks, try to find a valid frame
        // Scan for sync word
        bool found = false;
        for (size_t i = 0; i + 4 <= length && i < 4096; ++i) {
            if (parse_mpeg_frame(data, length, i, first_frame)) {
                pos = i + first_frame.frame_size;
                consistent_frames = 1;
                found = true;
                break;
            }
        }
        if (!found) return ValidateResult::AcceptHeader;  // No valid frame in this block
    }

    // Walk consecutive frames - need 5+ for AcceptStructure (reduced false positives)
    constexpr int MIN_FRAMES_FOR_STRUCTURE = 5;
    const int MAX_FRAMES = 15;
    while (consistent_frames < MAX_FRAMES && pos + 4 <= length) {
        FrameInfo frame;
        if (!parse_mpeg_frame(data, length, pos, frame)) break;

        // Check consistency: version, layer, sample rate must match
        if (frame.version != first_frame.version ||
            frame.layer != first_frame.layer ||
            frame.sample_rate != first_frame.sample_rate) {
            break;
        }

        consistent_frames++;
        pos += frame.frame_size;
    }

    if (consistent_frames >= MIN_FRAMES_FOR_STRUCTURE) {
        return ValidateResult::AcceptStructure;
    }

    // Not enough consistent frames yet — keep carving
    return ValidateResult::AcceptHeader;
}

} // anonymous namespace

const FormatDescriptor MP3_DESCRIPTOR = {
    .file_type       = FileType::Audio,
    .extension       = L"mp3",
    .description     = L"MP3 audio",
    .min_filesize    = 128,
    .max_filesize    = 0,
    .signature       = {MP3_MAGIC, MP3_MASK, 2, 0, 0x49},  // 0x49 = 'I' for ID3 tag
    .header_check    = check_mp3_header_impl,
    .data_check      = check_mp3_data_impl,
    .file_check      = nullptr,
    .enabled_by_default = true,
};

// Public interface
ValidateResult check_mp3_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_mp3_header_impl(data, length, calculated_file_size);
}

ValidateResult check_mp3_data(const uint8_t* data, size_t length, uint64_t offset_in_file, uint64_t& calculated_file_size) {
    return check_mp3_data_impl(data, length, offset_in_file, calculated_file_size);
}

} // namespace disk_recover
