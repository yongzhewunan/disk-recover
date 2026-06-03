#pragma once
#include "file_signatures.hpp"

namespace disk_recover {

std::optional<MatchResult>
validate_jpeg(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_png(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_gif(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_tiff_raw(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_riff(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_bmff(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_ebml(const uint8_t* data, size_t length);

std::optional<MatchResult>
validate_ts(const uint8_t* data, size_t length);

} // namespace disk_recover
