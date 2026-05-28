#include "recover_manager.hpp"

namespace disk_recover {

bool RecoverManager::start_recovery(SectorReader& reader,
                                    const std::vector<RecoverableFile>& files,
                                    MultiTargetWriter& writer) {
    report_ = {};
    report_.total_files = static_cast<uint32_t>(files.size());

    for (uint32_t i = 0; i < files.size(); ++i) {
        if (recover_single_file(reader, files[i], writer)) {
            report_.success_count++;
            report_.total_bytes_recovered += files[i].file_size;
        } else {
            report_.failed_count++;
        }
        if (on_progress_) on_progress_(i + 1, report_.total_files);
    }
    return report_.success_count > 0;
}

bool RecoverManager::recover_single_file(SectorReader& reader,
                                         const RecoverableFile& file,
                                         MultiTargetWriter& writer) {
    if (!writer.open_file(file.file_name)) return false;

    AlignedBuffer buf(reader.sector_size() * 64, reader.sector_size());

    for (const auto& ext : file.fragments) {
        uint64_t sector = ext.start_sector;
        uint64_t remaining = ext.sector_count;

        while (remaining > 0) {
            uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(remaining, 64));
            if (!reader.read_sectors(sector, count, buf)) {
                break;
            }
            writer.write(buf.data(), count * reader.sector_size());
            sector += count;
            remaining -= count;
        }
    }

    writer.close_file();
    return true;
}

} // namespace disk_recover
