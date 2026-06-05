# TestDisk core sources - always compiled
set(TESTDISK_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/filegen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/memmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/setdate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/td_stubs.c
)

# Each validator is compiled as a separate source with SINGLE_FORMAT_xxx defined
# This avoids duplicate helper function issues (get_be32, get_be64, etc.)
# We define both SINGLE_FORMAT and SINGLE_FORMAT_xxx to match TestDisk's pattern
function(add_validator_source VAR_NAME FORMAT_NAME)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/validators/file_${FORMAT_NAME}.c
        PROPERTIES
        COMPILE_DEFINITIONS "SINGLE_FORMAT;SINGLE_FORMAT_${FORMAT_NAME}"
        LANGUAGE C
    )
    list(APPEND ${VAR_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/validators/file_${FORMAT_NAME}.c
    )
    set(${VAR_NAME} ${${VAR_NAME}} PARENT_SCOPE)
endfunction()

# Core validator sources
set(TESTDISK_VALIDATOR_SOURCES "")

# Adobe family (priority)
add_validator_source(TESTDISK_VALIDATOR_SOURCES psd)
add_validator_source(TESTDISK_VALIDATOR_SOURCES psb)
add_validator_source(TESTDISK_VALIDATOR_SOURCES abr)
add_validator_source(TESTDISK_VALIDATOR_SOURCES indd)
add_validator_source(TESTDISK_VALIDATOR_SOURCES pdf)
add_validator_source(TESTDISK_VALIDATOR_SOURCES afdesign)

# Image formats
add_validator_source(TESTDISK_VALIDATOR_SOURCES jpg)
add_validator_source(TESTDISK_VALIDATOR_SOURCES bmp)
add_validator_source(TESTDISK_VALIDATOR_SOURCES png)
add_validator_source(TESTDISK_VALIDATOR_SOURCES gif)
add_validator_source(TESTDISK_VALIDATOR_SOURCES tiff)
add_validator_source(TESTDISK_VALIDATOR_SOURCES tiff_be)
add_validator_source(TESTDISK_VALIDATOR_SOURCES tiff_le)

# Video formats
add_validator_source(TESTDISK_VALIDATOR_SOURCES mov)
add_validator_source(TESTDISK_VALIDATOR_SOURCES riff)
add_validator_source(TESTDISK_VALIDATOR_SOURCES mkv)
add_validator_source(TESTDISK_VALIDATOR_SOURCES flv)
add_validator_source(TESTDISK_VALIDATOR_SOURCES asf)

# Audio formats
add_validator_source(TESTDISK_VALIDATOR_SOURCES flac)
add_validator_source(TESTDISK_VALIDATOR_SOURCES mp3)

# Document formats
add_validator_source(TESTDISK_VALIDATOR_SOURCES doc)

# Archive formats
add_validator_source(TESTDISK_VALIDATOR_SOURCES zip)
add_validator_source(TESTDISK_VALIDATOR_SOURCES 7z)
add_validator_source(TESTDISK_VALIDATOR_SOURCES rar)

# Registration initialization source (compiled without SINGLE_FORMAT)
set(TESTDISK_INIT_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/td_validators_init.c
)

# Create TestDisk library
add_library(disk_recover_testdisk STATIC
    ${TESTDISK_CORE_SOURCES}
    ${TESTDISK_VALIDATOR_SOURCES}
    ${TESTDISK_INIT_SOURCE}
)

# Include directories
target_include_directories(disk_recover_testdisk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk
    ${CMAKE_CURRENT_SOURCE_DIR}/testdisk/validators
)

# Compile as C code
set_target_properties(disk_recover_testdisk PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Link with common utilities
target_link_libraries(disk_recover_testdisk PUBLIC disk_recover_common)