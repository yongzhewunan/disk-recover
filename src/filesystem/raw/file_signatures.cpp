#include "file_signatures.hpp"
#include "validators.hpp"
#include "evidence_weights.hpp"
#include <vector>
#include <algorithm>
#include <cstring>

namespace disk_recover {

// Legacy pattern definitions for backward compatibility
static const uint8_t JPEG_PAT[] = {0xFF, 0xD8, 0xFF};
static const uint8_t PNG_PAT[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t BMP_PAT[]  = {0x42, 0x4D};
static const uint8_t GIF_PAT[]  = {0x47, 0x49, 0x46, 0x38};
static const uint8_t TIFF_LE_PAT[] = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t TIFF_BE_PAT[] = {0x4D, 0x4D, 0x00, 0x2A};
static const uint8_t WEBP_PAT[] = {0x52, 0x49, 0x46, 0x46};
static const uint8_t MP4_PAT[]  = {0x66, 0x74, 0x79, 0x70};
static const uint8_t AVI_PAT[]  = {0x52, 0x49, 0x46, 0x46};
static const uint8_t MKV_PAT[]  = {0x1A, 0x45, 0xDF, 0xA3};
static const uint8_t WMV_PAT[]  = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11};
static const uint8_t FLV_PAT[]  = {0x46, 0x4C, 0x56};
static const uint8_t MOV_PAT[]  = {0x6D, 0x6F, 0x6F, 0x76};
static const uint8_t HEIC_PAT[] = {0x66, 0x74, 0x79, 0x70};
static const uint8_t MTS_PAT[]  = {0x47};

// RAW camera format signatures
static const uint8_t CR2_PAT[]  = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t NEF_PAT[]  = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t ARW_PAT[]  = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t DNG_PAT[]  = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t RW2_PAT[]  = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t ORF_PAT[]  = {0x49, 0x49, 0x52, 0x4F};

const std::vector<FileSignatures::SignatureEntry>& FileSignatures::entries() {
    static const std::vector<SignatureEntry> sigs = {
        {FileType::Image, L"jpg",  L"JPEG",     JPEG_PAT, 3, 0},
        {FileType::Image, L"png",  L"PNG",      PNG_PAT,  8, 0},
        {FileType::Image, L"bmp",  L"BMP",      BMP_PAT,  2, 0},
        {FileType::Image, L"gif",  L"GIF",      GIF_PAT,  4, 0},
        {FileType::Image, L"tiff", L"TIFF-LE",  TIFF_LE_PAT, 4, 0},
        {FileType::Image, L"tiff", L"TIFF-BE",  TIFF_BE_PAT, 4, 0},
        {FileType::Image, L"cr2",  L"CR2 (Canon RAW)",   CR2_PAT,  4, 0},
        {FileType::Image, L"nef",  L"NEF (Nikon RAW)",   NEF_PAT,  4, 0},
        {FileType::Image, L"arw",  L"ARW (Sony RAW)",    ARW_PAT,  4, 0},
        {FileType::Image, L"dng",  L"DNG (Adobe DNG)",   DNG_PAT,  4, 0},
        {FileType::Image, L"rw2",  L"RW2 (Panasonic)",   RW2_PAT,  4, 0},
        {FileType::Image, L"orf",  L"ORF (Olympus RAW)", ORF_PAT,  4, 0},
        {FileType::Image, L"heic", L"HEIC (Apple HEIF)", HEIC_PAT, 4, 4},
        {FileType::Video, L"mp4",  L"MP4",      MP4_PAT,  4, 4},
        {FileType::Video, L"webm", L"WebM",     MKV_PAT,  4, 0},
        {FileType::Video, L"avi",  L"AVI",      AVI_PAT,  4, 0},
        {FileType::Video, L"mkv",  L"MKV",      MKV_PAT,  4, 0},
        {FileType::Video, L"wmv",  L"WMV/ASF",  WMV_PAT, 8, 0},
        {FileType::Video, L"flv",  L"FLV",      FLV_PAT,  3, 0},
        {FileType::Video, L"mov",  L"MOV",      MOV_PAT,  4, 4},
        {FileType::Video, L"mts",  L"MTS (AVCHD)",  MTS_PAT, 1, 0},
        {FileType::Video, L"m2ts", L"M2TS (AVCHD)", MTS_PAT, 1, 0},
    };
    return sigs;
}

// Helper: validate BMP with strict DIB header checks
static std::optional<MatchResult> validate_bmp(const uint8_t* data, size_t length) {
    if (length < 54) return std::nullopt;
    if (data[0] != 0x42 || data[1] != 0x4D) return std::nullopt;

    // BMP header validation
    // Bytes 2-5: file size (little-endian)
    // Bytes 10-13: pixel data offset
    uint32_t file_size = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24);
    uint32_t data_offset = data[10] | (data[11] << 8) | (data[12] << 16) | (data[13] << 24);

    // Sanity checks
    if (file_size < 54 || data_offset < 54 || data_offset > file_size) {
        return MatchResult{
            {FileType::Image, L"bmp", L"BMP"},
            25,  // Low confidence - header present but values suspicious
            MatchFlags::HasHeader | MatchFlags::PartialMatch,
            0
        };
    }

    // ── DIB Header Validation ──
    // BITMAPINFOHEADER starts at offset 14
    // Bytes 14-17: DIB header size (must be 40 for standard BMP)
    if (length < 18) {
        return MatchResult{
            {FileType::Image, L"bmp", L"BMP"},
            40,
            MatchFlags::HasHeader | MatchFlags::PartialMatch,
            file_size
        };
    }

    uint32_t dib_header_size = data[14] | (data[15] << 8) | (data[16] << 16) | (data[17] << 24);

    // Valid DIB header sizes: 12 (OS/2 BITMAPCOREHEADER), 40 (BITMAPINFOHEADER),
    // 52, 56, 64, 108, 124 (various extended versions)
    // Most common is 40
    bool valid_dib_size = (dib_header_size == 12 || dib_header_size == 40 ||
                          dib_header_size == 52 || dib_header_size == 56 ||
                          dib_header_size == 64 || dib_header_size == 108 ||
                          dib_header_size == 124);

    float evidence = 50.0f;
    MatchFlags flags = MatchFlags::HasHeader;

    if (!valid_dib_size) {
        // Unusual DIB header size - reduce confidence
        evidence -= 15.0f;
        flags |= MatchFlags::PartialMatch;
    } else {
        evidence += 10.0f;  // Good DIB header size
    }

    // Validate image dimensions and other fields (for standard BITMAPINFOHEADER, size=40)
    if (length >= 54 && dib_header_size >= 40) {
        int32_t width = static_cast<int32_t>(data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24));
        int32_t height = static_cast<int32_t>(data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24));

        // BMP height can be negative (top-down DIB)
        int32_t abs_height = height < 0 ? -height : height;

        // Reasonable image dimensions (1 to 65535)
        if (width > 0 && width <= 65535 && abs_height > 0 && abs_height <= 65535) {
            evidence += 10.0f;
        } else {
            evidence -= 10.0f;
            flags |= MatchFlags::PartialMatch;
        }

        // ── Planes validation ──
        // Must be 1 for standard BMP
        uint16_t planes = data[26] | (data[27] << 8);
        if (planes == 1) {
            evidence += 5.0f;
        } else {
            evidence -= 10.0f;  // Invalid planes value
            flags |= MatchFlags::PartialMatch;
        }

        // ── Bits per pixel validation ──
        // Valid values: 1, 4, 8, 16, 24, 32
        uint16_t bits_per_pixel = data[28] | (data[29] << 8);
        bool valid_bpp = (bits_per_pixel == 1 || bits_per_pixel == 4 ||
                         bits_per_pixel == 8 || bits_per_pixel == 16 ||
                         bits_per_pixel == 24 || bits_per_pixel == 32);

        if (valid_bpp) {
            evidence += 10.0f;
        } else {
            evidence -= 15.0f;  // Invalid bits per pixel
            flags |= MatchFlags::PartialMatch;
        }

        // ── Compression validation ──
        // 0=BI_RGB, 1=BI_RLE8, 2=BI_RLE4, 3=BI_BITFIELDS, 4=BI_JPEG, 5=BI_PNG
        uint32_t compression = data[30] | (data[31] << 8) | (data[32] << 16) | (data[33] << 24);
        bool valid_compression = (compression <= 5);
        if (valid_compression) {
            evidence += 5.0f;
        } else {
            evidence -= 15.0f;  // Invalid compression
            flags |= MatchFlags::PartialMatch;
        }

        // ── Calculate expected file size for verification ──
        if (valid_bpp && width > 0 && abs_height > 0) {
            // Calculate row size (padded to 4-byte boundary)
            uint32_t row_size;
            if (bits_per_pixel == 1) {
                // 1-bit: 8 pixels per byte
                row_size = ((width + 7) / 8 + 3) / 4 * 4;
            } else if (bits_per_pixel == 4) {
                // 4-bit: 2 pixels per byte
                row_size = ((width + 1) / 2 + 3) / 4 * 4;
            } else if (bits_per_pixel == 8) {
                // 8-bit: 1 byte per pixel (or palette index)
                row_size = (width + 3) / 4 * 4;
            } else {
                // 16, 24, 32-bit: bytes_per_pixel * width, padded
                uint32_t bytes_per_pixel = bits_per_pixel / 8;
                row_size = (width * bytes_per_pixel + 3) / 4 * 4;
            }

            uint32_t expected_data_size = row_size * abs_height;

            // Add palette size for paletted images
            uint32_t palette_size = 0;
            if (bits_per_pixel <= 8) {
                // Palette has 2^bpp entries, each 4 bytes (RGBQUAD)
                palette_size = (1 << bits_per_pixel) * 4;
            }

            uint32_t expected_file_size = 14 + dib_header_size + palette_size + expected_data_size;

            // Allow some tolerance for file size
            if (file_size > 0 && expected_file_size > 0) {
                uint32_t diff = (expected_file_size > file_size) ?
                                (expected_file_size - file_size) : (file_size - expected_file_size);
                if (diff < file_size / 10) {  // Within 10%
                    evidence += 15.0f;  // Size matches well
                } else if (diff < file_size / 4) {  // Within 25%
                    evidence += 5.0f;   // Size somewhat matches
                }
            }
        }
    }

    flags |= MatchFlags::DeepValidated;

    return MatchResult{
        {FileType::Image, L"bmp", L"BMP"},
        static_cast<uint8_t>(evidence > 100 ? 100 : (evidence < 20 ? 20 : evidence)),
        flags,
        file_size  // verified_file_size from BMP header
    };
}

// Helper: validate WMV/ASF
static std::optional<MatchResult> validate_wmv(const uint8_t* data, size_t length) {
    // ASF header GUID: 30 26 B2 75 8E 66 CF 11 A6 D9 00 AA 00 62 CE 6C
    static const uint8_t ASF_HEADER[] = {
        0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
        0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
    };

    if (length < 30) return std::nullopt;

    // Check ASF header GUID
    bool is_asf = true;
    for (size_t i = 0; i < 16; ++i) {
        if (data[i] != ASF_HEADER[i]) {
            is_asf = false;
            break;
        }
    }

    if (!is_asf) return std::nullopt;

    return MatchResult{
        {FileType::Video, L"wmv", L"WMV/ASF"},
        90,  // High confidence - GUID is definitive
        MatchFlags::HasHeader | MatchFlags::DeepValidated
    };
}

// Helper: validate FLV
static std::optional<MatchResult> validate_flv(const uint8_t* data, size_t length) {
    if (length < 13) return std::nullopt;

    // FLV signature: 'F' 'L' 'V'
    if (data[0] != 0x46 || data[1] != 0x4C || data[2] != 0x56) return std::nullopt;

    // Version should be 1
    if (data[3] != 1) return std::nullopt;

    // Flags: bit 0 = video, bit 2 = audio
    uint8_t flags = data[4];
    if ((flags & 0x05) == 0) {
        // No audio or video flags set - suspicious
        return MatchResult{
            {FileType::Video, L"flv", L"FLV"},
            50,  // Medium confidence
            MatchFlags::HasHeader | MatchFlags::PartialMatch
        };
    }

    return MatchResult{
        {FileType::Video, L"flv", L"FLV"},
        85,  // High confidence
        MatchFlags::HasHeader | MatchFlags::DeepValidated
    };
}

// Multi-candidate dispatcher with probabilistic routing
std::optional<MatchResult> FileSignatures::match_with_confidence(
    const uint8_t* data, size_t length)
{
    if (!data || length == 0) return std::nullopt;

    // Collect candidate validators based on first byte heuristic
    std::vector<std::optional<MatchResult>(*)(const uint8_t*, size_t)> candidates;

    uint8_t b0 = data[0];

    // Fast byte-heuristic routing
    // IMPORTANT: Use multi-byte patterns to reduce false positives

    // JPEG: Use 3-byte pattern FF D8 FF instead of just FF
    // FF alone appears in many compressed data streams
    // A valid JPEG must start with FF D8 followed by another FF (marker)
    if (b0 == 0xFF) {
        if (length >= 3 && data[1] == 0xD8 && data[2] == 0xFF) {
            candidates.push_back(validate_jpeg);
        }
        // Otherwise skip - too likely to be random FF in compressed data
    }

    if (b0 == 0x89) candidates.push_back(validate_png);
    if (b0 == 0x47) {
        // GIF vs TS conflict resolution:
        // Both start with 0x47, but GIF has full signature "GIF8"
        // GIF typically wins with higher confidence from signature match
        // Strategy: Run GIF first, only add TS if GIF doesn't strongly match

        auto gif_result = validate_gif(data, length);
        if (gif_result && gif_result->confidence >= 80) {
            // GIF89a or GIF87a with full structure - very likely GIF
            candidates.push_back(validate_gif);
        } else {
            // GIF didn't strongly match - try both
            candidates.push_back(validate_gif);
            // TS requires enough data for packet validation (at least 2 packets)
            if (length >= 376) {
                candidates.push_back(validate_ts);
            }
        }
    }
    if (b0 == 0x49 || b0 == 0x4D) candidates.push_back(validate_tiff_raw);
    if (b0 == 0x42) candidates.push_back(validate_bmp);
    if (b0 == 0x52) candidates.push_back(validate_riff);  // RIFF: AVI/WebP
    if (b0 == 0x1A) candidates.push_back(validate_ebml);  // EBML: MKV/WebM
    if (b0 == 0x30) candidates.push_back(validate_wmv);   // ASF header starts with 0x30
    if (b0 == 0x46) candidates.push_back(validate_flv);   // 'F' for FLV

    // Pattern probe for BMFF (ftyp not at byte 0)
    // BMFF containers have size field at bytes 0-3, then 'ftyp'/'moov' at 4-7
    if (length >= 12) {
        // Validate box size is reasonable before accepting BMFF candidate
        uint32_t box_size = (uint32_t(data[0]) << 24) |
                            (uint32_t(data[1]) << 16) |
                            (uint32_t(data[2]) << 8)  |
                            uint32_t(data[3]);

        // Box size should be at least 8 (header size) and reasonable (< 32MB for header)
        if (box_size >= 8 && box_size <= 32 * 1024 * 1024) {
            // Check for 'ftyp' at offset 4 (standard position)
            if (data[4] == 0x66 && data[5] == 0x74 && data[6] == 0x79 && data[7] == 0x70) {
                candidates.push_back(validate_bmff);
            }
            // Also check for 'moov' box (some raw MOV files)
            if (data[4] == 0x6D && data[5] == 0x6F && data[6] == 0x6F && data[7] == 0x76) {
                candidates.push_back(validate_bmff);
            }
        }
    }

    // Run all candidate validators and collect results
    std::vector<MatchResult> results;
    for (auto validator : candidates) {
        if (auto r = validator(data, length)) {
            results.push_back(*r);
        }
    }

    // Return the result with highest confidence
    if (results.empty()) return std::nullopt;

    auto best = std::max_element(results.begin(), results.end(),
        [](const MatchResult& a, const MatchResult& b) {
            return a.confidence < b.confidence;
        });

    return *best;
}

// Legacy interface wrapper
std::optional<FileSignature> FileSignatures::match(const uint8_t* data, size_t length) {
    auto result = match_with_confidence(data, length);
    if (!result) return std::nullopt;
    return result->signature;
}

} // namespace disk_recover
