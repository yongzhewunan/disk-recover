/*

    File: testdisk_validators.cpp

    Implementation of TestDisk validator registration for disk-recover.

    After td_register_all_validators() populates file_check_plist with
    signature patterns, this module walks the plist and creates FormatDescriptor
    entries with real signature bytes and per-format dispatch function pointers.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#include "testdisk_validators.hpp"
#include "testdisk_adapter.hpp"
#include <cstring>
#include <unordered_map>
#include <vector>
#include <list>
#include <string>

extern "C" {
#include "../external/testdisk/filegen.h"
}

// Declaration of the registration function from td_validators_init.c
extern "C" void td_register_all_validators(void);

namespace disk_recover {

// ============================================================================
// Global validator map — keyed by extension string (wchar_t)
// ============================================================================
static std::unordered_map<std::wstring, std::unique_ptr<TestDiskValidator>>& g_validators() {
    static std::unordered_map<std::wstring, std::unique_ptr<TestDiskValidator>> map;
    return map;
}

// Stable string storage — wstrings must outlive the FormatDescriptors that
// reference their c_str() pointers.  This list owns them permanently.
// Using std::list instead of std::vector because list nodes are stable —
// inserting new elements does NOT invalidate references/pointers to existing ones.
static std::list<std::wstring>& g_stable_strings() {
    static std::list<std::wstring> store;
    return store;
}

// Stable signature pattern storage — raw byte arrays that FormatDescriptor
// signature.pattern points to.  Must outlive the descriptors.
// Using std::list for the same stability guarantee.
static std::list<std::vector<uint8_t>>& g_stable_patterns() {
    static std::list<std::vector<uint8_t>> store;
    return store;
}

// ============================================================================
// Helper: Convert char* to wstring (ASCII only)
// ============================================================================

static std::wstring char_to_wstring(const char* str) {
    if (str == nullptr) return L"";
    size_t len = strlen(str);
    std::wstring result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(str[i])));
    }
    return result;
}

// ============================================================================
// Helper: Map TestDisk extension to disk-recover FileType
// ============================================================================

static FileType extension_to_filetype(const char* extension) {
    if (extension == nullptr) return FileType::Unknown;

    // Images
    if (strcmp(extension, "psd") == 0 || strcmp(extension, "psb") == 0 ||
        strcmp(extension, "abr") == 0 ||
        strcmp(extension, "jpg") == 0 || strcmp(extension, "jpeg") == 0 ||
        strcmp(extension, "bmp") == 0 || strcmp(extension, "png") == 0 ||
        strcmp(extension, "gif") == 0 || strcmp(extension, "tiff") == 0 ||
        strcmp(extension, "tif") == 0 || strcmp(extension, "afdesign") == 0 ||
        strcmp(extension, "heic") == 0 || strcmp(extension, "jp2") == 0 ||
        strcmp(extension, "cr3") == 0 || strcmp(extension, "webp") == 0 ||
        strcmp(extension, "ani") == 0)
        return FileType::Image;

    // Video
    if (strcmp(extension, "mov") == 0 || strcmp(extension, "mp4") == 0 ||
        strcmp(extension, "avi") == 0 || strcmp(extension, "mkv") == 0 ||
        strcmp(extension, "webm") == 0 || strcmp(extension, "flv") == 0 ||
        strcmp(extension, "wmv") == 0 || strcmp(extension, "asf") == 0 ||
        strcmp(extension, "3gp") == 0 || strcmp(extension, "3g2") == 0 ||
        strcmp(extension, "m4v") == 0)
        return FileType::Video;

    // Audio
    if (strcmp(extension, "mp3") == 0 || strcmp(extension, "flac") == 0 ||
        strcmp(extension, "m4a") == 0 || strcmp(extension, "wav") == 0 ||
        strcmp(extension, "wma") == 0 || strcmp(extension, "mid") == 0 ||
        strcmp(extension, "cda") == 0)
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

// ============================================================================
// Helper: Determine the effective file type for a FormatDescriptor
// ============================================================================

static FileType determine_file_type(const file_hint_t* hint) {
    FileType ft = extension_to_filetype(hint->extension);

    // Special case: RIFF defaults to Video (AVI is most common)
    if (strcmp(hint->extension, "riff") == 0) {
        return FileType::Video;
    }

    // Special case: MOV covers MP4/MOV/HEIC/M4A — default to Video
    if (strcmp(hint->extension, "mov") == 0) {
        return FileType::Video;
    }

    return ft;
}

// ============================================================================
// Dispatch wrapper functions
// ============================================================================
//
// FormatDescriptor uses bare function pointers with no user-data parameter.
// The dispatch context (g_td_dispatch_ext) is set by FormatRegistry::match()
// before calling header_check, and by SignatureScanner before calling
// data_check/file_check. The wrapper functions look up the TestDiskValidator
// by extension key and dispatch to the appropriate method.

static ValidateResult td_dispatch_header_check(const uint8_t* data, size_t length,
                                                uint64_t& calculated_file_size) {
    if (g_td_dispatch_ext == nullptr) {
        printf("[TD-DISP] header_check: g_td_dispatch_ext is NULL\n");
        return ValidateResult::Reject;
    }
    auto& vmap = g_validators();
    std::wstring key(g_td_dispatch_ext);
    printf("[TD-DISP] header_check: looking for key='%ls', vmap size=%zu\n",
           g_td_dispatch_ext, vmap.size());
    auto it = vmap.find(key);
    if (it != vmap.end()) {
        printf("[TD-DISP] header_check: found validator for '%ls'\n", g_td_dispatch_ext);
        return it->second->header_check(data, length, calculated_file_size);
    }
    printf("[TD-DISP] header_check: key '%ls' NOT FOUND in vmap\n", g_td_dispatch_ext);
    return ValidateResult::Reject;
}

static ValidateResult td_dispatch_data_check(const uint8_t* data, size_t length,
                                             uint64_t offset_in_file,
                                             uint64_t& calculated_file_size) {
    if (g_td_dispatch_ext == nullptr) return ValidateResult::Reject;
    auto& vmap = g_validators();
    auto it = vmap.find(std::wstring(g_td_dispatch_ext));
    if (it != vmap.end()) {
        return it->second->data_check(data, length, offset_in_file, calculated_file_size);
    }
    return ValidateResult::Reject;
}

static ValidateResult td_dispatch_file_check(const uint8_t* data, size_t length,
                                             uint64_t& calculated_file_size) {
    if (g_td_dispatch_ext == nullptr) return ValidateResult::Reject;
    auto& vmap = g_validators();
    auto it = vmap.find(std::wstring(g_td_dispatch_ext));
    if (it != vmap.end()) {
        return it->second->file_check(data, length, calculated_file_size);
    }
    return ValidateResult::Reject;
}

// ============================================================================
// Main registration function
// ============================================================================

void register_testdisk_validators(FormatRegistry& registry) {
    // Step 1: Register all TestDisk signature patterns in their global list.
    // This populates file_check_plist with file_check_t entries,
    // each containing {value, length, offset, header_check, file_stat}.
    td_register_all_validators();

    // Map from file_hint_t* to the wstring extension key for g_validators
    std::unordered_map<const file_hint_t*, std::wstring> hint_to_ext;

    // DEBUG: Count plist entries
    int plist_total = 0, plist_valid = 0;
    struct td_list_head* debug_pos;
    td_list_for_each(debug_pos, &file_check_plist.list) {
        plist_total++;
        file_check_t* fc = td_list_entry(debug_pos, file_check_t, list);
        if (fc->file_stat && fc->file_stat->file_hint) plist_valid++;
    }
    printf("[TD-REG] plist: %d total, %d valid (file_stat && file_hint)\n", plist_total, plist_valid);

    // First pass: collect unique file_hint_t pointers and create validators
    struct td_list_head* pos;
    td_list_for_each(pos, &file_check_plist.list) {
        file_check_t* fc = td_list_entry(pos, file_check_t, list);

        if (fc->file_stat == nullptr || fc->file_stat->file_hint == nullptr) {
            continue;
        }

        const file_hint_t* hint = fc->file_stat->file_hint;

        // Already seen this hint?
        if (hint_to_ext.count(hint)) {
            continue;
        }

        // Create the extension key
        std::wstring ext_wstr = char_to_wstring(hint->extension);
        hint_to_ext[hint] = ext_wstr;
        printf("[TD-REG] Created validator for ext=%s, hint=%p\n", hint->extension, (void*)hint);

        // Create a TestDiskValidator for this hint, passing the file_stat
        // so header_check can filter correctly
        auto validator = std::make_unique<TestDiskValidator>(hint, fc->file_stat);
        g_validators()[ext_wstr] = std::move(validator);
    }
    printf("[TD-REG] Created %zu validators\n", hint_to_ext.size());

    // Step 3: Second pass — create FormatDescriptor for each signature pattern.
    // Each file_check_t entry becomes a separate FormatDescriptor.
    // Multiple entries for the same hint share the same TestDiskValidator.
    int desc_count = 0;
    td_list_for_each(pos, &file_check_plist.list) {
        file_check_t* fc = td_list_entry(pos, file_check_t, list);

        if (fc->file_stat == nullptr || fc->file_stat->file_hint == nullptr) {
            continue;
        }

        const file_hint_t* hint = fc->file_stat->file_hint;
        auto ext_it = hint_to_ext.find(hint);
        if (ext_it == hint_to_ext.end()) {
            continue;
        }

        const std::wstring& ext_wstr = ext_it->second;

        // Create FormatDescriptor
        FormatDescriptor desc;
        desc.file_type = determine_file_type(hint);

        // Store extension in stable storage
        g_stable_strings().push_back(ext_wstr);
        desc.extension = g_stable_strings().back().c_str();

        // Store description in stable storage
        std::wstring desc_wstr = char_to_wstring(hint->description);
        g_stable_strings().push_back(desc_wstr);
        desc.description = g_stable_strings().back().c_str();

        desc.min_filesize = 0;
        desc.max_filesize = hint->max_filesize;
        desc.enabled_by_default = (hint->enable_by_default != 0);

        // Set signature pattern from file_check_t
        if (fc->value != nullptr && fc->length > 0) {
            // Copy signature bytes to stable storage
            const uint8_t* sig_bytes = static_cast<const uint8_t*>(fc->value);
            g_stable_patterns().push_back(std::vector<uint8_t>(sig_bytes, sig_bytes + fc->length));
            desc.signature.pattern = g_stable_patterns().back().data();
            desc.signature.pattern_len = static_cast<uint8_t>(fc->length);
            desc.signature.offset = static_cast<uint8_t>(fc->offset);

            // DEBUG: print first few sig bytes
            char sig_hex[32] = {0};
            for (int k = 0; k < (int)fc->length && k < 8; k++)
                sprintf(sig_hex + k*2, "%02X", sig_bytes[k]);
            printf("[TD-REG] Desc #%d: ext=%s sig=[%s] offset=%u len=%u\n",
                   desc_count, hint->extension, sig_hex, fc->offset, fc->length);
        } else {
            // No signature pattern — this entry cannot be indexed
            desc.signature.pattern = nullptr;
            desc.signature.pattern_len = 0;
            desc.signature.offset = 0;
            printf("[TD-REG] Desc #%d: ext=%s NO SIGNATURE (value=%p, length=%u)\n",
                   desc_count, hint->extension, fc->value, fc->length);
        }

        // Set function pointers — all formats share the same dispatch functions.
        // The dispatch context (g_td_dispatch_ext) is set by the caller
        // (FormatRegistry::match() or SignatureScanner) to identify which
        // FormatDescriptor is being validated, and the dispatch function
        // uses the extension key to find the correct TestDiskValidator.
        desc.header_check = td_dispatch_header_check;
        desc.data_check = td_dispatch_data_check;
        desc.file_check = td_dispatch_file_check;

        desc_count++;
        registry.register_format(desc);
    }
    printf("[TD-REG] Registered %d FormatDescriptors\n", desc_count);
}

} // namespace disk_recover