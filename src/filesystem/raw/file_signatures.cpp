#include "file_signatures.hpp"
#include "binary_reader.hpp"
#include "evidence_weights.hpp"
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
// Uses return-nullopt for invalid fields instead of soft penalties
// This drastically reduces false positives from random BM-prefixed data
static std::optional<MatchResult> validate_bmp(const uint8_t* data, size_t length) {
    if (length < 54) return std::nullopt;
    if (data[0] != 0x42 || data[1] != 0x4D) return std::nullopt;

    float evidence = 50.0f;
    MatchFlags flags = MatchFlags::HasHeader;

    uint32_t file_size    = read_le32(data + 2);
    uint32_t pixel_offset = read_le32(data + 10);
    uint32_t dib_size     = read_le32(data + 14);

    // ── Hard rejects ──
    if (file_size < 54)    return std::nullopt;
    if (pixel_offset < 54) return std::nullopt;

    evidence += 10.0f;

    // ── DIB header size ──
    switch (dib_size) {
        case 12:  // BITMAPCOREHEADER (OS/2)
        case 40:  // BITMAPINFOHEADER (standard)
        case 52: case 56: case 64:    // extended
        case 108: // BITMAPV4HEADER
        case 124: // BITMAPV5HEADER
            evidence += 15.0f;
            break;
        default:
            return std::nullopt;  // Unknown DIB header → reject
    }

    // ── Dimensions ──
    int32_t width  = static_cast<int32_t>(read_le32(data + 18));
    int32_t height = static_cast<int32_t>(read_le32(data + 22));
    uint16_t planes = read_le16(data + 26);
    uint16_t bpp    = read_le16(data + 28);
    uint32_t compression = read_le32(data + 30);

    // Planes must be 1
    if (planes != 1) return std::nullopt;
    evidence += 10.0f;

    // ── Bits per pixel ──
    switch (bpp) {
        case 1: case 4: case 8:
        case 16: case 24: case 32:
            evidence += 10.0f;
            break;
        default:
            return std::nullopt;  // Invalid bpp → reject
    }

    // ── Compression ──
    switch (compression) {
        case 0: // BI_RGB
        case 1: // BI_RLE8
        case 2: // BI_RLE4
        case 3: // BI_BITFIELDS
        case 4: // BI_JPEG
        case 5: // BI_PNG
            evidence += 10.0f;
            break;
        default:
            return std::nullopt;  // Unknown compression → reject
    }

    // ── Dimension sanity ──
    if (width <= 0 || width > 100000) return std::nullopt;
    if (height == 0) return std::nullopt;
    int64_t abs_height = height < 0 ? -static_cast<int64_t>(height) : static_cast<int64_t>(height);
    if (abs_height > 100000) return std::nullopt;
    evidence += 10.0f;

    // ── File size cross-verification (uncompressed only) ──
    if (compression == 0) {
        // Row size: bits packed into 4-byte aligned rows
        uint64_t row_bits  = static_cast<uint64_t>(width) * bpp;
        uint64_t row_bytes = ((row_bits + 31) / 32) * 4;
        uint64_t expected_size = pixel_offset + row_bytes * static_cast<uint64_t>(abs_height);

        if (file_size > 0) {
            uint64_t diff = expected_size > file_size
                          ? expected_size - file_size
                          : file_size - expected_size;
            if (diff < file_size * 5 / 100)       // within 5%
                evidence += 15.0f;
            else if (diff < file_size * 25 / 100)  // within 25%
                evidence += 5.0f;
        }
    }

    flags = flags | MatchFlags::DeepValidated;

    return MatchResult{
        {FileType::Image, L"bmp", L"BMP"},
        normalize_confidence(evidence, BMP_WEIGHTS),
        flags,
        file_size
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
