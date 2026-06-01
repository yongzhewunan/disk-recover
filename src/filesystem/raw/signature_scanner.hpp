#pragma once
#include "file_signatures.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <functional>

namespace disk_recover {

class SignatureScanner {
public:
    struct ScanConfig {
        uint64_t start_sector = 0;
        uint64_t end_sector = 0;
        uint32_t step_sectors = 1;
        uint64_t resume_from_sector = 0;  // Absolute sector LBA to resume from (0 = start fresh)
        bool scan_images = true;
        bool scan_videos = true;
        std::function<bool()> should_stop;  // Called to check if scan should stop
    };

    void scan(SectorReader& reader, const ScanConfig& config,
              std::function<void(RecoverableFile&&)> on_file_found,
              std::function<void(const ScanProgress&)> on_progress);

private:
    bool try_recover_file(SectorReader& reader, uint64_t start_sector,
                          const FileSignature& sig, RecoverableFile& file);

    static std::vector<RecoverableFile> merge_video_fragments(
        std::vector<RecoverableFile>& files, uint32_t sector_size);
};

} // namespace disk_recover
