/*

    File: testdisk_validators.hpp

    Registration of TestDisk validators into disk-recover's FormatRegistry.

    After td_register_all_validators() populates the global file_check_plist
    with signature patterns, this module walks the plist and creates
    FormatDescriptor entries for each signature, with per-format dispatch
    function pointers that bridge to TestDiskValidator instances.

    Copyright (C) 2024
    Based on TestDisk/PhotoRec by Christophe GRENIER

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

 */

#pragma once

#include "format_registry.hpp"

namespace disk_recover {

/**
 * @brief Register all TestDisk validators into FormatRegistry.
 *
 * This function:
 * 1. Calls td_register_all_validators() to populate the global
 *    file_check_plist linked list with signature patterns.
 * 2. Walks file_check_plist to extract signature bytes (value, length, offset).
 * 3. Creates TestDiskValidator instances for each file_hint_t.
 * 4. Creates FormatDescriptor entries for each signature pattern,
 *    with per-format dispatch wrappers for header_check/data_check/file_check.
 *
 * @param registry The FormatRegistry to register with.
 */
void register_testdisk_validators(FormatRegistry& registry);

} // namespace disk_recover