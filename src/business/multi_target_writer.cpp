#define NOMINMAX
#include "multi_target_writer.hpp"
#include <windows.h>
#include <filesystem>
#include <algorithm>

namespace disk_recover {

void MultiTargetWriter::add_target(const std::wstring& path) {
    TargetDisk td;
    td.path = path;
    ULARGE_INTEGER free_avail, total, free_total;
    if (GetDiskFreeSpaceExW(path.c_str(), &free_avail, &total, &free_total)) {
        td.total_bytes = total.QuadPart;
        td.free_bytes = free_avail.QuadPart;
    }
    targets_.push_back(std::move(td));
}

void MultiTargetWriter::remove_target(const std::wstring& path) {
    targets_.erase(
        std::remove_if(targets_.begin(), targets_.end(),
            [&](const TargetDisk& t) { return t.path == path; }),
        targets_.end());
}

void MultiTargetWriter::refresh_space_info() {
    for (auto& td : targets_) {
        ULARGE_INTEGER free_avail, total, free_total;
        if (GetDiskFreeSpaceExW(td.path.c_str(), &free_avail, &total, &free_total)) {
            td.free_bytes = free_avail.QuadPart;
        }
    }
}

std::wstring MultiTargetWriter::current_target() const {
    if (current_ < targets_.size()) return targets_[current_].path;
    return {};
}

bool MultiTargetWriter::has_space(uint64_t required_bytes) const {
    if (current_ >= targets_.size()) return false;
    return targets_[current_].free_bytes >= required_bytes;
}

bool MultiTargetWriter::open_file(const std::wstring& relative_path) {
    close_file();
    if (current_ >= targets_.size()) return false;

    std::wstring full_path = targets_[current_].path + L"\\" + relative_path;
    std::filesystem::create_directories(
        std::filesystem::path(full_path).parent_path());

    HANDLE h = CreateFileW(full_path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (auto_switch_ && switch_to_next_target()) {
            return open_file(relative_path);
        }
        return false;
    }
    file_handle_ = h;
    return true;
}

void MultiTargetWriter::close_file() {
    if (file_handle_) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
        file_handle_ = nullptr;
    }
}

uint64_t MultiTargetWriter::write(const uint8_t* data, uint64_t size) {
    if (current_ >= targets_.size()) return 0;

    if (!has_space(size) && auto_switch_) {
        if (!switch_to_next_target()) return 0;
    }

    DWORD written = 0;
    HANDLE h = static_cast<HANDLE>(file_handle_);
    if (!h) return 0;
    WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
    return written;
}

bool MultiTargetWriter::switch_to_next_target() {
    for (size_t i = 0; i < targets_.size(); ++i) {
        size_t next = (current_ + 1 + i) % targets_.size();
        refresh_space_info();
        if (targets_[next].free_bytes > 0) {
            current_ = next;
            return true;
        }
    }
    return false;
}

} // namespace disk_recover
