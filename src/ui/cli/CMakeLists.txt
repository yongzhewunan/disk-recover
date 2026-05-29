add_executable(disk-recover-cli main.cpp)
target_link_libraries(disk-recover-cli PRIVATE
    disk_recover_business
    disk_recover_disk_io
    disk_recover_common
    CLI11::CLI11
    gdiplus
)
