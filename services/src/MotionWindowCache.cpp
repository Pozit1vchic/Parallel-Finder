#include <pfservices/MotionWindowCache.hpp>
#include <cmath>
#include <cstring>

namespace pfservices {
namespace {
template <typename T>
void appendBytes(std::vector<std::uint8_t>& output, const T& value)
{
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    output.insert(output.end(), begin, begin + sizeof(T));
}

template <typename T>
bool readBytes(std::span<const std::uint8_t> input, std::size_t& offset, T& value)
{
    if (offset > input.size() || sizeof(T) > input.size() - offset) return false;
    std::memcpy(&value, input.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

} // namespace

std::vector<std::uint8_t> serializeMotionWindows(std::span<const pfcore::MotionWindow> windows, int sceneCount)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t version = 9;
    appendBytes(output, version);
    appendBytes(output, static_cast<std::uint32_t>(windows.size()));
    appendBytes(output, static_cast<std::uint32_t>(sceneCount));
    for (const auto& window : windows) {
        appendBytes(output, static_cast<std::uint32_t>(window.sourceId.size()));
        output.insert(output.end(), window.sourceId.begin(), window.sourceId.end());
        appendBytes(output, static_cast<std::uint64_t>(window.trackId));
        appendBytes(output, static_cast<std::uint64_t>(window.sceneIndex));
        appendBytes(output, static_cast<std::uint8_t>(window.hasSceneIndex ? 1 : 0));
        appendBytes(output, static_cast<std::uint8_t>(window.staticFrameSet ? 1 : 0));
        appendBytes(output, window.sceneStartSeconds);
        appendBytes(output, window.sceneEndSeconds);
        appendBytes(output, window.appearanceConfidence);
        appendBytes(output, static_cast<std::uint32_t>(window.appearanceEmbedding.size()));
        for (const float value : window.appearanceEmbedding) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint32_t>(window.sceneContext.size()));
        for (const float value : window.sceneContext) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint8_t>(window.sceneViewSampled ? 1 : 0));
        appendBytes(output, static_cast<std::uint32_t>(window.sceneView.size()));
        for (const float value : window.sceneView) appendBytes(output, value);
        appendBytes(output, window.faceConfidence);
        appendBytes(output, static_cast<std::uint32_t>(window.faceEmbedding.size()));
        for (const float value : window.faceEmbedding) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint32_t>(window.frames.size()));
        for (const auto& frame : window.frames) {
            appendBytes(output, frame.timestampSeconds);
            appendBytes(output, static_cast<std::uint32_t>(frame.keypoints.size()));
            for (const auto& point : frame.keypoints) {
                appendBytes(output, point.x);
                appendBytes(output, point.y);
                appendBytes(output, point.confidence);
            }
        }
    }
    return output;
}

bool deserializeMotionWindows(std::span<const std::uint8_t> input,
                              std::vector<pfcore::MotionWindow>& windows, int& cachedSceneCount)
{
    cachedSceneCount = -1;
    std::size_t offset = 0;
    std::uint32_t version = 0, windowCount = 0;
    if (!readBytes(input, offset, version) || (version != 7 && version != 8 && version != 9)
        || !readBytes(input, offset, windowCount) || windowCount > 100'000U) return false;
    if (version >= 8) {
        std::uint32_t count = 0;
        if (!readBytes(input, offset, count) || count > 10'000'000U) return false;
        cachedSceneCount = static_cast<int>(count);
    }
    // Version 7 already needs at least 66 bytes per window, without any
    // source, embedding or pose data. Reject forged counts before allocation.
    constexpr std::size_t minimumWindowBytes = 66;
    if (windowCount > (input.size() - offset) / minimumWindowBytes) return false;
    windows.clear();
    windows.reserve(windowCount);
    std::uint64_t totalFrames = 0;
    for (std::uint32_t windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
        std::uint32_t sourceSize = 0;
        if (!readBytes(input, offset, sourceSize) || sourceSize > 16U * 1024U
            || offset + sourceSize > input.size()) return false;
        pfcore::MotionWindow window;
        window.sourceId.assign(reinterpret_cast<const char*>(input.data() + offset), sourceSize);
        offset += sourceSize;
        std::uint64_t trackId = 0, sceneIndex = 0;
        std::uint8_t hasSceneIndex = 0, staticFrameSet = 0;
        if (!readBytes(input, offset, trackId) || !readBytes(input, offset, sceneIndex)
            || !readBytes(input, offset, hasSceneIndex)
            || !readBytes(input, offset, staticFrameSet)) return false;
        window.trackId = static_cast<std::size_t>(trackId);
        window.sceneIndex = static_cast<std::size_t>(sceneIndex);
        window.hasSceneIndex = hasSceneIndex != 0;
        window.staticFrameSet = staticFrameSet != 0;
        if (!readBytes(input, offset, window.sceneStartSeconds)
            || !readBytes(input, offset, window.sceneEndSeconds)
            || !std::isfinite(window.sceneStartSeconds)
            || !std::isfinite(window.sceneEndSeconds)) return false;
        std::uint32_t embeddingSize = 0;
        if (!readBytes(input, offset, window.appearanceConfidence)
            || !std::isfinite(window.appearanceConfidence)
            || !readBytes(input, offset, embeddingSize)
            || embeddingSize > 4096U) return false;
        window.appearanceEmbedding.resize(embeddingSize);
        for (float& value : window.appearanceEmbedding) {
            if (!readBytes(input, offset, value) || !std::isfinite(value)) return false;
        }
        if (version >= 8) {
            std::uint32_t contextSize = 0;
            // A fully black/short shot legitimately has no histogram. Empty
            // context is missing evidence, not a corrupt or incomplete cache.
            if (!readBytes(input, offset, contextSize) || contextSize > 4096U) return false;
            window.sceneContext.resize(contextSize);
            for (auto& value : window.sceneContext)
                if (!readBytes(input, offset, value) || !std::isfinite(value) || value < 0.0F) return false;
        }
        if (version >= 9) {
            std::uint8_t sampled = 0; std::uint32_t viewSize = 0;
            if (!readBytes(input, offset, sampled) || sampled > 1
                || !readBytes(input, offset, viewSize) || (viewSize != 0 && viewSize != 432)) return false;
            window.sceneViewSampled = sampled != 0;
            window.sceneView.resize(viewSize);
            for (auto& value : window.sceneView)
                if (!readBytes(input, offset, value) || !std::isfinite(value) || value < 0 || value > 1) return false;
        }
        if (!readBytes(input, offset, window.faceConfidence) || !std::isfinite(window.faceConfidence)
            || !readBytes(input, offset, embeddingSize) || embeddingSize > 4096U) return false;
        window.faceEmbedding.resize(embeddingSize);
        for (float& value : window.faceEmbedding)
            if (!readBytes(input, offset, value) || !std::isfinite(value)) return false;
        std::uint32_t frameCount = 0;
        if (!readBytes(input, offset, frameCount) || frameCount > pfcore::MotionMatcherParams::maximumWindowFrames
            || totalFrames > 2'000'000ULL - frameCount
            || frameCount > (input.size() - offset) / 12U) return false;
        totalFrames += frameCount;
        window.frames.reserve(frameCount);
        for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            pfcore::PoseFrame frame;
            std::uint32_t pointCount = 0;
            if (!readBytes(input, offset, frame.timestampSeconds)
                || !std::isfinite(frame.timestampSeconds)
                || !readBytes(input, offset, pointCount) || pointCount > 128U) return false;
            frame.keypoints.resize(pointCount);
            for (auto& point : frame.keypoints) {
                if (!readBytes(input, offset, point.x) || !readBytes(input, offset, point.y)
                    || !readBytes(input, offset, point.confidence)
                    || !std::isfinite(point.x) || !std::isfinite(point.y)
                    || !std::isfinite(point.confidence)) return false;
            }
            window.frames.push_back(std::move(frame));
        }
        windows.push_back(std::move(window));
    }
    return offset == input.size();
}

} // namespace pfservices
