#include "aligned_buffer.hpp"
#include <windows.h>
#include <new>

namespace disk_recover {

AlignedBuffer::AlignedBuffer(size_t size, size_t alignment) {
    allocate(size, alignment);
}

AlignedBuffer::~AlignedBuffer() {
    reset();
}

AlignedBuffer::AlignedBuffer(AlignedBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

AlignedBuffer& AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void AlignedBuffer::allocate(size_t size, size_t alignment) {
    reset();
    data_ = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!data_) {
        throw std::bad_alloc();
    }
    size_ = size;
}

void AlignedBuffer::reset() {
    if (data_) {
        VirtualFree(data_, 0, MEM_RELEASE);
        data_ = nullptr;
        size_ = 0;
    }
}

} // namespace disk_recover