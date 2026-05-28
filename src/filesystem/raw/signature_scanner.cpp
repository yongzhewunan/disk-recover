#define NOMINMAX
#include "signature_scanner.hpp"
#include <algorithm>

namespace disk_recover {

void SignatureScanner::scan(SectorReader& reader, const ScanConfig& config,
                            std::function<void(RecoverableFile&&)> on_file_found,
                            std::function<void(const ScanProgress&)> on_progress) {
    ScanProgress progress{};
    progress.total_sectors = config.end_sector - config.start_sector;

    const uint32_t sectors_per_chunk = 64;
    AlignedBuffer buf(sectors_per_chunk * reader.sector_size(), reader.sector_size());

    for (uint64_t sector = config.start_sector;
         sector < config.end_sector;
         sector += config.step_sectors) {

        uint32_t count = std::min(sectors_per_chunk,
            static_cast<uint32_t>(config.end_sector - sector));

        if (!reader.read_sectors_checked(sector, count, buf)) {
            progress.sectors_scanned += count;
            progress.bad_sectors_hit++;
            if (on_progress) on_progress(progress);
            continue;
        }

        for (uint32_t offset = 0; offset < count * reader.sector_size();
             offset += reader.sector_size()) {
            auto sig = FileSignatures::match(buf.data() + offset, reader.sector_size());
            if (!sig) continue;

            if (sig->file_type == FileType::Image && !config.scan_images) continue;
            if (sig->file_type == FileType::Video && !config.scan_videos) continue;

            RecoverableFile file{};
            uint64_t file_sector = sector + offset / reader.sector_size();
            if (try_recover_file(reader, file_sector, *sig, file)) {
                progress.files_found++;
                if (on_file_found) on_file_found(std::move(file));
            }
        }

        progress.sectors_scanned += count;
        if (on_progress) on_progress(progress);
    }

    progress.is_complete = true;
    if (on_progress) on_progress(progress);
}

bool SignatureScanner::try_recover_file(SectorReader& reader, uint64_t start_sector,
                                         const FileSignature& sig, RecoverableFile& file) {
    file.file_type = sig.file_type;
    file.file_name = sig.description + L"_sector_" + std::to_wstring(start_sector)
                     + L"." + sig.extension;
    file.fragments.push_back({start_sector, 1});
    file.is_corrupted = true;
    file.file_size = 0;
    return true;
}

} // namespace disk_recover
