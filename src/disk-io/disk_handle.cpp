#include "disk_handle.hpp"

namespace disk_recover {

DiskHandle::~DiskHandle() {
    close();
}

DiskHandle::DiskHandle(DiskHandle&& other) noexcept
    : handle_(other.handle_), device_path_(std::move(other.device_path_)) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

DiskHandle& DiskHandle::operator=(DiskHandle&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        device_path_ = std::move(other.device_path_);
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

bool DiskHandle::open(const std::wstring& device_path) {
    close();

    // First try with standard flags
    handle_ = CreateFileW(
        device_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        nullptr);

    if (handle_ == INVALID_HANDLE_VALUE) {
        DWORD err1 = GetLastError();

        // Try with fewer restrictions - without FILE_FLAG_NO_BUFFERING
        handle_ = CreateFileW(
            device_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            DWORD err2 = GetLastError();

            // Try with maximum allowed share mode
            handle_ = CreateFileW(
                device_path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

            if (handle_ == INVALID_HANDLE_VALUE) {
                return false;
            }
        }
    }

    device_path_ = device_path;
    return true;
}

void DiskHandle::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        device_path_.clear();
    }
}

} // namespace disk_recover