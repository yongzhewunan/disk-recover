// Ensure NOMINMAX is defined before including Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "format_registry.hpp"
#include <cstring>
#include <algorithm>

namespace disk_recover {

FormatRegistry& FormatRegistry::instance() {
    static FormatRegistry registry;
    // Explicitly register all format descriptors on first call.
    // This replaces unreliable auto-registration via static initializers
    // which were stripped by MSVC's linker in Release builds.
    static bool once = []() { register_all_formats(registry); return true; }();
    (void)once;
    return registry;
}

void FormatRegistry::register_format(const FormatDescriptor& descriptor) {
    formats_.push_back(descriptor);
    indexed_ = false;  // Index needs rebuild after registration
}

void FormatRegistry::build_index() {
    // Clear existing index
    for (auto& bucket : first_byte_index_) {
        bucket.clear();
    }
    offset_signatures_.clear();

    for (const auto& fmt : formats_) {
        if (fmt.signature.pattern == nullptr || fmt.signature.pattern_len == 0) {
            // No signature pattern — add to all buckets as fallback
            // (This shouldn't normally happen, but handle gracefully)
            continue;
        }

        if (fmt.signature.offset == 0) {
            // Pattern at offset 0: index by first byte of pattern
            uint8_t first_byte = fmt.signature.pattern[0];
            first_byte_index_[first_byte].push_back(&fmt);
        } else {
            // Pattern at non-zero offset: add to offset_signatures_ for secondary probing
            offset_signatures_.push_back(&fmt);
        }
    }

    indexed_ = true;
}

void FormatRegistry::rebuild_index() {
    build_index();
}

const std::vector<const FormatDescriptor*>& FormatRegistry::lookup_by_first_byte(uint8_t byte) const {
    return first_byte_index_[byte];
}

std::optional<FormatRegistry::MatchResult> FormatRegistry::match(const uint8_t* data, size_t length) const {
    if (length == 0 || data == nullptr) {
        return std::nullopt;
    }

    // Build index lazily on first use
    if (!indexed_) {
        const_cast<FormatRegistry*>(this)->build_index();
    }

    // Track best match across all candidates
    MatchResult best_match{nullptr, ValidateResult::Reject, 0};

    // Phase 1: Check first-byte indexed signatures (offset == 0)
    const auto& candidates = first_byte_index_[data[0]];

    for (const auto* fmt : candidates) {
        // Quick signature match check before calling header_check
        if (fmt->signature.offset == 0 &&
            fmt->signature.pattern_len > 0 &&
            length >= fmt->signature.pattern_len) {
            if (std::memcmp(data, fmt->signature.pattern, fmt->signature.pattern_len) != 0) {
                continue;  // Signature doesn't match, skip
            }
        }

        // Run header_check
        if (fmt->header_check) {
            uint64_t calc_size = 0;
            ValidateResult result = fmt->header_check(data, length, calc_size);
            if (result != ValidateResult::Reject) {
                // Keep the match with deepest validation
                if (result > best_match.result) {
                    best_match.descriptor = fmt;
                    best_match.result = result;
                    best_match.calculated_file_size = calc_size;
                } else if (result == best_match.result && best_match.descriptor == nullptr) {
                    // Same depth but no previous match — take this one
                    best_match.descriptor = fmt;
                    best_match.result = result;
                    best_match.calculated_file_size = calc_size;
                }
            }
        }
    }

    // Phase 2: Check offset signatures (offset > 0)
    for (const auto* fmt : offset_signatures_) {
        if (fmt->signature.offset > 0 &&
            fmt->signature.pattern_len > 0 &&
            length >= static_cast<size_t>(fmt->signature.offset + fmt->signature.pattern_len)) {
            if (std::memcmp(data + fmt->signature.offset,
                            fmt->signature.pattern,
                            fmt->signature.pattern_len) != 0) {
                continue;  // Signature doesn't match
            }
        } else {
            continue;  // Not enough data to check signature
        }

        // Run header_check
        if (fmt->header_check) {
            uint64_t calc_size = 0;
            ValidateResult result = fmt->header_check(data, length, calc_size);
            if (result != ValidateResult::Reject) {
                if (result > best_match.result) {
                    best_match.descriptor = fmt;
                    best_match.result = result;
                    best_match.calculated_file_size = calc_size;
                }
            }
        }
    }

    if (best_match.descriptor != nullptr) {
        return best_match;
    }
    return std::nullopt;
}

} // namespace disk_recover