#pragma once
#include <cstdint>
#include <cstddef>

namespace disk_recover {

class AlignedBuffer {
public:
    AlignedBuffer() = default;
    AlignedBuffer(size_t size, size_t alignment);
    ~AlignedBuffer();

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;

    void allocate(size_t size, size_t alignment);
    void reset();

    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return data_ == nullptr; }

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace disk_recover