#include "flac_validator.hpp"
#include "binary_reader.hpp"
#include "format_registry.hpp"

namespace disk_recover {
namespace {

// FLAC signature: "fLaC"
static const uint8_t FLAC_MAGIC[] = {0x66, 0x4C, 0x61, 0x43};  // 'f', 'L', 'a', 'C'

// ============================================================================
// FLAC Three-Phase Validator
//
// FLAC files start with "fLaC" signature followed by one or more metadata blocks,
// then the audio frames. The first metadata block must be STREAMINFO.
//
// Phase 1 (header_check): Verify "fLaC" signature + first metadata block header.
//   The first block must be STREAMINFO (type=0). Return AcceptStructure.
//
// Phase 2 (file_check): Walk all metadata blocks. Parse STREAMINFO block (34 bytes)
//   for: sample rate, channels, bit depth, total samples.
//   Set calculated_file_size from total audio data + metadata overhead.
//   Return AcceptVerified.
//
// Reference: FLAC format specification (https://xiph.org/flac/format.html).
// ============================================================================

// Metadata block header: 4 bytes
// - Bit 0: last-metadata-block flag
// - Bits 1-6: block type (0=STREAMINFO, 1=PADDING, 2=APPLICATION, 3=SEEKTABLE, 4=VORBIS_COMMENT, 5=CUESHEET, 6=PICTURE)
// - Bits 8-31: length of block data (24 bits, big-endian)

// STREAMINFO block: 34 bytes
// - Bytes 0-1: minimum block size (samples)
// - Bytes 2-3: maximum block size (samples)
// - Bytes 4-7: minimum frame size (bytes)
// - Bytes 8-11: maximum frame size (bytes)
// - Bytes 12-15: sample rate (20 bits), channels (3 bits), bits per sample (5 bits), total samples (36 bits, upper 4 bits)
// - Bytes 16-19: total samples (lower 32 bits)
// - Bytes 20-33: MD5 signature (16 bytes)

// ── Phase 1: Header check ──
ValidateResult check_flac_header_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Minimum: 4 (signature) + 4 (block header) + 34 (STREAMINFO) = 42 bytes
    if (length < 42) return ValidateResult::Reject;

    // Verify "fLaC" signature
    for (int i = 0; i < 4; ++i) {
        if (data[i] != FLAC_MAGIC[i]) return ValidateResult::Reject;
    }

    // First metadata block header at offset 4
    uint8_t block_header = data[4];
    bool is_last = (block_header & 0x80) != 0;
    uint8_t block_type = block_header & 0x7F;

    // First block must be STREAMINFO (type 0)
    if (block_type != 0) return ValidateResult::Reject;

    // Block length (24 bits, big-endian) at offset 5-7
    uint32_t block_length = (uint32_t(data[5]) << 16) | (uint32_t(data[6]) << 8) | uint32_t(data[7]);

    // STREAMINFO must be exactly 34 bytes
    if (block_length != 34) return ValidateResult::Reject;

    // Validate STREAMINFO fields
    // Minimum block size (bytes 8-9): must be >= 16
    uint16_t min_block_size = read_be16(data + 8);
    if (min_block_size < 16) return ValidateResult::Reject;

    // Maximum block size (bytes 10-11): must be >= min_block_size
    uint16_t max_block_size = read_be16(data + 10);
    if (max_block_size < min_block_size) return ValidateResult::Reject;

    // Sample rate (bits 12-15): 20-bit sample rate at bits 12:4 to 14:3
    // Actually: bytes 12-15 contain: sample_rate (20 bits), channels (3 bits), bits_per_sample (5 bits), total_samples_upper (4 bits)
    // Sample rate is in the upper 20 bits of bytes 12-14 (bits 31:12 of the 32-bit word)
    uint32_t sample_rate_fields = read_be32(data + 12);
    uint32_t sample_rate = (sample_rate_fields >> 12) & 0xFFFFF;  // 20 bits

    // Sample rate must be valid (non-zero, reasonable range)
    if (sample_rate == 0 || sample_rate > 655350) return ValidateResult::Reject;

    // Channels: bits 11:9 of sample_rate_fields (3 bits, 1-8)
    uint8_t channels = ((sample_rate_fields >> 9) & 0x07) + 1;  // 0 = 1 channel, 7 = 8 channels
    if (channels < 1 || channels > 8) return ValidateResult::Reject;

    // Bits per sample: bits 8:4 of sample_rate_fields (5 bits, 4-32)
    uint8_t bits_per_sample = ((sample_rate_fields >> 4) & 0x1F) + 1;  // 0 = 4 bits, 31 = 32 bits
    if (bits_per_sample < 4 || bits_per_sample > 32) return ValidateResult::Reject;

    calculated_file_size = 0;  // Size not yet determined
    return ValidateResult::AcceptStructure;
}

// ── Phase 2: File check ──
// Walk all metadata blocks and calculate file size from total samples.
ValidateResult check_flac_file_impl(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    // Re-verify signature
    if (length < 42) return ValidateResult::Reject;
    for (int i = 0; i < 4; ++i) {
        if (data[i] != FLAC_MAGIC[i]) return ValidateResult::Reject;
    }

    // Walk metadata blocks
    size_t pos = 4;  // Start after signature
    bool found_streaminfo = false;
    uint64_t total_samples = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    uint8_t bits_per_sample = 0;
    uint64_t metadata_bytes = 4;  // Start with signature

    while (pos + 4 <= length) {
        uint8_t block_header = data[pos];
        bool is_last = (block_header & 0x80) != 0;
        uint8_t block_type = block_header & 0x7F;

        // Block length (24 bits)
        uint32_t block_length = (uint32_t(data[pos + 1]) << 16) |
                                (uint32_t(data[pos + 2]) << 8) |
                                uint32_t(data[pos + 3]);

        // Sanity check block length
        if (block_length > 16 * 1024 * 1024) break;  // Max 16MB per block

        metadata_bytes += 4 + block_length;  // Header + data

        if (block_type == 0 && !found_streaminfo) {
            // STREAMINFO block
            if (pos + 4 + 34 <= length) {
                // Parse STREAMINFO
                uint32_t sample_rate_fields = read_be32(data + pos + 4 + 12);
                sample_rate = (sample_rate_fields >> 12) & 0xFFFFF;
                channels = ((sample_rate_fields >> 9) & 0x07) + 1;
                bits_per_sample = ((sample_rate_fields >> 4) & 0x1F) + 1;

                // Total samples: upper 4 bits in sample_rate_fields, lower 32 bits in next word
                uint64_t total_samples_upper = sample_rate_fields & 0x0F;
                uint32_t total_samples_lower = read_be32(data + pos + 4 + 16);
                total_samples = (total_samples_upper << 32) | total_samples_lower;

                found_streaminfo = true;
            }
        }

        pos += 4 + block_length;

        if (is_last) break;
    }

    if (!found_streaminfo) return ValidateResult::AcceptStructure;

    // Calculate file size from total samples
    // FLAC audio frame size varies, but we can estimate:
    // Average bytes per sample = (bits_per_sample * channels) / 8
    // FLAC compression ratio is typically 50-70% of original
    // For a rough estimate, use: total_samples * channels * bits_per_sample / 8 * 0.6
    // But this is unreliable — better to just report metadata_bytes as minimum

    if (total_samples > 0 && sample_rate > 0) {
        // Estimate audio data size
        // Uncompressed size = total_samples * channels * (bits_per_sample / 8)
        // FLAC typically achieves 50-70% compression
        uint64_t uncompressed_bytes = total_samples * channels * ((bits_per_sample + 7) / 8);
        uint64_t estimated_audio_bytes = uncompressed_bytes * 6 / 10;  // 60% compression ratio

        // Add some overhead for frame headers (typically 4-16 bytes per frame)
        // Average frame size is ~4096 samples, so num_frames = total_samples / 4096
        uint64_t num_frames = total_samples / 4096 + 1;
        uint64_t frame_header_bytes = num_frames * 8;

        calculated_file_size = metadata_bytes + estimated_audio_bytes + frame_header_bytes;
    } else {
        // Unknown total samples — can't calculate size
        calculated_file_size = 0;
    }

    return ValidateResult::AcceptVerified;
}

// Auto-registration with FormatRegistry
static const FormatDescriptor FLAC_DESCRIPTOR = {
    .file_type       = FileType::Audio,
    .extension       = L"flac",
    .description     = L"FLAC audio",
    .min_filesize    = 42,  // 4 signature + 4 block header + 34 STREAMINFO
    .max_filesize    = 0,
    .signature       = {FLAC_MAGIC, 4, 0},
    .header_check    = check_flac_header_impl,
    .data_check      = nullptr,
    .file_check      = check_flac_file_impl,
    .enabled_by_default = true,
};

static bool _flac_registered = []() {
    FormatRegistry::instance().register_format(FLAC_DESCRIPTOR);
    return true;
}();

} // anonymous namespace

// Public interface
ValidateResult check_flac_header(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_flac_header_impl(data, length, calculated_file_size);
}

ValidateResult check_flac_file(const uint8_t* data, size_t length, uint64_t& calculated_file_size) {
    return check_flac_file_impl(data, length, calculated_file_size);
}

} // namespace disk_recover
