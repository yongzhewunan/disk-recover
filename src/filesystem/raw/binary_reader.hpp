#pragma once
#include <cstdint>
#include <cstddef>

namespace disk_recover {

inline uint16_t read_le16(const uint8_t* p) {
    return
        uint16_t(p[0]) |
        (uint16_t(p[1]) << 8);
}

inline uint32_t read_le32(const uint8_t* p) {
    return
        uint32_t(p[0]) |
        (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) |
        (uint32_t(p[3]) << 24);
}

inline uint16_t read_be16(const uint8_t* p) {
    return
        (uint16_t(p[0]) << 8) |
        uint16_t(p[1]);
}

inline uint32_t read_be32(const uint8_t* p) {
    return
        (uint32_t(p[0]) << 24) |
        (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) << 8) |
        uint32_t(p[3]);
}

inline uint64_t read_be64(const uint8_t* p) {
    return
        (uint64_t(p[0]) << 56) |
        (uint64_t(p[1]) << 48) |
        (uint64_t(p[2]) << 40) |
        (uint64_t(p[3]) << 32) |
        (uint64_t(p[4]) << 24) |
        (uint64_t(p[5]) << 16) |
        (uint64_t(p[6]) << 8)  |
        uint64_t(p[7]);
}

inline bool has_bytes(
    const uint8_t* data,
    size_t length,
    size_t offset,
    const uint8_t* pat,
    size_t pat_len)
{
    if (offset + pat_len > length)
        return false;
    for (size_t i = 0; i < pat_len; ++i) {
        if (data[offset + i] != pat[i])
            return false;
    }
    return true;
}

// Check if data[offset..offset+N) equals a literal string (excluding NUL terminator)
template<size_t N>
inline bool has_str(const uint8_t* data, size_t length, size_t offset, const char (&s)[N]) {
    return has_bytes(data, length, offset, reinterpret_cast<const uint8_t*>(s), N - 1);
}

} // namespace disk_recover
