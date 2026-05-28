#pragma once
#include <windows.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

// Forward declare WIC interface (at global scope)
struct IWICImagingFactory;

namespace disk_recover::business {

struct RecoverableFile; // forward declaration

class PreviewManager {
public:
    PreviewManager();
    ~PreviewManager();

    // Non-copyable, non-movable (owns COM resources)
    PreviewManager(const PreviewManager&) = delete;
    PreviewManager& operator=(const PreviewManager&) = delete;
    PreviewManager(PreviewManager&&) = delete;
    PreviewManager& operator=(PreviewManager&&) = delete;

    // Generate thumbnail from file data (for images)
    // Returns HBITMAP or nullptr on failure
    // width/height: maximum dimensions (maintains aspect ratio)
    HBITMAP CreateThumbnailFromData(
        const uint8_t* data,
        size_t dataSize,
        int max_width,
        int max_height
    );

    // Check if file type is supported for preview
    static bool IsImageFile(const std::wstring& extension);
    static bool IsVideoFile(const std::wstring& extension);

private:
    // WIC factory (created once, reused)
    IWICImagingFactory* pWicFactory_ = nullptr;

    bool InitializeWIC();
};

} // namespace disk_recover::business
