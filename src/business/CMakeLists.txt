add_library(disk_recover_business STATIC
    scan_cache_db.cpp
    scan_manager.cpp
)

target_include_directories(disk_recover_business PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(disk_recover_business PUBLIC
    disk_recover_disk_io
    disk_recover_filesystem_raw
    disk_recover_common
    unofficial::sqlite3::sqlite3
)
