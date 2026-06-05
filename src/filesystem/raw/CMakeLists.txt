# Validator sources
set(VALIDATOR_SOURCES
    validators/bmp_validator.cpp
    validators/jpeg_validator.cpp
    validators/png_validator.cpp
    validators/gif_validator.cpp
    validators/tiff_validator.cpp
    validators/bmff_validator.cpp
    validators/riff_validator.cpp
    validators/ebml_validator.cpp
    validators/ts_validator.cpp
    validators/wmv_validator.cpp
    validators/flv_validator.cpp
    validators/pdf_validator.cpp
    validators/zip_validator.cpp
    validators/sevenz_validator.cpp
    validators/rar_validator.cpp
    validators/doc_validator.cpp
    validators/mp3_validator.cpp
    validators/flac_validator.cpp
)

add_library(disk_recover_filesystem_raw STATIC
    format_registry.cpp
    validator_linkage.cpp
    file_signatures.cpp
    signature_scanner.cpp
    ${VALIDATOR_SOURCES}
)

# Prevent MSVC linker from stripping .obj files from the static library
# that contain the FormatDescriptor definitions needed by register_all_formats()
if(MSVC)
    target_link_options(disk_recover_filesystem_raw PUBLIC /OPT:NOREF)
endif()

target_include_directories(disk_recover_filesystem_raw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_filesystem_raw PUBLIC disk_recover_common disk_recover_disk_io)
