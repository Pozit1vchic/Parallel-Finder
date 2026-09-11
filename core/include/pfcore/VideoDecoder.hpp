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

    void open(const std::string& path);
    void close() noexcept;
    bool isOpen() const noexcept;
    const VideoInfo& info() const;

    // Returns false at end of stream. Throws std::runtime_error for decode
    // errors. Frames are converted to tightly packed RGBA8.
    bool readNext(DecodedFrame& frame);
    void seek(double timestampSeconds);
    void rewind();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pfcore
