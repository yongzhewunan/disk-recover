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

    uint64_t write(const uint8_t* data, uint64_t size);
    bool open_file(const std::wstring& relative_path);
    void close_file();

    const std::vector<TargetDisk>& targets() const { return targets_; }
    size_t current_index() const { return current_; }

private:
    bool switch_to_next_target();

    std::vector<TargetDisk> targets_;
    size_t current_ = 0;
    bool auto_switch_ = true;
    void* file_handle_ = nullptr;
};

} // namespace disk_recover
