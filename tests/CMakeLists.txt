add_executable(disk_recover_tests
    aligned_buffer_test.cpp
    disk_handle_test.cpp
    bad_sector_test.cpp
)
target_link_libraries(disk_recover_tests PRIVATE
    disk_recover_disk_io
    disk_recover_common
    GTest::gtest_main
)

enable_testing()
add_test(NAME disk_recover_tests COMMAND disk_recover_tests)