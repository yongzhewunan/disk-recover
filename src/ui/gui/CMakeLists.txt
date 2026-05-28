# GUI executable for disk-recover
cmake_minimum_required(VERSION 3.20)

add_executable(disk-recover
    main.cpp
    main_window.cpp
    main_window.hpp
    resource.h
    resource.rc
)

target_include_directories(disk-recover PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(disk-recover PRIVATE
    disk_recover_business
    disk_recover_disk_io
    disk_recover_common
    comctl32   # Common controls (ListView, StatusBar, etc.)
)

# Win32 GUI application (no console window)
set_target_properties(disk-recover PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "disk-recover"
)

# Link with Windows resources
if(MSVC)
    # Enable Unicode
    target_compile_definitions(disk-recover PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
