#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace disk_recover {

struct TargetDisk {
    std::wstring path;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
};

class MultiTargetWriter {
public:
    void add_target(const std::wstring& path);
    void remove_target(const std::wstring& path);
    void refresh_space_info();

    std::wstring current_target() const;
    bool has_space(uint64_t required_bytes) const;
    bool auto_switch_enabled() const { return auto_switch_; }
    void set_auto_switch(bool enabled) { auto_switch_ = enabled; }

    // Minimum free space threshold for has_space() and switch_to_next_target()
    // Default: 2GB. Targets with less free space are considered "full".
    void set_min_free_space(uint64_t bytes) { min_free_space_ = bytes; }
    uint64_t min_free_space() const { return min_free_space_; }

    uint64_t write(const uint8_t* data, uint64_t size);
    bool open_file(const std::wstring& relative_path);
    void close_file();
    bool switch_to_next_target();

    const std::vector<TargetDisk>& targets() const { return targets_; }
    size_t current_index() const { return current_; }

private:
    std::vector<TargetDisk> targets_;
    size_t current_ = 0;
    bool auto_switch_ = true;
    uint64_t min_free_space_ = 2ULL * 1024 * 1024 * 1024;  // 2GB default
    void* file_handle_ = nullptr;
};

} // namespace disk_recover