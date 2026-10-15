#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <stop_token>

namespace pfcore {

// Presentation span of encoded video/audio packets, relative to the container
// start. Stream-copy cuts can contain reordered tail frames beyond the header's
// duration; concat must include those frames before starting the next clip.
double mediaPacketDuration(const std::string& path, std::stop_token stop = {});
// Copies encoded packets, omitting HEVC leading RASL pictures at a splice.
// An independent decoder already discards these pictures; a continuous decoder
// would otherwise try to use references from the wrong preceding clip.
// Returns the presentation span while copying, avoiding a second packet scan.
double prepareStreamCopySplice(const std::string& input, const std::string& output, std::stop_token stop = {});

struct VideoInfo {
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    double sampleAspectRatio = 1.0;
    double rotationDegrees = 0.0;
    bool variableFrameRate = false;
    bool hasAudio = false;
    // Primary video and all audio codec parameters, including initialization
    // data. Equal signatures allow conservative stream-copy concatenation.
    std::string streamCopySignature;
    // Same codec/timebase/color/audio admission, without the observed average
    // frame rate, which can differ between exact VFR intervals of one source.
    std::string streamCopyCodecSignature;
};

struct DecodedFrame {
    int width = 0;
    int height = 0;
    double timestampSeconds = 0.0;
    std::vector<std::uint8_t> rgba;
};

// Owned native pixels, independent of the decoder's next receive/seek. No
// FFmpeg types escape the interface. A snapshot can be converted only by its
// original open decoder; closing/reopening invalidates conversion admission.
class VideoFrameSnapshot {
public:
    double timestampSeconds = 0;
private:
    std::shared_ptr<const void> pixels_, source_;
    friend class VideoDecoder;
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
    // Explicit audit of parallel swscale. The established serial pixel path
    // remains the default until complete pixel/PTS and performance validation.
    int conversionThreads = 1;
    // Enable validated four-worker conversion only after native-size NVDEC
    // actually opens for >=3840x1440 pixels (including cinematic/portrait 4K)
    // on a machine with at least eight CPU threads.
    bool autoNvidiaConversion = false;
};

struct VideoDecodeDiagnostics {
    std::string backend = "cpu";
    std::string fallbackReason;
    int threads = 1;
    int conversionThreads = 1;
    std::uint64_t decodedFrames = 0;
    std::uint64_t convertedFrames = 0;
    std::uint64_t hardwareDownloads = 0;
    std::uint64_t reusedDownloadBuffers = 0;
    double downloadMilliseconds = 0.0;
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
    // One thread may receive snapshots while another converts them. Geometry
    // must remain fixed, diagnostics must be queried after both threads join,
    // and seek/close/reopen require stopping and joining the receiving thread.
    bool readNextSnapshot(VideoFrameSnapshot& snapshot);
    bool convertSnapshotToRgba(const VideoFrameSnapshot& snapshot, DecodedFrame& frame);
    void seek(double timestampSeconds);
    void rewind();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pfcore
