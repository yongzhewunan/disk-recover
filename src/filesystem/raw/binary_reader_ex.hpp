#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <cstring>

namespace disk_recover {

// ============================================================================
// Stateful BinaryReader with EOF/Error tracking
//
// Unlike the safe functions in binary_reader.hpp which return default values
// on out-of-bounds access (potentially causing infinite loops), this class
// tracks state and allows callers to detect and handle errors properly.
//
// Usage:
//   BinaryReader reader(data, length);
//   auto val = reader.read_le32();
//   if (!reader.ok()) { /* handle error */ }
//   if (!val.has_value()) { /* handle missing value */ }
// ============================================================================

class BinaryReader {
public:
    explicit BinaryReader(const uint8_t* data, size_t length)
        : data_(data), length_(length), pos_(0), error_(false) {}

    explicit BinaryReader(std::span<const std::byte> span)
        : data_(reinterpret_cast<const uint8_t*>(span.data())),
          length_(span.size()), pos_(0), error_(false) {}

    // Check if reader is in a valid state (no errors yet)
    bool ok() const { return !error_; }

    // Check if we've reached end of data
    bool eof() const { return pos_ >= length_; }

    // Get current position
    size_t position() const { return pos_; }

    // Get total length
    size_t size() const { return length_; }

    // Get remaining bytes
    size_t remaining() const { return (pos_ < length_) ? (length_ - pos_) : 0; }

    // Reset error state (e.g., after handling)
    void clear_error() { error_ = false; }

    // Seek to absolute position
    bool seek(size_t pos) {
        if (pos > length_) {
            error_ = true;
            return false;
        }
        pos_ = pos;
        return true;
    }

    // Skip forward by N bytes
    bool skip(size_t n) {
        if (pos_ + n > length_) {
            error_ = true;
            pos_ = length_;
            return false;
        }
        pos_ += n;
        return true;
    }

    // Read a single byte
    std::optional<uint8_t> read_u8() {
        if (pos_ + 1 > length_) {
            error_ = true;
            return std::nullopt;
        }
        return data_[pos_++];
    }

    // Read little-endian 16-bit
    std::optional<uint16_t> read_le16() {
        if (pos_ + 2 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint16_t val = uint16_t(data_[pos_]) | (uint16_t(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return val;
    }

    // Read big-endian 16-bit
    std::optional<uint16_t> read_be16() {
        if (pos_ + 2 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint16_t val = (uint16_t(data_[pos_]) << 8) | uint16_t(data_[pos_ + 1]);
        pos_ += 2;
        return val;
    }

    // Read little-endian 32-bit
    std::optional<uint32_t> read_le32() {
        if (pos_ + 4 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint32_t val = uint32_t(data_[pos_]) |
                      (uint32_t(data_[pos_ + 1]) << 8) |
                      (uint32_t(data_[pos_ + 2]) << 16) |
                      (uint32_t(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return val;
    }

    // Read big-endian 32-bit
    std::optional<uint32_t> read_be32() {
        if (pos_ + 4 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint32_t val = (uint32_t(data_[pos_]) << 24) |
                      (uint32_t(data_[pos_ + 1]) << 16) |
                      (uint32_t(data_[pos_ + 2]) << 8) |
                      uint32_t(data_[pos_ + 3]);
        pos_ += 4;
        return val;
    }

    // Read little-endian 64-bit
    std::optional<uint64_t> read_le64() {
        if (pos_ + 8 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint64_t val = uint64_t(data_[pos_]) |
                      (uint64_t(data_[pos_ + 1]) << 8) |
                      (uint64_t(data_[pos_ + 2]) << 16) |
                      (uint64_t(data_[pos_ + 3]) << 24) |
                      (uint64_t(data_[pos_ + 4]) << 32) |
                      (uint64_t(data_[pos_ + 5]) << 40) |
                      (uint64_t(data_[pos_ + 6]) << 48) |
                      (uint64_t(data_[pos_ + 7]) << 56);
        pos_ += 8;
        return val;
    }

    // Read big-endian 64-bit
    std::optional<uint64_t> read_be64() {
        if (pos_ + 8 > length_) {
            error_ = true;
            return std::nullopt;
        }
        uint64_t val = (uint64_t(data_[pos_]) << 56) |
                      (uint64_t(data_[pos_ + 1]) << 48) |
                      (uint64_t(data_[pos_ + 2]) << 40) |
                      (uint64_t(data_[pos_ + 3]) << 32) |
                      (uint64_t(data_[pos_ + 4]) << 24) |
                      (uint64_t(data_[pos_ + 5]) << 16) |
                      (uint64_t(data_[pos_ + 6]) << 8) |
                      uint64_t(data_[pos_ + 7]);
        pos_ += 8;
        return val;
    }

    // Read a specific number of bytes into a buffer
    bool read_bytes(void* dest, size_t count) {
        if (pos_ + count > length_) {
            error_ = true;
            return false;
        }
        std::memcpy(dest, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    // Peek at data without advancing position
    bool peek_bytes(void* dest, size_t count) const {
        if (pos_ + count > length_) {
            return false;
        }
        std::memcpy(dest, data_ + pos_, count);
        return true;
    }

    // Get a pointer to current position (use with caution)
    const uint8_t* current_ptr() const { return data_ + pos_; }

    // Get a pointer to data at a specific offset (use with caution)
    const uint8_t* ptr_at(size_t offset) const {
        if (offset >= length_) return nullptr;
        return data_ + offset;
    }

private:
    const uint8_t* data_;
    size_t length_;
    size_t pos_;
    bool error_;
};

// ============================================================================
// Non-consuming reader for random access patterns
// ============================================================================

class RandomAccessReader {
public:
    explicit RandomAccessReader(const uint8_t* data, size_t length)
        : data_(data), length_(length), error_(false) {}

    bool ok() const { return !error_; }
    void clear_error() { error_ = false; }
    size_t size() const { return length_; }

    // Check if offset + count is within bounds
    bool check_bounds(size_t offset, size_t count) const {
        if (offset + count > length_ || offset + count < offset) {
            return false;
        }
        return true;
    }

    // Read at specific offset, set error on failure
    std::optional<uint16_t> read_le16(size_t offset) {
        if (!check_bounds(offset, 2)) {
            error_ = true;
            return std::nullopt;
        }
        return uint16_t(data_[offset]) | (uint16_t(data_[offset + 1]) << 8);
    }

    std::optional<uint16_t> read_be16(size_t offset) {
        if (!check_bounds(offset, 2)) {
            error_ = true;
            return std::nullopt;
        }
        return (uint16_t(data_[offset]) << 8) | uint16_t(data_[offset + 1]);
    }

    std::optional<uint32_t> read_le32(size_t offset) {
        if (!check_bounds(offset, 4)) {
            error_ = true;
            return std::nullopt;
        }
        return uint32_t(data_[offset]) |
               (uint32_t(data_[offset + 1]) << 8) |
               (uint32_t(data_[offset + 2]) << 16) |
               (uint32_t(data_[offset + 3]) << 24);
    }

    std::optional<uint32_t> read_be32(size_t offset) {
        if (!check_bounds(offset, 4)) {
            error_ = true;
            return std::nullopt;
        }
        return (uint32_t(data_[offset]) << 24) |
               (uint32_t(data_[offset + 1]) << 16) |
               (uint32_t(data_[offset + 2]) << 8) |
               uint32_t(data_[offset + 3]);
    }

    std::optional<uint64_t> read_be64(size_t offset) {
        if (!check_bounds(offset, 8)) {
            error_ = true;
            return std::nullopt;
        }
        return (uint64_t(data_[offset]) << 56) |
               (uint64_t(data_[offset + 1]) << 48) |
               (uint64_t(data_[offset + 2]) << 40) |
               (uint64_t(data_[offset + 3]) << 32) |
               (uint64_t(data_[offset + 4]) << 24) |
               (uint64_t(data_[offset + 5]) << 16) |
               (uint64_t(data_[offset + 6]) << 8) |
               uint64_t(data_[offset + 7]);
    }

    // Compare bytes at offset with pattern
    bool has_bytes(size_t offset, const uint8_t* pat, size_t pat_len) const {
        if (!check_bounds(offset, pat_len)) return false;
        return std::memcmp(data_ + offset, pat, pat_len) == 0;
    }

    // Compare bytes at offset with literal string (excluding NUL)
    template<size_t N>
    bool has_str(size_t offset, const char (&s)[N]) const {
        return has_bytes(offset, reinterpret_cast<const uint8_t*>(s), N - 1);
    }

    // Get raw pointer at offset
    const uint8_t* ptr(size_t offset) const {
        if (offset >= length_) {
            error_ = true;
            return nullptr;
        }
        return data_ + offset;
    }

private:
    const uint8_t* data_;
    size_t length_;
    mutable bool error_;
};

} // namespace disk_recover
