#pragma once
#include "multi_target_writer.hpp"
#include "disk-io/sector_reader.hpp"
#include "common/types.hpp"
#include <vector>
#include <functional>

namespace disk_recover {

struct RecoverReport {
    uint32_t total_files = 0;
    uint32_t success_count = 0;
    uint32_t failed_count = 0;
    uint64_t total_bytes_recovered = 0;
};

class RecoverManager {
public:
    bool start_recovery(SectorReader& reader,
                        const std::vector<RecoverableFile>& files,
                        MultiTargetWriter& writer);
    const RecoverReport& report() const { return report_; }

    void set_progress_callback(std::function<void(uint32_t, uint32_t)> cb) { on_progress_ = cb; }

private:
    bool recover_single_file(SectorReader& reader,
                             const RecoverableFile& file,
                             MultiTargetWriter& writer);
    RecoverReport report_{};
    std::function<void(uint32_t, uint32_t)> on_progress_;
};

} // namespace disk_recover
