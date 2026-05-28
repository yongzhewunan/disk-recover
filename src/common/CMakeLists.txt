add_library(disk_recover_common STATIC
    utils.cpp
)
target_include_directories(disk_recover_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
