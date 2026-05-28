#include "file_signatures.hpp"
#include <vector>

namespace disk_recover {

static const uint8_t JPEG_PAT[] = {0xFF, 0xD8, 0xFF};
static const uint8_t PNG_PAT[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t BMP_PAT[]  = {0x42, 0x4D};
static const uint8_t GIF_PAT[]  = {0x47, 0x49, 0x46, 0x38};
static const uint8_t TIFF_LE_PAT[] = {0x49, 0x49, 0x2A, 0x00};
static const uint8_t TIFF_BE_PAT[] = {0x4D, 0x4D, 0x00, 0x2A};
static const uint8_t WEBP_PAT[] = {0x52, 0x49, 0x46, 0x46};
static const uint8_t MP4_PAT[]  = {0x66, 0x74, 0x79, 0x70};  // at offset 4
static const uint8_t AVI_PAT[]  = {0x52, 0x49, 0x46, 0x46};  // + AVI at offset 8
static const uint8_t MKV_PAT[]  = {0x1A, 0x45, 0xDF, 0xA3};
static const uint8_t WMV_PAT[]  = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11};
static const uint8_t FLV_PAT[]  = {0x46, 0x4C, 0x56};
static const uint8_t MOV_PAT[]  = {0x6D, 0x6F, 0x6F, 0x76};  // at offset 4

const std::vector<FileSignatures::SignatureEntry>& FileSignatures::entries() {
    static const std::vector<SignatureEntry> sigs = {
        {FileType::Image, L"jpg",  L"JPEG",     JPEG_PAT, 3, 0},
        {FileType::Image, L"png",  L"PNG",      PNG_PAT,  8, 0},
        {FileType::Image, L"bmp",  L"BMP",      BMP_PAT,  2, 0},
        {FileType::Image, L"gif",  L"GIF",      GIF_PAT,  4, 0},
        {FileType::Image, L"tiff", L"TIFF-LE",  TIFF_LE_PAT, 4, 0},
        {FileType::Image, L"tiff", L"TIFF-BE",  TIFF_BE_PAT, 4, 0},
        {FileType::Video, L"mp4",  L"MP4",      MP4_PAT,  4, 4},
        {FileType::Video, L"avi",  L"AVI",      AVI_PAT,  4, 0},
        {FileType::Video, L"mkv",  L"MKV/WebM", MKV_PAT, 4, 0},
        {FileType::Video, L"wmv",  L"WMV/ASF",  WMV_PAT, 8, 0},
        {FileType::Video, L"flv",  L"FLV",      FLV_PAT,  3, 0},
        {FileType::Video, L"mov",  L"MOV",      MOV_PAT,  4, 4},
    };
    return sigs;
}

std::optional<FileSignature> FileSignatures::match(const uint8_t* data, size_t length) {
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
