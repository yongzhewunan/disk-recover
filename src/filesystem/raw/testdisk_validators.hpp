/*

    File: testdisk_validators.hpp

    Automatic registration of TestDisk validators into disk-recover's FormatRegistry.

    This file provides a unified interface to register all TestDisk validators
    and integrates them with disk-recover's FormatDescriptor system.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include "testdisk_adapter.hpp"
#include "format_descriptor.hpp"
#include "format_registry.hpp"

// TestDisk file hints - declarations from file_xxx.c files
extern "C" {

#include "../external/testdisk/filegen.h"

// Adobe family formats (priority)
extern const file_hint_t file_hint_psd;
extern const file_hint_t file_hint_psb;
extern const file_hint_t file_hint_abr;
extern const file_hint_t file_hint_indd;
extern const file_hint_t file_hint_pdf;
extern const file_hint_t file_hint_afdesign;

// Image formats
extern const file_hint_t file_hint_jpg;
extern const file_hint_t file_hint_bmp;
extern const file_hint_t file_hint_png;
extern const file_hint_t file_hint_gif;
extern const file_hint_t file_hint_tiff;

// Video formats
extern const file_hint_t file_hint_mov;
extern const file_hint_t file_hint_riff;
extern const file_hint_t file_hint_mkv;
extern const file_hint_t file_hint_flv;
extern const file_hint_t file_hint_asf;

// Audio formats
extern const file_hint_t file_hint_flac;
extern const file_hint_t file_hint_mp3;

// Document formats
extern const file_hint_t file_hint_doc;

// Archive formats
extern const file_hint_t file_hint_zip;
extern const file_hint_t file_hint_7z;
extern const file_hint_t file_hint_rar;

// Registration function
void td_register_all_validators(void);

} // extern "C"

namespace disk_recover {

/**
 * @brief Registration helper for TestDisk validators.
 *
 * Uses a global validator map to store TestDiskValidator instances keyed
 * by extension, so the static FormatDescriptor function pointers can
 * dispatch to the appropriate validator.
 */
class TestDiskValidatorRegistrar {
public:
    /**
     * @brief Get the singleton instance.
     */
    static TestDiskValidatorRegistrar& instance();

    /**
     * @brief Register all TestDisk validators with the FormatRegistry.
     *
     * This:
     * 1. Calls td_register_all_validators() to register signature patterns
     *    in TestDisk's global header check list.
     * 2. Creates TestDiskValidator instances for each format.
     * 3. Creates FormatDescriptors with wrapper function pointers that
     *    dispatch to the appropriate validator via the global map.
     *
     * @param registry The FormatRegistry to register with.
     */
    void register_all(FormatRegistry& registry);

private:
    TestDiskValidatorRegistrar() = default;

    // Global validator map keyed by extension string
    std::unordered_map<std::string, std::unique_ptr<TestDiskValidator>> validators_;

    // Convert file_hint_t extension to FileType enum
    static FileType extension_to_filetype(const char* extension);

    // Convert char* to wstring
    static std::wstring to_wstring(const char* str);

    // Helper to register a single format
    void register_format(FormatRegistry& registry,
                         const file_hint_t* hint,
                         FileType file_type);
};

/**
 * @brief Convenience function to register all TestDisk validators.
 */
inline void register_testdisk_validators() {
    TestDiskValidatorRegistrar::instance().register_all(
        FormatRegistry::instance());
}

} // namespace disk_recover