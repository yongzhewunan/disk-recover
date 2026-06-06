# Tools for disk-recover project

add_executable(sample_collector sample_collector.cpp)
target_link_libraries(sample_collector PRIVATE
    disk_recover_filesystem_raw
    disk_recover_common
)