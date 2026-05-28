#pragma once
#include <string>
#include <windows.h>

namespace disk_recover {

class DiskHandle {
public:
    DiskHandle() = default;
    ~DiskHandle();

    DiskHandle(const DiskHandle&) = delete;
    DiskHandle& operator=(const DiskHandle&) = delete;
    DiskHandle(DiskHandle&& other) noexcept;
    DiskHandle& operator=(DiskHandle&& other) noexcept;

    bool open(const std::wstring& device_path);
    void close();

    bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE native_handle() const { return handle_; }
    const std::wstring& device_path() const { return device_path_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring device_path_;
};

} // namespace disk_recover