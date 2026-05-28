add_subdirectory(cli)

# GUI executable (Win32)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gui/CMakeLists.txt")
    add_subdirectory(gui)
endif()