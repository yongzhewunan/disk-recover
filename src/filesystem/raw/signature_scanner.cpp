#define NOMINMAX
#include "signature_scanner.hpp"
#include "signature_scanner_impl.hpp"
#include "../disk-io/buffered_reader.hpp"

namespace disk_recover {

// Static merge function implementation
std::vector<RecoverableFile> SignatureScanner::merge_video_fragments(
    std::vector<RecoverableFile>& files, uint32_t sector_size) {

    if (files.size() <= 1) return std::move(files);

    std::sort(files.begin(), files.end(),
        [](const RecoverableFile& a, const RecoverableFile& b) {
            if (a.fragments.empty() || b.fragments.empty()) return false;
            return a.fragments[0].start_sector < b.fragments[0].start_sector;
        });

    const uint64_t MAX_GAP_SECTORS = (1024ULL * 1024) / sector_size;

    auto get_extension = [](const std::wstring& name) -> std::wstring {
        auto pos = name.rfind(L'.');
        if (pos == std::wstring::npos) return L"";
        return name.substr(pos);
    };

    std::vector<RecoverableFile> result;
    result.push_back(std::move(files[0]));

    for (size_t i = 1; i < files.size(); ++i) {
        RecoverableFile& last = result.back();
        RecoverableFile& current = files[i];

        if (last.file_type == current.file_type &&
            get_extension(last.file_name) == get_extension(current.file_name)) {

            uint64_t last_end = 0;
            for (const auto& frag : last.fragments) {
                last_end = std::max(last_end, frag.start_sector + frag.sector_count);
            }

            uint64_t current_start = current.fragments[0].start_sector;
            uint64_t gap = (current_start > last_end) ? (current_start - last_end) : 0;

            if (gap <= MAX_GAP_SECTORS) {
                if (gap > 0) {
                    last.fragments.push_back({last_end, gap});
                }
                for (auto& frag : current.fragments) {
                    last.fragments.push_back(std::move(frag));
                }
                uint64_t total_sectors = 0;
                for (const auto& frag : last.fragments) {
                    total_sectors += frag.sector_count;
                }
                last.file_size = total_sectors * sector_size;
                last.is_corrupted = true;
                continue;
            }
        }

        result.push_back(std::move(current));
    }

    return result;
}

// Explicit template instantiation for common reader types
template void SignatureScanner::scan<SectorReader>(
    SectorReader& reader, const ScanConfig& config,
    std::function<void(RecoverableFile&&)> on_file_found,
    std::function<void(const ScanProgress&)> on_progress);

template void SignatureScanner::scan<BufferedSectorReader>(
    BufferedSectorReader& reader, const ScanConfig& config,
    std::function<void(RecoverableFile&&)> on_file_found,
    std::function<void(const ScanProgress&)> on_progress);

} // namespace disk_recover
