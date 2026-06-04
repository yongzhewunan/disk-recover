add_library(disk_recover_business STATIC
    scan_cache_db.cpp
    scan_manager.cpp
    scan_recover_manager.cpp
    multi_target_writer.cpp
    recovery_manager.cpp
    preview_manager.cpp
)

target_include_directories(disk_recover_business PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(disk_recover_business PUBLIC
    disk_recover_disk_io
    disk_recover_filesystem_raw
    disk_recover_filesystem_ntfs
    disk_recover_filesystem_fat
    disk_recover_filesystem_exfat
    disk_recover_common
    unofficial::sqlite3::sqlite3
    windowscodecs
    ${FFMPEG_LIBRARIES}
)

target_include_directories(disk_recover_business PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)
