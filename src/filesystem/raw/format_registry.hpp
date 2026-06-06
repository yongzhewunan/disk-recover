#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <optional>
#include "format_descriptor.hpp"
#include "validation.hpp"

namespace disk_recover {

// Registry of all file format descriptors with indexed first-byte lookup.
// Validators auto-register via static initializers (see format_descriptor.hpp).
//
// Usage:
//   auto match = FormatRegistry::instance().match(data, length);
//   if (match && match->result != ValidateResult::Reject) { ... }
class FormatRegistry {
public:
    // Singleton access (Meyer's singleton — thread-safe in C++11+)
    static FormatRegistry& instance();

    // Register a format descriptor. Called by validator static initializers.
    void register_format(const FormatDescriptor& descriptor);

    // Result of matching data against all registered formats.
    struct MatchResult {
        const FormatDescriptor* descriptor;
        ValidateResult          result;
        uint64_t                calculated_file_size;  // 0 if unknown
    };

    // Match data against all registered formats using first-byte index.
    // Returns the best match (deepest ValidateResult), or std::nullopt if no match.
    std::optional<MatchResult> match(const uint8_t* data, size_t length) const;

    // Match data against all registered formats and return ALL candidates.
    // Returns a vector of all non-Reject matches, sorted by ValidateResult depth (deepest first).
    // Used for multi-format parallel recovery.
    std::vector<MatchResult> match_all(const uint8_t* data, size_t length) const;

    // Lookup all formats whose signature starts with the given byte.
    // Used for pre-filtering before running header_check.
    const std::vector<const FormatDescriptor*>& lookup_by_first_byte(uint8_t byte) const;

    // Get all registered formats.
    const std::vector<FormatDescriptor>& formats() const { return formats_; }

    // Check if the index has been built (for testing).
    bool is_indexed() const { return indexed_; }

    // Force index rebuild (normally called lazily on first match()).
    void rebuild_index();

private:
    FormatRegistry() = default;
    void build_index();

    std::vector<FormatDescriptor> formats_;
    std::array<std::vector<const FormatDescriptor*>, 256> first_byte_index_;
    std::vector<const FormatDescriptor*> offset_signatures_;  // Patterns at offset > 0
    bool indexed_ = false;
};

// Register all built-in format descriptors with the given registry.
// Called by FormatRegistry::instance() on first access.
// Not intended to be called directly — use FormatRegistry::instance() instead.
void register_all_formats(FormatRegistry& registry);

} // namespace disk_recover