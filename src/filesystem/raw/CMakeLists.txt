# Only legacy validator without a TestDisk equivalent
set(VALIDATOR_SOURCES
    validators/ts_validator.cpp
)

# TestDisk adapter sources
set(TESTDISK_ADAPTER_SOURCES
    testdisk_adapter.cpp
    testdisk_validators.cpp
)

add_library(disk_recover_filesystem_raw STATIC
    format_registry.cpp
    validator_linkage.cpp
    signature_scanner.cpp
    ${VALIDATOR_SOURCES}
    ${TESTDISK_ADAPTER_SOURCES}
)

# Prevent MSVC linker from stripping .obj files from the static library
# that contain the FormatDescriptor definitions needed by register_all_formats()
if(MSVC)
    target_link_options(disk_recover_filesystem_raw PUBLIC /OPT:NOREF)
endif()

target_include_directories(disk_recover_filesystem_raw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_filesystem_raw PUBLIC disk_recover_common disk_recover_disk_io disk_recover_testdisk)
