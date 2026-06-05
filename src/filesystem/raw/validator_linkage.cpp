// Central validator registration — explicitly registers all format descriptors.
// This replaces the unreliable auto-registration via static initializers,
// which was stripped by MSVC's linker when consuming validators from a static library.
// By calling register_all_formats() from FormatRegistry::instance(), we guarantee
// all descriptors are registered before any match() calls, regardless of linker behavior.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "format_registry.hpp"

#include "validators/bmp_validator.hpp"
#include "validators/wmv_validator.hpp"
#include "validators/flv_validator.hpp"
#include "validators/jpeg_validator.hpp"
#include "validators/png_validator.hpp"
#include "validators/gif_validator.hpp"
#include "validators/tiff_validator.hpp"
#include "validators/riff_validator.hpp"
#include "validators/bmff_validator.hpp"
#include "validators/ebml_validator.hpp"
#include "validators/ts_validator.hpp"
#include "validators/pdf_validator.hpp"
#include "validators/zip_validator.hpp"
#include "validators/sevenz_validator.hpp"
#include "validators/rar_validator.hpp"
#include "validators/doc_validator.hpp"
#include "validators/mp3_validator.hpp"
#include "validators/flac_validator.hpp"

namespace disk_recover {

void register_all_formats(FormatRegistry& registry) {
    // Image formats
    registry.register_format(BMP_DESCRIPTOR);
    registry.register_format(JPEG_DESCRIPTOR);
    registry.register_format(PNG_DESCRIPTOR);
    registry.register_format(GIF_DESCRIPTOR);
    registry.register_format(TIFF_LE_DESCRIPTOR);
    registry.register_format(TIFF_BE_DESCRIPTOR);
    registry.register_format(ORF_DESCRIPTOR);
    registry.register_format(WEBP_DESCRIPTOR);

    // Video formats
    registry.register_format(AVI_DESCRIPTOR);
    registry.register_format(BMFF_VIDEO_DESCRIPTOR);
    registry.register_format(EBML_DESCRIPTOR);
    registry.register_format(TS_DESCRIPTOR);
    registry.register_format(WMV_DESCRIPTOR);
    registry.register_format(FLV_DESCRIPTOR);
    registry.register_format(RIFF_GENERIC_DESCRIPTOR);

    // Audio formats
    registry.register_format(WAV_DESCRIPTOR);
    registry.register_format(BMFF_AUDIO_DESCRIPTOR);
    registry.register_format(BMFF_IMAGE_DESCRIPTOR);  // HEIC/HEIF image variant
    registry.register_format(MP3_DESCRIPTOR);
    registry.register_format(FLAC_DESCRIPTOR);

    // Document formats
    registry.register_format(PDF_DESCRIPTOR);
    // registry.register_format(DOC_DESCRIPTOR);  // Temporarily disabled

    // Archive formats
    registry.register_format(ZIP_DESCRIPTOR);
    registry.register_format(SEVENZ_DESCRIPTOR);
    registry.register_format(RAR_DESCRIPTOR);
}

} // namespace disk_recover