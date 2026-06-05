/*

    File: testdisk_adapter.cpp

    Implementation of TestDisk validator adapter for disk-recover.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#include "testdisk_adapter.hpp"
#include <cstring>
#include <algorithm>
#include <stdexcept>

// Windows API for temporary file operations in file_check
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

extern "C" {
#include "../external/testdisk/common.h"
}

namespace disk_recover {

// ============================================================================
// Dispatch context — defined here, declared extern in header
// ============================================================================
const wchar_t* g_td_dispatch_ext = nullptr;

// ============================================================================
// Result Mapping Functions
// ============================================================================

ValidateResult TestDiskValidator::map_result(data_check_t dc) {
    switch (dc) {
        case DC_STOP:
            // TestDisk indicates file boundary found, file is complete
            return ValidateResult::AcceptVerified;
        case DC_ERROR:
            // TestDisk detected invalid data, reject file
            return ValidateResult::Reject;
        case DC_CONTINUE:
            // TestDisk says continue scanning, structure looks valid
            return ValidateResult::AcceptStructure;
        case DC_SCAN:
            // Default scanning state, header matched but no structure validation yet
            return ValidateResult::AcceptHeader;
        default:
            return ValidateResult::AcceptHeader;
    }
}

ValidateResult TestDiskValidator::map_header_result(int result) {
    // TestDisk's header_check returns 1 for success, 0 for failure
    if (result == 1) {
        return ValidateResult::AcceptHeader;
    }
    return ValidateResult::Reject;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

TestDiskValidator::TestDiskValidator(const file_hint_t* hint, file_stat_t* file_stat)
    : hint_(hint)
    , recovery_ctx_(nullptr)
    , file_stat_(file_stat)
    , max_filesize_(0)
    , min_filesize_(0)
    , enabled_by_default_(false)
    , has_data_check_(false)
    , has_file_check_(false)
{
    if (hint_ == nullptr) {
        throw std::invalid_argument("TestDiskValidator: hint cannot be null");
    }

    // Allocate recovery context
    recovery_ctx_ = (file_recovery_t*)MALLOC(sizeof(file_recovery_t));
    if (recovery_ctx_ == nullptr) {
        throw std::bad_alloc();
    }

    // Note: file_stat_ may be null if called from old constructor path.
    // In that case, allocate one.
    if (file_stat_ == nullptr) {
        file_stat_ = (file_stat_t*)MALLOC(sizeof(file_stat_t));
        if (file_stat_ == nullptr) {
            free(recovery_ctx_);
            throw std::bad_alloc();
        }
        file_stat_->not_recovered = 0;
        file_stat_->recovered = 0;
        file_stat_->file_hint = hint_;
    }

    // Initialize structures
    reset_file_recovery(recovery_ctx_);

    // Update cached values from hint
    update_cached_values();

    // Initialize recovery context
    init_recovery_context();
}

TestDiskValidator::~TestDiskValidator() {
    if (recovery_ctx_ != nullptr) {
        // Free any allocated location data
        if (recovery_ctx_->handle != nullptr) {
            fclose(recovery_ctx_->handle);
        }
        free(recovery_ctx_);
    }
    // Note: file_stat_ is owned by td_validators_init.c (static storage),
    // so we don't free it here. If we allocated it ourselves (null param
    // constructor), we would need to free it — but we don't track that.
    // For now, the static file_stats in td_validators_init.c outlive
    // everything, so this is safe.
}

// ============================================================================
// Move Operations
// ============================================================================

TestDiskValidator::TestDiskValidator(TestDiskValidator&& other) noexcept
    : hint_(other.hint_)
    , recovery_ctx_(other.recovery_ctx_)
    , file_stat_(other.file_stat_)
    , extension_(std::move(other.extension_))
    , description_(std::move(other.description_))
    , max_filesize_(other.max_filesize_)
    , min_filesize_(other.min_filesize_)
    , enabled_by_default_(other.enabled_by_default_)
    , has_data_check_(other.has_data_check_)
    , has_file_check_(other.has_file_check_)
    , file_buffer_(std::move(other.file_buffer_))
{
    // Reset other's pointers to prevent double-free
    other.hint_ = nullptr;
    other.recovery_ctx_ = nullptr;
    other.file_stat_ = nullptr;
}

TestDiskValidator& TestDiskValidator::operator=(TestDiskValidator&& other) noexcept {
    if (this != &other) {
        // Free current resources
        if (recovery_ctx_ != nullptr) {
            if (recovery_ctx_->handle != nullptr) {
                fclose(recovery_ctx_->handle);
            }
            free(recovery_ctx_);
        }

        // Move from other
        hint_ = other.hint_;
        recovery_ctx_ = other.recovery_ctx_;
        file_stat_ = other.file_stat_;
        extension_ = std::move(other.extension_);
        description_ = std::move(other.description_);
        max_filesize_ = other.max_filesize_;
        min_filesize_ = other.min_filesize_;
        enabled_by_default_ = other.enabled_by_default_;
        has_data_check_ = other.has_data_check_;
        has_file_check_ = other.has_file_check_;
        file_buffer_ = std::move(other.file_buffer_);

        // Reset other's pointers
        other.hint_ = nullptr;
        other.recovery_ctx_ = nullptr;
        other.file_stat_ = nullptr;
    }
    return *this;
}

// ============================================================================
// Helper Functions
// ============================================================================

void TestDiskValidator::update_cached_values() {
    if (hint_ == nullptr) return;

    // Convert extension from char* to wchar_t*
    if (hint_->extension != nullptr) {
        size_t len = strlen(hint_->extension);
        extension_.resize(len);
        for (size_t i = 0; i < len; ++i) {
            extension_[i] = static_cast<wchar_t>(hint_->extension[i]);
        }
    }

    // Convert description from char* to wchar_t*
    if (hint_->description != nullptr) {
        size_t len = strlen(hint_->description);
        description_.resize(len);
        for (size_t i = 0; i < len; ++i) {
            description_[i] = static_cast<wchar_t>(hint_->description[i]);
        }
    }

    // Copy size limits
    max_filesize_ = hint_->max_filesize;
    min_filesize_ = 0; // TestDisk doesn't have min_filesize in file_hint_t

    // Check enable status
    enabled_by_default_ = (hint_->enable_by_default != 0);

    // Check available validation functions
    // These will be set after header_check is called
    has_data_check_ = false;
    has_file_check_ = false;
}

void TestDiskValidator::init_recovery_context() {
    if (recovery_ctx_ == nullptr || hint_ == nullptr) return;

    // Reset to clean state
    reset_file_recovery(recovery_ctx_);

    // Set file statistics pointer
    recovery_ctx_->file_stat = file_stat_;

    // Set initial extension from hint
    recovery_ctx_->extension = hint_->extension;

    // Set blocksize (default 512 for disk-recover)
    recovery_ctx_->blocksize = 512;

    // Initialize size tracking
    recovery_ctx_->file_size = 0;
    recovery_ctx_->calculated_file_size = 0;
    recovery_ctx_->min_filesize = 0;
}

// ============================================================================
// Validation Functions
// ============================================================================

ValidateResult TestDiskValidator::header_check(const uint8_t* data, size_t length,
                                               uint64_t& calculated_file_size) {
    if (hint_ == nullptr || recovery_ctx_ == nullptr || data == nullptr || length == 0) {
        calculated_file_size = 0;
        return ValidateResult::Reject;
    }

    // Reset context for new validation
    init_recovery_context();

    // Create a new recovery context for output
    file_recovery_t* file_recovery_new = (file_recovery_t*)MALLOC(sizeof(file_recovery_t));
    if (file_recovery_new == nullptr) {
        calculated_file_size = 0;
        return ValidateResult::Reject;
    }

    reset_file_recovery(file_recovery_new);
    file_recovery_new->file_stat = file_stat_;
    file_recovery_new->extension = hint_->extension;
    file_recovery_new->blocksize = 512;

    // Walk the global header check list to find matching patterns
    // registered by this hint's register_header_check function.
    // We compare signature bytes and call the header_check callback.
    struct td_list_head *pos;
    int result = 0;
    int checked_count = 0;

    td_list_for_each(pos, &file_check_plist.list) {
        file_check_t *fc = td_list_entry(pos, file_check_t, list);

        // Check if this check belongs to our file_stat
        if (fc->file_stat != file_stat_) {
            continue;
        }

        checked_count++;

        // Check if the signature matches
        if (fc->offset + fc->length <= length) {
            int cmp = memcmp(data + fc->offset, fc->value, fc->length);
            printf("[TD-HC] ext=%s sig_cmp=%d (offset=%u, len=%u)\n",
                   hint_->extension, cmp, fc->offset, fc->length);
            if (cmp == 0) {
                // Signature matched, call the header_check function
                // Debug: print first 32 bytes of data
                printf("[TD-HC] ext=%s data[0..31]=", hint_->extension);
                for (int k = 0; k < 32 && k < (int)length; k++)
                    printf("%02X", data[k]);
                printf("\n");
                result = fc->header_check(
                    data,
                    static_cast<unsigned int>(length),
                    0,  // safe_header_only = 0
                    recovery_ctx_,
                    file_recovery_new
                );

                printf("[TD-HC] ext=%s sig_match=1 result=%d\n", hint_->extension, result);

                if (result == 1) {
                    // Header check passed
                    // Copy results from the new context
                    recovery_ctx_->data_check = file_recovery_new->data_check;
                    recovery_ctx_->file_check = file_recovery_new->file_check;
                    recovery_ctx_->file_rename = file_recovery_new->file_rename;
                    recovery_ctx_->calculated_file_size = file_recovery_new->calculated_file_size;
                    recovery_ctx_->min_filesize = file_recovery_new->min_filesize;
                    recovery_ctx_->extension = file_recovery_new->extension;
                    recovery_ctx_->time = file_recovery_new->time;
                    recovery_ctx_->flags = file_recovery_new->flags;

                    // Update flags
                    has_data_check_ = (recovery_ctx_->data_check != nullptr);
                    has_file_check_ = (recovery_ctx_->file_check != nullptr);

                    calculated_file_size = recovery_ctx_->calculated_file_size;
                    free(file_recovery_new);
                    return ValidateResult::AcceptHeader;
                }
            }
        }
    }

    // No matching signature found or header check failed
    printf("[TD-HC] ext=%s checked=%d entries, all rejected\n", hint_->extension, checked_count);
    calculated_file_size = 0;
    free(file_recovery_new);
    return ValidateResult::Reject;
}

ValidateResult TestDiskValidator::data_check(const uint8_t* data, size_t length,
                                             uint64_t offset_in_file,
                                             uint64_t& calculated_file_size) {
    if (recovery_ctx_ == nullptr || data == nullptr || length == 0) {
        calculated_file_size = 0;
        return ValidateResult::Reject;
    }

    // Check if data_check callback is available
    if (recovery_ctx_->data_check == nullptr) {
        // No data_check function, return current state
        calculated_file_size = recovery_ctx_->calculated_file_size;
        return ValidateResult::AcceptHeader;
    }

    // Update file_size to current offset + block size
    recovery_ctx_->file_size = offset_in_file + length;

    // Call TestDisk's data_check function
    data_check_t result = recovery_ctx_->data_check(
        data,
        static_cast<unsigned int>(length),
        recovery_ctx_
    );

    // Extract calculated file size from context
    calculated_file_size = recovery_ctx_->calculated_file_size;

    // Update has_data_check flag
    has_data_check_ = true;

    return map_result(result);
}

ValidateResult TestDiskValidator::file_check(const uint8_t* data, size_t length,
                                             uint64_t& calculated_file_size) {
    if (recovery_ctx_ == nullptr || data == nullptr || length == 0) {
        calculated_file_size = 0;
        return ValidateResult::Reject;
    }

    // Check if file_check callback is available
    if (recovery_ctx_->file_check == nullptr) {
        // No file_check function, use calculated_file_size from context
        calculated_file_size = recovery_ctx_->calculated_file_size;
        return ValidateResult::AcceptStructure;
    }

    // TestDisk's file_check functions operate on FILE* handles.
    // We create a temporary file from the in-memory buffer,
    // set recovery_ctx_->handle, call file_check, then clean up.

    // Generate temporary file path
    char tmp_path[MAX_PATH];
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "tdr", 0, tmp_path);

    // Write buffer to temporary file
    FILE* tmp_file = fopen(tmp_path, "wb");
    if (tmp_file == nullptr) {
        calculated_file_size = recovery_ctx_->calculated_file_size;
        return ValidateResult::AcceptStructure;
    }

    size_t written = fwrite(data, 1, length, tmp_file);
    fclose(tmp_file);

    if (written != length) {
        // Failed to write complete data
        DeleteFileA(tmp_path);
        calculated_file_size = recovery_ctx_->calculated_file_size;
        return ValidateResult::AcceptStructure;
    }

    // Open for reading (TestDisk's file_check reads from handle)
    tmp_file = fopen(tmp_path, "rb");
    if (tmp_file == nullptr) {
        DeleteFileA(tmp_path);
        calculated_file_size = recovery_ctx_->calculated_file_size;
        return ValidateResult::AcceptStructure;
    }

    // Set up recovery context for file_check
    recovery_ctx_->handle = tmp_file;
    recovery_ctx_->file_size = length;

    // Call TestDisk's file_check
    recovery_ctx_->file_check(recovery_ctx_);

    // Clean up
    fclose(tmp_file);
    recovery_ctx_->handle = nullptr;
    DeleteFileA(tmp_path);

    // Extract final file size
    calculated_file_size = recovery_ctx_->calculated_file_size;
    if (calculated_file_size == 0) {
        calculated_file_size = recovery_ctx_->file_size;
    }

    // Update has_file_check flag
    has_file_check_ = true;

    // If file_check modified file_size to calculated_file_size, it's verified
    if (recovery_ctx_->file_size == recovery_ctx_->calculated_file_size ||
        recovery_ctx_->calculated_file_size > 0) {
        return ValidateResult::AcceptVerified;
    }

    return ValidateResult::AcceptStructure;
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<TestDiskValidator> create_validator(const file_hint_t* hint, file_stat_t* file_stat) {
    return std::make_unique<TestDiskValidator>(hint, file_stat);
}

} // namespace disk_recover