#pragma once
#include <cstdint>
#include <compare>

namespace disk_recover {

// Validation result for file signature matching.
// Replaces the old 0-100 confidence model with explicit validation depth.
// Inspired by PhotoRec's header_check/data_check/file_check progression.
enum class ValidateResult : uint8_t {
    Reject          = 0,  // Definitely not this format
    AcceptHeader    = 1,  // Magic bytes match, structure not yet validated
    AcceptStructure = 2,  // Internal structure partially validated (e.g., JPEG markers parsed)
    AcceptContainer = 3,  // Container parsed (atoms, IFDs, chunks walked)
    AcceptVerified  = 4,  // Full validation including size bounds / footer found
};

// C++20 spaceship operator generates all 6 comparison operators (==, !=, <, <=, >, >=)
// For scoped enums, we need to explicitly specify the return type
inline std::strong_ordering operator<=>(ValidateResult a, ValidateResult b) {
    return static_cast<uint8_t>(a) <=> static_cast<uint8_t>(b);
}

// Map ValidateResult to a 0-100 confidence value for UI backward compatibility.
// Reject=0, AcceptHeader=25, AcceptStructure=50, AcceptContainer=75, AcceptVerified=100
inline uint8_t validate_result_to_confidence(ValidateResult result) {
    return static_cast<uint8_t>(result) * 25;
}

} // namespace disk_recover