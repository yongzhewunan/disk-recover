#include "preview_manager.hpp"
#include <wincodec.h>
#include <algorithm>
#include <cwctype>
#include <objbase.h>
#include <limits>

namespace disk_recover::business {

// Helper to convert extension to lowercase
static std::wstring ToLowerExtension(const std::wstring& ext) {
    std::wstring result = ext;
    // Ensure it starts with a dot
    if (!result.empty() && result[0] != L'.') {
        result = L"." + result;
    }
    // Convert to lowercase
    for (wchar_t& c : result) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return result;
}

PreviewManager::PreviewManager() {
    // Initialize COM for this thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hr == S_OK || hr == S_FALSE) {
        comInitialized_ = true;
    }
    InitializeWIC();
}

PreviewManager::~PreviewManager() {
    if (pWicFactory_) {
        pWicFactory_->Release();
        pWicFactory_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

bool PreviewManager::InitializeWIC() {
    if (pWicFactory_) {
        return true;  // Already initialized
    }

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWicFactory_)
    );

    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to create WIC Imaging Factory\n");
        pWicFactory_ = nullptr;
        return false;
    }

    return true;
}

HBITMAP PreviewManager::CreateThumbnailFromData(
    const uint8_t* data,
    size_t dataSize,
    int max_width,
    int max_height
) {
    if (!pWicFactory_ || !data || dataSize == 0 || max_width <= 0 || max_height <= 0) {
        return nullptr;
    }

    // Declare all variables at the top to avoid goto issues
    IWICStream* pStream = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICBitmapScaler* pScaler = nullptr;
    IWICBitmapSource* pConvertedSource = nullptr;
    HBITMAP hBitmap = nullptr;
    HRESULT hr = S_OK;

    UINT originalWidth = 0;
    UINT originalHeight = 0;
    int scaledWidth = max_width;
    int scaledHeight = max_height;
    double aspectRatio = 1.0;
    double targetRatio = 1.0;

    BITMAPINFO bmi = {};
    void* pvBits = nullptr;
    HDC hdcScreen = nullptr;

    // Create a WIC stream from memory
    hr = pWicFactory_->CreateStream(&pStream);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to create WIC stream\n");
        goto cleanup;
    }

    hr = pStream->InitializeFromMemory(
        const_cast<BYTE*>(data),
        static_cast<DWORD>(dataSize)
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to initialize stream from memory\n");
        goto cleanup;
    }

    // Create decoder from stream
    hr = pWicFactory_->CreateDecoderFromStream(
        pStream,
        nullptr,  // No vendor preference
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );
    if (FAILED(hr)) {
        // Could be corrupted or unsupported format
        OutputDebugStringW(L"[PreviewManager] Failed to create decoder from stream (corrupted or unsupported format)\n");
        goto cleanup;
    }

    // Get the first frame (most images have only one frame)
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to get frame from decoder\n");
        goto cleanup;
    }

    // Convert to 32bppPBGRA format (compatible with GDI HBITMAP)
    WICPixelFormatGUID pixelFormat;
    hr = pFrame->GetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to get pixel format\n");
        goto cleanup;
    }

    // Check if conversion is needed
    if (pixelFormat != GUID_WICPixelFormat32bppPBGRA) {
        IWICFormatConverter* pConverter = nullptr;
        hr = pWicFactory_->CreateFormatConverter(&pConverter);
        if (FAILED(hr)) {
            OutputDebugStringW(L"[PreviewManager] Failed to create format converter\n");
            goto cleanup;
        }

        hr = pConverter->Initialize(
            pFrame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );
        if (FAILED(hr)) {
            pConverter->Release();
            OutputDebugStringW(L"[PreviewManager] Failed to initialize format converter\n");
            goto cleanup;
        }

        pConvertedSource = pConverter;
    } else {
        // Already in correct format
        pConvertedSource = pFrame;
        pFrame->AddRef();  // Add ref since we'll release it in cleanup
    }

    // Get original dimensions
    hr = pConvertedSource->GetSize(&originalWidth, &originalHeight);
    if (FAILED(hr) || originalWidth == 0 || originalHeight == 0) {
        OutputDebugStringW(L"[PreviewManager] Failed to get image dimensions\n");
        goto cleanup;
    }

    // Calculate scaled dimensions maintaining aspect ratio
    aspectRatio = static_cast<double>(originalWidth) / static_cast<double>(originalHeight);
    targetRatio = static_cast<double>(max_width) / static_cast<double>(max_height);

    if (aspectRatio > targetRatio) {
        // Image is wider than target - fit to width
        scaledHeight = static_cast<int>(static_cast<double>(max_width) / aspectRatio);
    } else {
        // Image is taller than target - fit to height
        scaledWidth = static_cast<int>(static_cast<double>(max_height) * aspectRatio);
    }

    // Ensure minimum size of 1x1
    if (scaledWidth < 1) scaledWidth = 1;
    if (scaledHeight < 1) scaledHeight = 1;

    // Check for overflow before allocating bitmap buffer
    if (scaledWidth > 65535 || scaledHeight > 65535 ||
        static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight) > (std::numeric_limits<size_t>::max)() / 4) {
        OutputDebugStringW(L"[PreviewManager] Image dimensions too large\n");
        goto cleanup;
    }

    // Create scaler
    hr = pWicFactory_->CreateBitmapScaler(&pScaler);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to create bitmap scaler\n");
        goto cleanup;
    }

    hr = pScaler->Initialize(
        pConvertedSource,
        static_cast<UINT>(scaledWidth),
        static_cast<UINT>(scaledHeight),
        WICBitmapInterpolationModeFant  // High quality scaling
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to initialize bitmap scaler\n");
        goto cleanup;
    }

    // Create DIB section for HBITMAP
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = scaledWidth;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(scaledHeight);  // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdcScreen = GetDC(nullptr);
    hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    hdcScreen = nullptr;

    if (!hBitmap || !pvBits) {
        OutputDebugStringW(L"[PreviewManager] Failed to create DIB section\n");
        if (hBitmap) {
            DeleteObject(hBitmap);
            hBitmap = nullptr;
        }
        goto cleanup;
    }

    // Copy pixels from WIC bitmap to DIB section
    hr = pScaler->CopyPixels(
        nullptr,  // Copy entire bitmap
        static_cast<UINT>(scaledWidth) * 4,  // Stride (4 bytes per pixel)
        static_cast<UINT>(scaledWidth) * static_cast<UINT>(scaledHeight) * 4,  // Buffer size
        static_cast<BYTE*>(pvBits)
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PreviewManager] Failed to copy pixels to DIB section\n");
        DeleteObject(hBitmap);
        hBitmap = nullptr;
        goto cleanup;
    }

cleanup:
    if (pScaler) pScaler->Release();
    if (pConvertedSource) pConvertedSource->Release();
    if (pFrame) pFrame->Release();
    if (pDecoder) pDecoder->Release();
    if (pStream) pStream->Release();

    return hBitmap;
}

bool PreviewManager::IsImageFile(const std::wstring& extension) noexcept {
    std::wstring ext = ToLowerExtension(extension);

    // WIC built-in supported formats
    static const std::vector<std::wstring> imageExtensions = {
        L".jpg", L".jpeg", L".jpe", L".jfif",  // JPEG
        L".png",                                 // PNG
        L".gif",                                 // GIF
        L".bmp", L".dib",                        // BMP
        L".tif", L".tiff",                       // TIFF
        L".ico",                                 // ICO
        L".wdp", L".hdp",                        // HD Photo
        L".dds",                                 // DDS
    };

    for (const auto& supported : imageExtensions) {
        if (ext == supported) {
            return true;
        }
    }

    return false;
}

bool PreviewManager::IsVideoFile(const std::wstring& extension) noexcept {
    std::wstring ext = ToLowerExtension(extension);

    // Common video formats (not supported by WIC for thumbnails)
    static const std::vector<std::wstring> videoExtensions = {
        L".mp4", L".m4v", L".m4p",
        L".avi",
        L".mkv", L".webm",
        L".mov", L".qt",
        L".wmv", L".asf",
        L".flv", L".f4v",
        L".mpeg", L".mpg", L".mpe",
        L".3gp", L".3g2",
        L".ts", L".mts", L".m2ts",
    };

    for (const auto& supported : videoExtensions) {
        if (ext == supported) {
            return true;
        }
    }

    return false;
}

} // namespace disk_recover::business
