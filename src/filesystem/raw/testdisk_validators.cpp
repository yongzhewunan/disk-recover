/*

    File: testdisk_validators.cpp

    Implementation of TestDisk validator registration for disk-recover.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#include "testdisk_validators.hpp"
#include <cstring>
#include <stdexcept>

extern "C" {
#include "../external/testdisk/common.h"
}

namespace disk_recover {

// ============================================================================
// Global validator dispatch - static wrapper functions for FormatDescriptor
// ============================================================================
//
// FormatDescriptor uses simple function pointers (no user_data), so we need
// a way to dispatch to the correct TestDiskValidator. We use a global map
// keyed by the extension string stored in the FormatDescriptor.

static std::unordered_map<std::wstring, TestDiskValidator*>& validator_map() {
    static std::unordered_map<std::wstring, TestDiskValidator*> map;
    return map;
}

// Static wrapper for FormatDescriptor::header_check
// Dispatches to the TestDiskValidator identified by the extension in the descriptor
static ValidateResult td_header_check_dispatch(const uint8_t* data, size_t length,
                                                uint64_t& calculated_file_size) {
    // Find the validator by looking up the current format's extension
    // We store the descriptor pointer during registration so we can
    // find the right validator.
    // Actually, since FormatDescriptor stores extension as wchar_t*,
    // we need a different approach.
    //
    // Simpler approach: Try all validators in sequence.
    // This works because TestDisk's header_check will only match for
    // the correct format.
    auto& vmap = validator_map();
    for (auto& [ext, validator] : vmap) {
        ValidateResult result = validator->header_check(data, length, calculated_file_size);
        if (result != ValidateResult::Reject) {
            return result;
        }
    }
    calculated_file_size = 0;
    return ValidateResult::Reject;
}

// ============================================================================
// TestDiskValidatorRegistrar Implementation
// ============================================================================

TestDiskValidatorRegistrar& TestDiskValidatorRegistrar::instance() {
    static TestDiskValidatorRegistrar registrar;
    return registrar;
}

std::wstring TestDiskValidatorRegistrar::to_wstring(const char* str) {
    if (str == nullptr) return L"";
    size_t len = strlen(str);
    std::wstring result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(str[i])));
    }
    return result;
}

FileType TestDiskValidatorRegistrar::extension_to_filetype(const char* extension) {
    if (extension == nullptr) return FileType::Unknown;

    // Images
    if (strcmp(extension, "psd") == 0 || strcmp(extension, "psb") == 0 ||
        strcmp(extension, "abr") == 0 ||
        strcmp(extension, "jpg") == 0 || strcmp(extension, "jpeg") == 0 ||
        strcmp(extension, "bmp") == 0 || strcmp(extension, "png") == 0 ||
        strcmp(extension, "gif") == 0 || strcmp(extension, "tiff") == 0 ||
        strcmp(extension, "tif") == 0 || strcmp(extension, "afdesign") == 0)
        return FileType::Image;

    // Video
    if (strcmp(extension, "mov") == 0 || strcmp(extension, "mp4") == 0 ||
        strcmp(extension, "avi") == 0 || strcmp(extension, "mkv") == 0 ||
        strcmp(extension, "webm") == 0 || strcmp(extension, "flv") == 0 ||
        strcmp(extension, "wmv") == 0 || strcmp(extension, "asf") == 0)
        return FileType::Video;

    // Audio
    if (strcmp(extension, "mp3") == 0 || strcmp(extension, "flac") == 0)
        return FileType::Audio;

    // Documents
    if (strcmp(extension, "pdf") == 0 || strcmp(extension, "doc") == 0 ||
        strcmp(extension, "indd") == 0)
        return FileType::Document;

    // Archives
    if (strcmp(extension, "zip") == 0 || strcmp(extension, "7z") == 0 ||
        strcmp(extension, "rar") == 0)
        return FileType::Archive;

    return FileType::Unknown;
}

void TestDiskValidatorRegistrar::register_format(FormatRegistry& registry,
                                                  const file_hint_t* hint,
                                                  FileType file_type)
{
    // Create a validator instance
    auto validator = std::make_unique<TestDiskValidator>(hint);
    std::wstring ext_wstr = to_wstring(hint->extension);

    // Store in the global map for dispatch
    validator_map()[ext_wstr] = validator.get();

    // Also store in our local map
    validators_[hint->extension] = std::move(validator);

    // Create FormatDescriptor
    FormatDescriptor desc;
    desc.file_type = file_type;

    // Need stable storage for extension and description strings
    // Since validators_ owns the validators, we can get the wstring pointers
    auto& stored_validator = validators_[hint->extension];
    desc.extension = stored_validator->extension().c_str();
    desc.description = stored_validator->description().c_str();

    desc.min_filesize = 0;
    desc.max_filesize = hint->max_filesize;
    desc.enabled_by_default = (hint->enable_by_default != 0);

    // Signature pattern - TestDisk uses a registration system, not embedded patterns
    // We set a placeholder; actual matching is done via TestDisk's header_check_list
    desc.signature.pattern = nullptr;
    desc.signature.pattern_len = 0;
    desc.signature.offset = 0;

    // For now, we don't set header_check/data_check/file_check on the descriptor
    // because the dispatch mechanism needs the validator to be initialized.
    // The SignatureScanner will use TestDisk's registered header checks directly
    // via td_register_all_validators() + file_check_plist traversal.
    desc.header_check = nullptr;
    desc.data_check = nullptr;
    desc.file_check = nullptr;

    registry.register_format(desc);
}

void TestDiskValidatorRegistrar::register_all(FormatRegistry& registry) {
    // First, register all TestDisk signature patterns in their global list
    td_register_all_validators();

    // Adobe family
    register_format(registry, &file_hint_psd, FileType::Image);
    register_format(registry, &file_hint_psb, FileType::Image);
    register_format(registry, &file_hint_abr, FileType::Image);
    register_format(registry, &file_hint_indd, FileType::Document);
    register_format(registry, &file_hint_pdf, FileType::Document);
    register_format(registry, &file_hint_afdesign, FileType::Image);

    // Images
    register_format(registry, &file_hint_jpg, FileType::Image);
    register_format(registry, &file_hint_bmp, FileType::Image);
    register_format(registry, &file_hint_png, FileType::Image);
    register_format(registry, &file_hint_gif, FileType::Image);
    register_format(registry, &file_hint_tiff, FileType::Image);

    // Videos
    register_format(registry, &file_hint_mov, FileType::Video);
    register_format(registry, &file_hint_riff, FileType::Video);
    register_format(registry, &file_hint_mkv, FileType::Video);
    register_format(registry, &file_hint_flv, FileType::Video);
    register_format(registry, &file_hint_asf, FileType::Video);

    // Audio
    register_format(registry, &file_hint_flac, FileType::Audio);
    register_format(registry, &file_hint_mp3, FileType::Audio);

    // Documents
    register_format(registry, &file_hint_doc, FileType::Document);

    // Archives
    register_format(registry, &file_hint_zip, FileType::Archive);
    register_format(registry, &file_hint_7z, FileType::Archive);
    register_format(registry, &file_hint_rar, FileType::Archive);
}

} // namespace disk_recover