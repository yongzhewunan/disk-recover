// Central validator registration — explicitly registers all format descriptors.
// This replaces the unreliable auto-registration via static initializers,
// which was stripped by MSVC's linker when consuming validators from a static library.
// By calling register_all_formats() from FormatRegistry::instance(), we guarantee
// all descriptors are registered before any match() calls, regardless of linker behavior.
//
// All file format validators now use the TestDisk/PhotoRec engine via
// register_testdisk_validators(). The only exception is MPEG-TS (MTS/M2TS),
// which has no TestDisk equivalent and uses the native C++ validator.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "format_registry.hpp"
#include "testdisk_validators.hpp"

// Only legacy validator without a TestDisk equivalent
#include "validators/ts_validator.hpp"

namespace disk_recover {

void register_all_formats(FormatRegistry& registry) {
    // Register all TestDisk/PhotoRec validators (images, video, audio, documents, archives)
    // This populates FormatRegistry with FormatDescriptors extracted from file_check_plist,
    // including real signature patterns and per-format dispatch function pointers.
    register_testdisk_validators(registry);

    // MPEG-TS (MTS/M2TS) — no TestDisk equivalent, preserved as native validator
    registry.register_format(TS_DESCRIPTOR);
}

} // namespace disk_recover