// Central validator registration — ensures all validator objects are linked.
// On MSVC, the linker may strip static initializer objects from static libraries
// if no other code references them. This file provides explicit symbol references
// that force the linker to include every validator's .obj file, ensuring their
// auto-registration static initializers run before main().

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
#include "format_registry.hpp"

namespace disk_recover {

// Force linkage by taking the address of each validator's public function.
// This ensures the linker pulls in the corresponding .obj from the static library,
// which in turn ensures the static initializer that registers with FormatRegistry runs.
struct ForceLink {
    ForceLink() {
        (void)check_bmp_header;
        (void)check_wmv_header;
        (void)check_flv_header;
        (void)check_jpeg_header;
        (void)check_png_header;
        (void)check_gif_header;
        (void)check_tiff_header;
        (void)check_riff_header;
        (void)check_bmff_header;
        (void)check_ebml_header;
        (void)check_ts_header;
        (void)check_pdf_header;
        (void)check_zip_header;
        (void)check_7z_header;
        (void)check_rar_header;
        (void)check_doc_header;
        (void)check_mp3_header;
        (void)check_flac_header;
    }
};

static ForceLink _force_validator_linkage;

void ensure_all_validators_linked() {
    // This function can be called to ensure the ForceLink constructor has run.
    // In practice, the static initializer above runs before main().
    (void)_force_validator_linkage;
    FormatRegistry::instance();
}

} // namespace disk_recover