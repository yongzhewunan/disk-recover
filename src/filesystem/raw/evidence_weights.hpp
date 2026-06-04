#pragma once
#include <cstdint>

namespace disk_recover {

// Per-format evidence weights for normalized confidence scoring
// This enables semantic equivalence of confidence scores across different formats
struct EvidenceWeights {
    float header_weight;      // Max points for valid header
    float structure_weight;   // Max points for structure validation (marker stream, IFD)
    float container_weight;   // Max points for container parsing (atoms, tags)
    float footer_weight;      // Max points for footer detection (EOI, IEND)

    float max_total() const {
        return header_weight + structure_weight + container_weight + footer_weight;
    }
};

// JPEG evidence weights
// Header: SOI marker (FFD8)
// Structure: Marker stream (APP, SOF, DHT, DQT, SOS)
// Container: JFIF/Exif/Adobe markers
// Footer: EOI marker (FFD9)
constexpr EvidenceWeights JPEG_WEIGHTS = {
    .header_weight = 15.0f,
    .structure_weight = 35.0f,
    .container_weight = 10.0f,
    .footer_weight = 15.0f
};

// BMP evidence weights
// Header: BM signature + file_size + pixel_offset
// Structure: DIB header (header_size, planes, bpp, compression, dimensions)
// Container: Palette / color table
// Footer: File size cross-verification
constexpr EvidenceWeights BMP_WEIGHTS = {
    .header_weight = 20.0f,
    .structure_weight = 40.0f,
    .container_weight = 10.0f,
    .footer_weight = 30.0f
};

// TIFF/RAW evidence weights
// Header: TIFF header (II*/MM*) + magic 42 + IFD offset
// Structure: IFD structure valid, tag count reasonable
// Container: Vendor tags detected (DNGVersion, MakerNote) - PRIMARY evidence
// Footer: Not applicable for TIFF
constexpr EvidenceWeights TIFF_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 20.0f,
    .container_weight = 40.0f,
    .footer_weight = 10.0f
};

// PNG evidence weights
// Header: PNG signature (89 50 4E 47 0D 0A 1A 0A)
// Structure: IHDR chunk valid, chunk structure
// Container: Additional chunks (sRGB, gAMA, etc.)
// Footer: IEND chunk presence
constexpr EvidenceWeights PNG_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 30.0f,
    .container_weight = 10.0f,
    .footer_weight = 30.0f
};

// GIF evidence weights
// Header: GIF87a/GIF89a signature
// Structure: Logical Screen Descriptor valid
// Container: Extension blocks (GCE, Application)
// Footer: Trailer (0x3B)
constexpr EvidenceWeights GIF_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 25.0f,
    .container_weight = 10.0f,
    .footer_weight = 35.0f
};

// BMFF (MP4/MOV/HEIC/AVIF) evidence weights
// Header: ftyp box presence + brand
// Structure: Box structure valid (size, type)
// Container: moov/mdat boxes parsed
// Footer: Not typically applicable
constexpr EvidenceWeights BMFF_WEIGHTS = {
    .header_weight = 35.0f,
    .structure_weight = 25.0f,
    .container_weight = 30.0f,
    .footer_weight = 10.0f
};

// MPEG-TS evidence weights
// Header: Sync byte 0x47
// Structure: Packet structure valid (PID, adaptation)
// Container: Continuity counter valid, multiple packets
// Footer: Not applicable
constexpr EvidenceWeights TS_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 30.0f,
    .container_weight = 30.0f,
    .footer_weight = 10.0f
};

// RIFF (AVI/WebP) evidence weights
// Header: RIFF signature + container type
// Structure: Chunk structure valid
// Container: LIST hdrl for AVI, VP8 for WebP
// Footer: Size validation
constexpr EvidenceWeights RIFF_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 25.0f,
    .container_weight = 35.0f,
    .footer_weight = 10.0f
};

// EBML (MKV/WebM) evidence weights
// Header: EBML signature
// Structure: Element structure valid
// Container: DocType parsed (matroska/webm)
// Footer: Not applicable
constexpr EvidenceWeights EBML_WEIGHTS = {
    .header_weight = 30.0f,
    .structure_weight = 20.0f,
    .container_weight = 40.0f,
    .footer_weight = 10.0f
};

// Calculate normalized confidence from raw evidence score
// Returns 0-100 scale with semantic equivalence across formats
inline uint8_t normalize_confidence(float evidence, const EvidenceWeights& weights) {
    float ratio = evidence / weights.max_total();
    float confidence = ratio * 100.0f;
    if (confidence > 100.0f) confidence = 100.0f;
    if (confidence < 0.0f) confidence = 0.0f;
    return static_cast<uint8_t>(confidence);
}

} // namespace disk_recover