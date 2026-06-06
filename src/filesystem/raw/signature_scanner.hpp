#pragma once
#include "format_registry.hpp"
#include "sector_reader.hpp"
#include "types.hpp"
#include <functional>

namespace disk_recover {

// Minimum confidence threshold for accepting a file signature match
constexpr uint8_t MIN_CONFIDENCE_THRESHOLD = 15;

// Maximum sectors to search for video file boundary (100MB)
constexpr uint64_t MAX_VIDEO_SEARCH_SECTORS = 204800;

class SignatureScanner {
public:
    struct ScanConfig {
        uint64_t start_sector = 0;
        uint64_t end_sector = 0;
        uint32_t step_sectors = 1;
        uint64_t resume_from_sector = 0;  // Absolute sector LBA to resume from (0 = start fresh)
        bool scan_images    = true;
        bool scan_videos    = true;
        bool scan_audio     = true;
        bool scan_documents = true;
        bool scan_archives  = true;
        std::function<bool()> should_stop;  // Called to check if scan should stop
    };

    // Template to accept both SectorReader and BufferedSectorReader
    template<typename ReaderType>
    void scan(ReaderType& reader, const ScanConfig& config,
              std::function<void(RecoverableFile&&)> on_file_found,
              std::function<void(const ScanProgress&)> on_progress);

private:
    template<typename ReaderType>
    bool try_recover_file(ReaderType& reader, uint64_t start_sector,
                          const FormatRegistry::MatchResult& match_result, RecoverableFile& file);

    // Multi-format parallel recovery: attempt recovery for all candidate formats
    template<typename ReaderType>
    std::vector<RecoverableFile> try_recover_all_formats(
        ReaderType& reader, uint64_t start_sector,
        const std::vector<FormatRegistry::MatchResult>& candidates);

    static std::vector<RecoverableFile> merge_video_fragments(
        std::vector<RecoverableFile>& files, uint32_t sector_size);
};

} // namespace disk_recover

// Include implementation
#include "signature_scanner_impl.hpp"
