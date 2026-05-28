add_library(disk_recover_filesystem_raw STATIC
    file_signatures.cpp
    signature_scanner.cpp
)
target_include_directories(disk_recover_filesystem_raw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_filesystem_raw PUBLIC disk_recover_common disk_recover_disk_io)
