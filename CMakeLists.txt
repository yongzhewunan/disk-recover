cmake_minimum_required(VERSION 3.21)
project(disk-recover VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CLI11 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(GTest CONFIG REQUIRED)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
