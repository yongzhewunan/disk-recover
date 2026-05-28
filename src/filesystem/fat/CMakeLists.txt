add_library(disk_recover_filesystem_fat STATIC
    fat_parser.cpp
)
target_include_directories(disk_recover_filesystem_fat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disk_recover_filesystem_fat PUBLIC disk_recover_disk_io)
