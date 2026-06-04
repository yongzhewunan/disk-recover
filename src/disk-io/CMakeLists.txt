add_library(disk_recover_disk_io STATIC
    disk_handle.cpp
    disk_info.cpp
    aligned_buffer.cpp
    sector_reader.cpp
    bad_sector_manager.cpp
    buffered_reader.cpp
)
target_include_directories(disk_recover_disk_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_disk_io PUBLIC disk_recover_common)