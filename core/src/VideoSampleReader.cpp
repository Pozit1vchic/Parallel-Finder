#include "pfcore/VideoSampleReader.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <chrono>

namespace pfcore {
struct VideoSampleReader::Impl {
    VideoDecoder& decoder;
    double poseInterval, sceneInterval;
    double nextPose = -std::numeric_limits<double>::infinity();
    double nextScene = -std::numeric_limits<double>::infinity();
    std::function<bool()> cancelled;
    std::size_t capacity, peak = 0;
    std::uint64_t decoded = 0, delivered = 0;
    bool prefetch, done = false;
    bool profileWaits;
    VideoSampleReader::WaitDiagnostics waits;
    std::mutex mutex;
    std::condition_variable_any available, space;
    std::deque<VideoSample> queue;
    std::exception_ptr error;
    // Last member: the worker is joined before any state it uses is destroyed.
    std::jthread worker;

    Impl(VideoDecoder& source, double poseFps, double sceneFps,
         std::function<bool()> cancellation, bool async, std::size_t slots, bool profile)
        : decoder(source), poseInterval(poseFps > 0 ? 1 / poseFps : 0),
          sceneInterval(sceneFps > 0 ? 1 / sceneFps : 0), cancelled(std::move(cancellation)),
          capacity(slots), prefetch(async), profileWaits(profile)
    {
        if (!source.isOpen() || !std::isfinite(poseFps) || !std::isfinite(sceneFps)
            || poseFps < 0 || sceneFps < 0 || poseFps > 1000 || sceneFps > 1000
            || slots == 0 || slots > 8)
            throw std::invalid_argument("VideoSampleReader: invalid sampling options");
        if (prefetch) worker = std::jthread([this](std::stop_token stop) { produce(stop); });
    }

    bool stopped(std::stop_token stop) const { return stop.stop_requested() || (cancelled && cancelled()); }

    static bool select(double timestamp, double interval, double& next) {
        if (interval <= 0 || timestamp + 1e-9 < next) return false;
        if (!std::isfinite(next)) next = timestamp + interval;
        else do { next += interval; } while (next <= timestamp + 1e-9);
        return true;
    }

    bool decode(VideoSample& result, std::stop_token stop = {}) {
        result = {};
        while (!stopped(stop) && decoder.readNext(result.frame, false)) {
            ++decoded;
            result.pose = select(result.frame.timestampSeconds, poseInterval, nextPose);
            result.scene = select(result.frame.timestampSeconds, sceneInterval, nextScene);
            if (!result.pose && !result.scene) continue;
            decoder.convertCurrentFrameToRgba(result.frame);
            result.decodedFrames = delivered = decoded;
            return true;
        }
        // Carry the final unselected tail into progress without converting it.
        if (!stopped(stop) && delivered != decoded) {
            result = {};
            result.decodedFrames = delivered = decoded;
            return true;
        }
        return false;
    }

    void produce(std::stop_token stop) noexcept {
        try {
            VideoSample sample;
            while (!stopped(stop) && decode(sample, stop)) {
                std::unique_lock lock(mutex);
                const bool blocked = profileWaits && queue.size() >= capacity;
                const auto before = blocked ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                const bool ready = space.wait(lock, stop, [&] { return queue.size() < capacity; });
                if (blocked) {
                    ++waits.producerWaits;
                    waits.producerMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
                }
                if (!ready) break;
                if (stopped(stop)) break;
                queue.push_back(std::move(sample));
                peak = std::max(peak, queue.size());
                lock.unlock();
                available.notify_one();
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            error = std::current_exception();
        }
        { std::lock_guard lock(mutex); done = true; }
        available.notify_all();
    }

    void finish() noexcept {
        if (worker.joinable()) {
            worker.request_stop();
            space.notify_all();
            available.notify_all();
            worker.join();
        }
    }
};

VideoSampleReader::VideoSampleReader(VideoDecoder& source, double poseFps, double sceneFps,
    std::function<bool()> cancelled, bool prefetch, std::size_t capacity, bool profileWaits)
    : impl_(std::make_unique<Impl>(source, poseFps, sceneFps, std::move(cancelled), prefetch, capacity, profileWaits)) {}
VideoSampleReader::~VideoSampleReader() { finish(); }
void VideoSampleReader::finish() noexcept { impl_->finish(); }
std::size_t VideoSampleReader::peakBufferedSamples() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->peak;
}
VideoSampleReader::WaitDiagnostics VideoSampleReader::waitDiagnostics() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->waits;
}
bool VideoSampleReader::readNext(VideoSample& sample) {
    auto& state = *impl_;
    if (!state.prefetch) return state.decode(sample);
    std::unique_lock lock(state.mutex);
    const bool blocked = state.profileWaits && !state.done && state.queue.empty();
    const auto before = blocked ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    state.available.wait(lock, [&] { return state.done || !state.queue.empty(); });
    if (blocked) {
        ++state.waits.consumerWaits;
        state.waits.consumerMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
    }
    if (!state.queue.empty()) {
        sample = std::move(state.queue.front());
        state.queue.pop_front();
        lock.unlock();
        state.space.notify_one();
        return true;
    }
    const auto error = state.error;
    lock.unlock();
    state.finish();
    if (error) std::rethrow_exception(error);
    return false;
}
} // namespace pfcore
