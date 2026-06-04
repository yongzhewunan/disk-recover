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

            // Format-aware gap threshold
            uint64_t max_gap_bytes = video_merge_gap_bytes(get_extension(last.file_name));
            uint64_t max_gap_sectors = max_gap_bytes / sector_size;

            if (gap <= max_gap_sectors) {
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

                // Smart corruption assessment based on merge quality
                uint64_t gap_bytes = gap * sector_size;
                if (last.confidence >= 50 && current.confidence >= 50 && gap_bytes <= max_gap_bytes / 4) {
                    // High confidence, small gap — likely a good merge
                    last.corruption_level = std::max(last.corruption_level, CorruptionLevel::Minor);
                } else if (gap_bytes <= max_gap_bytes / 2) {
                    // Medium quality merge
                    last.corruption_level = std::max(last.corruption_level, CorruptionLevel::Moderate);
                } else {
                    // Large gap — uncertain merge
                    last.corruption_level = std::max(last.corruption_level, CorruptionLevel::Severe);
                }

                // Update confidence to average of merged fragments
                last.confidence = (last.confidence + current.confidence) / 2;
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
