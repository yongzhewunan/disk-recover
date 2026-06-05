/*

    File: testdisk_adapter.hpp

    Adapter layer to integrate TestDisk/PhotoRec file validators into disk-recover.

    This adapter wraps TestDisk's C-style validation functions into C++ classes
    that implement disk-recover's FormatDescriptor interface.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <functional>
#include "validation.hpp"
#include "types.hpp"

// TestDisk headers
extern "C" {
#include "../external/testdisk/filegen.h"
}

namespace disk_recover {

/**
 * @brief Adapter class that wraps TestDisk validators for disk-recover.
 *
 * This class provides a C++ interface to TestDisk's file validation functions,
 * mapping TestDisk's data_check_t results to disk-recover's ValidateResult enum.
 */
class TestDiskValidator {
public:
    /**
     * @brief Construct a validator from a TestDisk file_hint_t.
     * @param hint Pointer to TestDisk's file_hint_t structure.
     */
    explicit TestDiskValidator(const file_hint_t* hint);

    /**
     * @brief Destructor.
     */
    ~TestDiskValidator();

    // Non-copyable
    TestDiskValidator(const TestDiskValidator&) = delete;
    TestDiskValidator& operator=(const TestDiskValidator&) = delete;

    // Movable
    TestDiskValidator(TestDiskValidator&& other) noexcept;
    TestDiskValidator& operator=(TestDiskValidator&& other) noexcept;

    /**
     * @brief Perform header check validation.
     *
     * This wraps TestDisk's header_check function, mapping the result
     * to disk-recover's ValidateResult enum.
     *
     * @param data Buffer containing file header data.
     * @param length Length of the buffer.
     * @param calculated_file_size Output parameter for calculated file size.
     * @return ValidateResult indicating the validation outcome.
     */
    ValidateResult header_check(const uint8_t* data, size_t length,
                                uint64_t& calculated_file_size);

    /**
     * @brief Perform data check validation (progressive carving).
     *
     * This wraps TestDisk's data_check function for incremental validation
     * during file carving.
     *
     * @param data Buffer containing file data block.
     * @param length Length of the buffer.
     * @param offset_in_file Offset of this block within the file.
     * @param calculated_file_size Output parameter for calculated file size.
     * @return ValidateResult indicating the validation outcome.
     */
    ValidateResult data_check(const uint8_t* data, size_t length,
                              uint64_t offset_in_file,
                              uint64_t& calculated_file_size);

    /**
     * @brief Perform final file check validation.
     *
     * This wraps TestDisk's file_check function for complete file validation.
     *
     * @param data Buffer containing complete file data.
     * @param length Length of the buffer.
     * @param calculated_file_size Output parameter for calculated file size.
     * @return ValidateResult indicating the validation outcome.
     */
    ValidateResult file_check(const uint8_t* data, size_t length,
                              uint64_t& calculated_file_size);

    /**
     * @brief Get the file extension for this validator.
     * @return Wide string containing the file extension.
     */
    const std::wstring& extension() const { return extension_; }

    /**
     * @brief Get the file description for this validator.
     * @return Wide string containing the file description.
     */
    const std::wstring& description() const { return description_; }

    /**
     * @brief Get the maximum file size for this format.
     * @return Maximum file size in bytes.
     */
    uint64_t max_filesize() const { return max_filesize_; }

    /**
     * @brief Get the minimum file size for this format.
     * @return Minimum file size in bytes.
     */
    uint64_t min_filesize() const { return min_filesize_; }

    /**
     * @brief Check if this validator is enabled by default.
     * @return true if enabled by default, false otherwise.
     */
    bool enabled_by_default() const { return enabled_by_default_; }

    /**
     * @brief Check if this validator has a data_check function.
     * @return true if data_check is available.
     */
    bool has_data_check() const { return has_data_check_; }

    /**
     * @brief Check if this validator has a file_check function.
     * @return true if file_check is available.
     */
    bool has_file_check() const { return has_file_check_; }

private:
    const file_hint_t* hint_;          ///< Pointer to TestDisk's file hint
    file_recovery_t* recovery_ctx_;    ///< TestDisk's recovery context
    file_stat_t* file_stat_;           ///< TestDisk's file statistics

    // Cached values
    std::wstring extension_;
    std::wstring description_;
    uint64_t max_filesize_;
    uint64_t min_filesize_;
    bool enabled_by_default_;
    bool has_data_check_;
    bool has_file_check_;

    // Internal buffer for file data (used in file_check)
    std::vector<uint8_t> file_buffer_;

    /**
     * @brief Map TestDisk's data_check_t to ValidateResult.
     */
    static ValidateResult map_result(data_check_t dc);

    /**
     * @brief Map TestDisk's header_check return value to ValidateResult.
     */
    static ValidateResult map_header_result(int result);

    /**
     * @brief Initialize the recovery context.
     */
    void init_recovery_context();

    /**
     * @brief Update cached values from the hint.
     */
    void update_cached_values();
};

/**
 * @brief Factory function to create a validator for a specific format.
 *
 * @param hint Pointer to TestDisk's file_hint_t structure.
 * @return Unique pointer to a TestDiskValidator instance.
 */
std::unique_ptr<TestDiskValidator> create_validator(const file_hint_t* hint);

/**
 * @brief Macro to declare a TestDisk validator.
 *
 * Usage: DECLARE_TESTDISK_VALIDATOR(jpg)
 */
#define DECLARE_TESTDISK_VALIDATOR(name) \
    extern const file_hint_t file_hint_##name; \
    std::unique_ptr<TestDiskValidator> create_##name##_validator()

/**
 * @brief Macro to define a TestDisk validator factory function.
 *
 * Usage: DEFINE_TESTDISK_VALIDATOR(jpg)
 */
#define DEFINE_TESTDISK_VALIDATOR(name) \
    std::unique_ptr<TestDiskValidator> create_##name##_validator() { \
        return create_validator(&file_hint_##name); \
    }

} // namespace disk_recover