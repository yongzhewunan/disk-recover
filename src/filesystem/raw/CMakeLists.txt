# Validator sources
set(VALIDATOR_SOURCES
    validators/jpeg_validator.cpp
    validators/png_validator.cpp
    validators/gif_validator.cpp
    validators/tiff_validator.cpp
    validators/bmff_validator.cpp
    validators/riff_validator.cpp
    validators/ebml_validator.cpp
    validators/ts_validator.cpp
)

add_library(disk_recover_filesystem_raw STATIC
    file_signatures.cpp
    signature_scanner.cpp
    ${VALIDATOR_SOURCES}
)

target_include_directories(disk_recover_filesystem_raw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_filesystem_raw PUBLIC disk_recover_common disk_recover_disk_io)
