#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pfcore {

struct VideoInfo {
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    double sampleAspectRatio = 1.0;
    double rotationDegrees = 0.0;
    bool variableFrameRate = false;
};

struct DecodedFrame {
    int width = 0;
    int height = 0;
    double timestampSeconds = 0.0;
    std::vector<std::uint8_t> rgba;
};

struct VideoDecodeOptions {
    // Zero: bounded automatic CPU threading (at most eight threads).
    int threads = 0;
    bool preferNvidia = false;
    // Experimental: CUVID's resize kernel differs from the CPU analysis path.
    // Keep false in production until semantic reference pairs pass as well.
    bool resizeOnNvidia = false;
    // Small sources can be cheaper on CPU: applied before GPU initialization.
    std::uint64_t minimumNvidiaPixels = 0;
    int nvidiaDevice = 0;
    int maxWidth = 0;
    int maxHeight = 0;
};

struct VideoDecodeDiagnostics {
    std::string backend = "cpu";
    std::string fallbackReason;
    int threads = 1;
    std::uint64_t decodedFrames = 0;
    std::uint64_t convertedFrames = 0;
    std::uint64_t hardwareDownloads = 0;
    double readMilliseconds = 0.0;
    double conversionMilliseconds = 0.0;
};

// RAII FFmpeg decoder. The public API intentionally exposes no FFmpeg types,
// keeping pfcore independent from Qt and allowing the decoder to be replaced.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(VideoDecoder&&) noexcept;
    VideoDecoder& operator=(VideoDecoder&&) noexcept;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    void open(const std::string& path, VideoDecodeOptions options = {});
    void close() noexcept;
    bool isOpen() const noexcept;
    const VideoInfo& info() const;
    VideoDecodeDiagnostics diagnostics() const;

    // Returns false at end of stream. Throws std::runtime_error for decode
    // errors. Set convertToRgba to false when only timestamp/stream progress
    // is needed: decoding still advances normally, but avoids allocating and
    // converting a full RGBA frame that the caller will discard.
    bool readNext(DecodedFrame& frame, bool convertToRgba = true);

    // Limit RGBA conversion to an aspect-preserving working size. The coded
    // stream is still reconstructed at native resolution. When open() opts
    // into NVIDIA resize, only the reduced surface reaches CPU memory.
    // Experimental hardware resize is fixed at open: reopen to increase it.
    // Passing zero disables the limit (the default).
    void setRgbaMaxDimensions(int maxWidth, int maxHeight) noexcept;

    // Converts the most recently decoded frame after readNext(..., false).
    // This lets callers scan by timestamp without converting every frame on
    // the way to one requested preview. Returns false when no retained frame
    // is available (for example after a regular readNext(..., true)).
    bool convertCurrentFrameToRgba(DecodedFrame& frame);
    void seek(double timestampSeconds);
    void rewind();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pfcore
