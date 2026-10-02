#pragma once

#include "pfcore/VideoDecoder.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace pfcore {

struct VideoSample {
    DecodedFrame frame;
    bool pose = false;
    bool scene = false;
    std::uint64_t decodedFrames = 0;
};

// One producer owns decoder access while the consumer runs inference. Only
// timestamp-selected frames are converted/queued; sampling and pixels are
// identical to the serial path. The decoder must outlive this reader and must
// not be accessed until finish()/EOF. Destruction cancels and joins the worker.
class VideoSampleReader {
public:
    struct WaitDiagnostics {
        double consumerMilliseconds = 0;
        double producerMilliseconds = 0;
        std::uint64_t consumerWaits = 0;
        std::uint64_t producerWaits = 0;
    };
    VideoSampleReader(VideoDecoder& decoder, double poseFps, double sceneFps,
                      std::function<bool()> cancelled = {}, bool prefetch = true,
                      std::size_t capacity = 3, bool profileWaits = false);
    ~VideoSampleReader();
    VideoSampleReader(const VideoSampleReader&) = delete;
    VideoSampleReader& operator=(const VideoSampleReader&) = delete;
    bool readNext(VideoSample& sample);
    void finish() noexcept;
    std::size_t peakBufferedSamples() const noexcept;
    WaitDiagnostics waitDiagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pfcore
