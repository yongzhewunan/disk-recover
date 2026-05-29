#include "preview_manager.hpp"
#include <wincodec.h>
#include <algorithm>
#include <cwctype>
#include <objbase.h>
#include <limits>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

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
        // RAW camera formats
        L".cr2",                                 // Canon RAW
        L".nef",                                 // Nikon RAW
        L".arw",                                 // Sony RAW
        L".dng",                                 // Adobe DNG
        L".rw2",                                 // Panasonic RAW
        L".orf",                                 // Olympus RAW
        L".heic", L".heif",                      // HEIC/HEIF (Apple High Efficiency Image)
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

// ============================================================================
// Video Thumbnail Implementation (FFmpeg)
// ============================================================================

namespace {

// AVIO context for reading from memory
struct AVIOMemoryContext {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

// Read callback for AVIO context
static int avio_read_callback(void* opaque, uint8_t* buf, int buf_size) {
    AVIOMemoryContext* ctx = static_cast<AVIOMemoryContext*>(opaque);
    if (!ctx || !ctx->data) {
        return AVERROR_EOF;
    }

    if (ctx->pos >= ctx->size) {
        return AVERROR_EOF;
    }

    size_t remaining = ctx->size - ctx->pos;
    size_t to_read = static_cast<size_t>(buf_size);
    if (to_read > remaining) {
        to_read = remaining;
    }

    if (to_read == 0) {
        return AVERROR_EOF;
    }

    memcpy(buf, ctx->data + ctx->pos, to_read);
    ctx->pos += to_read;

    return static_cast<int>(to_read);
}

// Seek callback for AVIO context
static int64_t avio_seek_callback(void* opaque, int64_t offset, int whence) {
    AVIOMemoryContext* ctx = static_cast<AVIOMemoryContext*>(opaque);
    if (!ctx) {
        return AVERROR(EINVAL);
    }

    int64_t new_pos = 0;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = static_cast<int64_t>(ctx->pos) + offset;
            break;
        case SEEK_END:
            new_pos = static_cast<int64_t>(ctx->size) + offset;
            break;
        case AVSEEK_SIZE:
            return static_cast<int64_t>(ctx->size);
        default:
            return AVERROR(EINVAL);
    }

    if (new_pos < 0) {
        return AVERROR(EINVAL);
    }
    if (new_pos > static_cast<int64_t>(ctx->size)) {
        new_pos = static_cast<int64_t>(ctx->size);
    }

    ctx->pos = static_cast<size_t>(new_pos);
    return new_pos;
}

// RAII wrapper for AVIO context
struct AVIOContextDeleter {
    void operator()(AVIOContext* ctx) const {
        if (ctx) {
            // Free the buffer if it was allocated
            if (ctx->buffer) {
                av_freep(&ctx->buffer);
            }
            avio_context_free(&ctx);
        }
    }
};

using AVIOContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

// RAII wrapper for AVFormatContext
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

// RAII wrapper for AVCodecContext
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

// RAII wrapper for AVFrame
struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

// RAII wrapper for AVPacket
struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

// RAII wrapper for SwsContext
struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) {
            sws_freeContext(ctx);
        }
    }
};

using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

} // anonymous namespace

HBITMAP PreviewManager::CreateVideoThumbnailFromData(
    const uint8_t* data,
    size_t dataSize,
    int max_width,
    int max_height
) {
    if (!data || dataSize == 0 || max_width <= 0 || max_height <= 0) {
        return nullptr;
    }

    // Initialize memory context for AVIO
    AVIOMemoryContext memCtx = {};
    memCtx.data = data;
    memCtx.size = dataSize;
    memCtx.pos = 0;

    // Allocate AVIO context buffer (FFmpeg recommends at least 4KB)
    constexpr int AVIO_BUFFER_SIZE = 4096;
    unsigned char* avioBuffer = static_cast<unsigned char*>(av_malloc(AVIO_BUFFER_SIZE));
    if (!avioBuffer) {
        OutputDebugStringW(L"[PreviewManager] Failed to allocate AVIO buffer\n");
        return nullptr;
    }

    // Create AVIO context
    AVIOContextPtr avioCtx(avio_alloc_context(
        avioBuffer,
        AVIO_BUFFER_SIZE,
        0,  // write_flag=0 (read-only)
        &memCtx,
        avio_read_callback,
        nullptr,  // write callback (not needed)
        avio_seek_callback
    ));
    if (!avioCtx) {
        // avio_alloc_context takes ownership of buffer on success, free on failure
        av_free(avioBuffer);
        OutputDebugStringW(L"[PreviewManager] Failed to create AVIO context\n");
        return nullptr;
    }

    // Allocate format context
    AVFormatContext* rawFmtCtx = avformat_alloc_context();
    if (!rawFmtCtx) {
        OutputDebugStringW(L"[PreviewManager] Failed to allocate format context\n");
        return nullptr;
    }
    rawFmtCtx->pb = avioCtx.get();

    // Set error resilience flags for corrupted video handling
    rawFmtCtx->probesize = 256 * 1024;  // Increase probe size for better format detection
    rawFmtCtx->max_analyze_duration = 5 * AV_TIME_BASE;  // 5 seconds max analyze
    rawFmtCtx->error_recognition = AV_EF_EXPLODE;  // Be tolerant of errors

    // Open input
    AVFormatContextPtr fmtCtx(rawFmtCtx);
    int ret = avformat_open_input(&rawFmtCtx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        char errBuf[256] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        OutputDebugStringA(("[PreviewManager] Failed to open input: " + std::string(errBuf) + "\n").c_str());
        return nullptr;
    }
    // After successful open, rawFmtCtx is managed by fmtCtx

    // Find stream info
    ret = avformat_find_stream_info(fmtCtx.get(), nullptr);
    if (ret < 0) {
        char errBuf[256] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        OutputDebugStringA(("[PreviewManager] Failed to find stream info: " + std::string(errBuf) + "\n").c_str());
        return nullptr;
    }

    // Find video stream
    int videoStreamIdx = -1;
    const AVCodecParameters* videoCodecPar = nullptr;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = static_cast<int>(i);
            videoCodecPar = fmtCtx->streams[i]->codecpar;
            break;
        }
    }

    if (videoStreamIdx < 0 || !videoCodecPar) {
        OutputDebugStringW(L"[PreviewManager] No video stream found\n");
        return nullptr;
    }

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(videoCodecPar->codec_id);
    if (!codec) {
        OutputDebugStringW(L"[PreviewManager] No decoder found for video codec\n");
        return nullptr;
    }

    // Allocate codec context
    AVCodecContextPtr codecCtx(avcodec_alloc_context3(codec));
    if (!codecCtx) {
        OutputDebugStringW(L"[PreviewManager] Failed to allocate codec context\n");
        return nullptr;
    }

    // Copy codec parameters
    ret = avcodec_parameters_to_context(codecCtx.get(), videoCodecPar);
    if (ret < 0) {
        OutputDebugStringW(L"[PreviewManager] Failed to copy codec parameters\n");
        return nullptr;
    }

    // Set error resilience flags for corrupted video
    codecCtx->err_recognition = AV_EF_EXPLODE;
    // Note: AV_CODEC_FLAG_TRUNCATED was deprecated in FFmpeg 5.0+
    // Use AV_CODEC_CAP_TRUNCATED check on codec capabilities instead

    // Open codec
    ret = avcodec_open2(codecCtx.get(), codec, nullptr);
    if (ret < 0) {
        char errBuf[256] = {0};
        av_strerror(ret, errBuf, sizeof(errBuf));
        OutputDebugStringA(("[PreviewManager] Failed to open codec: " + std::string(errBuf) + "\n").c_str());
        return nullptr;
    }

    // Allocate frame for decoded video
    AVFramePtr frame(av_frame_alloc());
    if (!frame) {
        OutputDebugStringW(L"[PreviewManager] Failed to allocate frame\n");
        return nullptr;
    }

    // Allocate packet
    AVPacketPtr pkt(av_packet_alloc());
    if (!pkt) {
        OutputDebugStringW(L"[PreviewManager] Failed to allocate packet\n");
        return nullptr;
    }

    // Read frames until we get a keyframe
    constexpr int MAX_READ_ATTEMPTS = 100;  // Limit attempts for corrupted files
    int readAttempts = 0;
    bool gotKeyframe = false;

    while (readAttempts < MAX_READ_ATTEMPTS) {
        ret = av_read_frame(fmtCtx.get(), pkt.get());
        if (ret < 0) {
            // End of file or error
            break;
        }

        // Check if this is our video stream
        if (pkt->stream_index == videoStreamIdx) {
            readAttempts++;

            // Check if this is a keyframe
            if (pkt->flags & AV_PKT_FLAG_KEY) {
                // Send packet to decoder
                ret = avcodec_send_packet(codecCtx.get(), pkt.get());
                if (ret == 0 || ret == AVERROR(EAGAIN)) {
                    // Receive decoded frame
                    ret = avcodec_receive_frame(codecCtx.get(), frame.get());
                    if (ret == 0) {
                        gotKeyframe = true;
                        av_packet_unref(pkt.get());
                        break;
                    }
                    // Continue if we need more packets
                }
            }
        }

        av_packet_unref(pkt.get());
    }

    if (!gotKeyframe) {
        OutputDebugStringW(L"[PreviewManager] No keyframe found in video\n");
        return nullptr;
    }

    // Calculate scaled dimensions maintaining aspect ratio
    int srcWidth = frame->width;
    int srcHeight = frame->height;
    if (srcWidth <= 0 || srcHeight <= 0) {
        OutputDebugStringW(L"[PreviewManager] Invalid frame dimensions\n");
        return nullptr;
    }

    double aspectRatio = static_cast<double>(srcWidth) / static_cast<double>(srcHeight);
    double targetRatio = static_cast<double>(max_width) / static_cast<double>(max_height);

    int scaledWidth = max_width;
    int scaledHeight = max_height;

    if (aspectRatio > targetRatio) {
        // Video is wider than target - fit to width
        scaledHeight = static_cast<int>(static_cast<double>(max_width) / aspectRatio);
    } else {
        // Video is taller than target - fit to height
        scaledWidth = static_cast<int>(static_cast<double>(max_height) * aspectRatio);
    }

    // Ensure minimum size
    if (scaledWidth < 1) scaledWidth = 1;
    if (scaledHeight < 1) scaledHeight = 1;

    // Create HBITMAP
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = scaledWidth;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(scaledHeight);  // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;  // 24-bit BGR
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(nullptr);
    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (!hBitmap || !pvBits) {
        OutputDebugStringW(L"[PreviewManager] Failed to create DIB section for video frame\n");
        if (hBitmap) {
            DeleteObject(hBitmap);
        }
        return nullptr;
    }

    // Create sws context for conversion to BGR24
    // Note: FFmpeg uses RGB, Windows HBITMAP uses BGR, so we swap R and B
    SwsContextPtr swsCtx(sws_getContext(
        srcWidth, srcHeight, static_cast<AVPixelFormat>(frame->format),
        scaledWidth, scaledHeight, AV_PIX_FMT_BGR24,
        SWS_BILINEAR,
        nullptr, nullptr, nullptr
    ));

    if (!swsCtx) {
        OutputDebugStringW(L"[PreviewManager] Failed to create sws context\n");
        DeleteObject(hBitmap);
        return nullptr;
    }

    // Prepare destination data
    uint8_t* dstData[1] = { static_cast<uint8_t*>(pvBits) };
    int dstLinesize[1] = { scaledWidth * 3 };  // 3 bytes per pixel for BGR24

    // Scale and convert the frame
    ret = sws_scale(
        swsCtx.get(),
        frame->data, frame->linesize,
        0, srcHeight,
        dstData, dstLinesize
    );

    if (ret < 0) {
        OutputDebugStringW(L"[PreviewManager] Failed to scale video frame\n");
        DeleteObject(hBitmap);
        return nullptr;
    }

    return hBitmap;
}

} // namespace disk_recover::business
