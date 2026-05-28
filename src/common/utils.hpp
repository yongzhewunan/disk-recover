#pragma once
#include <string>
#include <cstdint>

namespace disk_recover::utils {

bool IsAdminPrivilege();
bool EnsureAdminPrivilege();

std::wstring FormatFileSize(uint64_t bytes);
std::wstring FormatSectorRange(uint64_t start, uint64_t count);

uint64_t AlignUp(uint64_t value, uint64_t alignment);
uint64_t AlignDown(uint64_t value, uint64_t alignment);

} // namespace disk_recover::utils