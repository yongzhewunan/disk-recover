#include "file_signatures.hpp"
#include <vector>

namespace disk_recover {

static const uint8_t JPEG_PAT[] = {0xFF, 0xD8, 0xFF};
static const uint8_t PNG_PAT[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t BMP_PAT[]  = {0x42, 0x4D};
static const uint8_t GIF_PAT[]  = {0x47, 0x49, 0x46, 0x38};
static const uint8_t TIFF_LE_PAT[] = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t TIFF_BE_PAT[] = {0x4D, 0x4D, 0x00, 0x2A};
static const uint8_t WEBP_PAT[] = {0x52, 0x49, 0x46, 0x46};  // RIFF, check 'WEBP' at offset 8
static const uint8_t MP4_PAT[]  = {0x66, 0x74, 0x79, 0x70};  // 'ftyp' at offset 4
static const uint8_t AVI_PAT[]  = {0x52, 0x49, 0x46, 0x46};  // RIFF, check 'AVI ' at offset 8
static const uint8_t MKV_PAT[]  = {0x1A, 0x45, 0xDF, 0xA3};  // EBML header (MKV/WebM)
static const uint8_t WMV_PAT[]  = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11};
static const uint8_t FLV_PAT[]  = {0x46, 0x4C, 0x56};
static const uint8_t MOV_PAT[]  = {0x6D, 0x6F, 0x6F, 0x76};  // 'moov' at offset 4
static const uint8_t HEIC_PAT[] = {0x66, 0x74, 0x79, 0x70};  // 'ftyp' at offset 4
static const uint8_t MTS_PAT[]  = {0x47};  // MPEG-2 TS sync byte

// RAW camera format signatures
// Most RAW formats are TIFF-based (II* header = 0x49 0x49 0x2A 0x00)
static const uint8_t CR2_PAT[]  = {0x49, 0x49, 0x2A, 0x00};  // Canon RAW - TIFF header, check bytes 8-10 for "CR"
static const uint8_t NEF_PAT[]  = {0x49, 0x49, 0x2A, 0x00};  // Nikon RAW - TIFF header
static const uint8_t ARW_PAT[]  = {0x49, 0x49, 0x2A, 0x00};  // Sony RAW - TIFF header
static const uint8_t DNG_PAT[]  = {0x49, 0x49, 0x2A, 0x00};  // Adobe DNG - TIFF header
static const uint8_t RW2_PAT[]  = {0x49, 0x49, 0x2A, 0x00};  // Panasonic RAW - TIFF header
static const uint8_t ORF_PAT[]  = {0x49, 0x49, 0x52, 0x4F};  // Olympus RAW - "IIRO" header (first 4 bytes of "IIROLYPUS")

const std::vector<FileSignatures::SignatureEntry>& FileSignatures::entries() {
    static const std::vector<SignatureEntry> sigs = {
        {FileType::Image, L"jpg",  L"JPEG",     JPEG_PAT, 3, 0},
        {FileType::Image, L"png",  L"PNG",      PNG_PAT,  8, 0},
        {FileType::Image, L"bmp",  L"BMP",      BMP_PAT,  2, 0},
        {FileType::Image, L"gif",  L"GIF",      GIF_PAT,  4, 0},
        {FileType::Image, L"tiff", L"TIFF-LE",  TIFF_LE_PAT, 4, 0},
        {FileType::Image, L"tiff", L"TIFF-BE",  TIFF_BE_PAT, 4, 0},
        // RAW camera formats
        {FileType::Image, L"cr2",  L"CR2 (Canon RAW)",   CR2_PAT,  4, 0},
        {FileType::Image, L"nef",  L"NEF (Nikon RAW)",   NEF_PAT,  4, 0},
        {FileType::Image, L"arw",  L"ARW (Sony RAW)",    ARW_PAT,  4, 0},
        {FileType::Image, L"dng",  L"DNG (Adobe DNG)",   DNG_PAT,  4, 0},
        {FileType::Image, L"rw2",  L"RW2 (Panasonic)",   RW2_PAT,  4, 0},
        {FileType::Image, L"orf",  L"ORF (Olympus RAW)", ORF_PAT,  4, 0},
        // HEIC/HEIF (Apple High Efficiency Image)
        {FileType::Image, L"heic", L"HEIC (Apple HEIF)", HEIC_PAT, 4, 4},
        // Video formats
        {FileType::Video, L"mp4",  L"MP4",      MP4_PAT,  4, 4},
        {FileType::Video, L"webm", L"WebM",     MKV_PAT,  4, 0},  // EBML header, same as MKV
        {FileType::Video, L"avi",  L"AVI",      AVI_PAT,  4, 0},
        {FileType::Video, L"mkv",  L"MKV",      MKV_PAT,  4, 0},
        {FileType::Video, L"wmv",  L"WMV/ASF",  WMV_PAT, 8, 0},
        {FileType::Video, L"flv",  L"FLV",      FLV_PAT,  3, 0},
        {FileType::Video, L"mov",  L"MOV",      MOV_PAT,  4, 4},
        // MPEG-2 Transport Stream (AVCHD video)
        {FileType::Video, L"mts",  L"MTS (AVCHD)",  MTS_PAT, 1, 0},
        {FileType::Video, L"m2ts", L"M2TS (AVCHD)", MTS_PAT, 1, 0},
    };
    return sigs;
}

std::optional<FileSignature> FileSignatures::match(const uint8_t* data, size_t length) {
    // Special handling for RIFF-based formats (WEBP vs AVI)
    // Both start with 'RIFF' but have different identifiers at offset 8
    if (length >= 12) {
        if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46) {
            // Check the 4-byte identifier at offset 8
            if (data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                // 'WEBP' found
                return FileSignature{FileType::Image, L"webp", L"WebP"};
            }
            // Note: AVI will be handled by the general loop below
        }
    }

    // Special handling for 'ftyp'-based formats (MP4, MOV, HEIC, etc.)
    // All have 'ftyp' at offset 4, but different brands at offset 8
    if (length >= 12) {
        // Check for 'ftyp' at offset 4
        if (data[4] == 0x66 && data[5] == 0x74 && data[6] == 0x79 && data[7] == 0x70) {
            // Check brand at offset 8
            // HEIC brands: heic, heix, mif1, msf1
            if ((data[8] == 0x68 && data[9] == 0x65 && data[10] == 0x69 && data[11] == 0x63) ||  // heic
                (data[8] == 0x68 && data[9] == 0x65 && data[10] == 0x69 && data[11] == 0x78) ||  // heix
                (data[8] == 0x6D && data[9] == 0x69 && data[10] == 0x66 && data[11] == 0x31)) {  // mif1
                return FileSignature{FileType::Image, L"heic", L"HEIC (Apple HEIF)"};
            }
            // MOV brands: qt
            if (data[8] == 0x71 && data[9] == 0x74 && data[10] == 0x20 && data[11] == 0x20) {  // qt  (with spaces)
                return FileSignature{FileType::Video, L"mov", L"MOV"};
            }
            // MP4 brands: isom, mp41, mp42, M4V, etc. - default to MP4 for other ftyp
            return FileSignature{FileType::Video, L"mp4", L"MP4"};
        }
    }

    for (const auto& entry : entries()) {
        if (length < entry.offset + entry.pattern_len) continue;
        bool matched = true;
        for (size_t i = 0; i < entry.pattern_len; ++i) {
            if (data[entry.offset + i] != entry.pattern[i]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return FileSignature{entry.file_type, entry.extension, entry.description};
        }
    }
    return std::nullopt;
}

} // namespace disk_recover
