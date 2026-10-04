#include "pfgpu/PoseEstimator.hpp"
#include "pfgpu/detail/PoseInput.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <chrono>

namespace pfgpu {
namespace {

using detail::LetterboxTransform;

std::size_t profileBatchSize(const std::string& profile)
{
    if (profile.size() > 1 && profile.front() == 'b') {
        try {
            const auto value = std::stoull(profile.substr(1));
            if (value > 0 && value <= 64) return static_cast<std::size_t>(value);
        } catch (const std::exception&) {
        }
    }
    return 1;
}

float intersectionOverUnion(const PoseDetection& left, const PoseDetection& right)
{
    const float intersectionLeft = std::max(left.left, right.left);
    const float intersectionTop = std::max(left.top, right.top);
    const float intersectionRight = std::min(left.right, right.right);
    const float intersectionBottom = std::min(left.bottom, right.bottom);
    const float intersectionWidth = std::max(0.0F, intersectionRight - intersectionLeft);
    const float intersectionHeight = std::max(0.0F, intersectionBottom - intersectionTop);
    const float intersection = intersectionWidth * intersectionHeight;
    const float leftArea = std::max(0.0F, left.right - left.left)
        * std::max(0.0F, left.bottom - left.top);
    const float rightArea = std::max(0.0F, right.right - right.left)
        * std::max(0.0F, right.bottom - right.top);
    const float denominator = leftArea + rightArea - intersection;
    return denominator <= 1e-6F ? 0.0F : intersection / denominator;
}

std::vector<PoseDetection> applyNms(std::vector<PoseDetection> detections, float threshold)
{
    std::sort(detections.begin(), detections.end(), [](const auto& left, const auto& right) {
        return left.confidence > right.confidence;
    });
    std::vector<PoseDetection> kept;
    kept.reserve(detections.size());
    for (auto& candidate : detections) {
        const bool suppressed = std::any_of(kept.begin(), kept.end(), [&](const auto& selected) {
            return intersectionOverUnion(candidate, selected) >= threshold;
        });
        if (!suppressed) kept.push_back(std::move(candidate));
    }
    return kept;
}

std::vector<std::vector<PoseDetection>> decodeOutput(
    const FloatTensor& tensor,
    const std::vector<LetterboxTransform>& transforms,
    const PoseEstimatorParams& params)
{
    if (tensor.shape.size() != 3) throw std::runtime_error("PoseEstimator: unsupported output rank");
    const std::size_t compactAttributes = 5U + params.keypointCount * 3U;
    const std::size_t endToEndAttributes = 6U + params.keypointCount * 3U;
    const bool channelsFirst = tensor.shape[1] == static_cast<std::int64_t>(compactAttributes)
        || tensor.shape[1] == static_cast<std::int64_t>(endToEndAttributes);
    const std::size_t attributes = channelsFirst
        ? static_cast<std::size_t>(tensor.shape[1]) : static_cast<std::size_t>(tensor.shape[2]);
    if (attributes != compactAttributes && attributes != endToEndAttributes)
        throw std::runtime_error("PoseEstimator: unsupported YOLO-pose attribute count");
    const std::size_t batchCount = static_cast<std::size_t>(tensor.shape[0]);
    const std::size_t candidates = channelsFirst
        ? static_cast<std::size_t>(tensor.shape[2]) : static_cast<std::size_t>(tensor.shape[1]);
    if (batchCount == 0 || candidates == 0 || batchCount < transforms.size())
        throw std::runtime_error("PoseEstimator: output batch does not match input batch");
    const std::size_t expectedValues = batchCount * attributes * candidates;
    if (tensor.values.size() != expectedValues)
        throw std::runtime_error("PoseEstimator: output shape does not match value count");

    bool endToEnd = params.outputMode == PoseOutputMode::EndToEnd;
    if (params.outputMode == PoseOutputMode::Auto)
        endToEnd = attributes == endToEndAttributes && candidates <= 1000;
    if (params.outputMode == PoseOutputMode::ExternalNms) endToEnd = false;
    if (endToEnd && attributes != endToEndAttributes)
        throw std::runtime_error("PoseEstimator: end-to-end output has no class column");

    std::vector<std::vector<PoseDetection>> result(transforms.size());
    const auto at = [&](std::size_t batch, std::size_t candidate, std::size_t attribute) {
        if (channelsFirst)
            return tensor.values[batch * attributes * candidates + attribute * candidates + candidate];
        return tensor.values[batch * attributes * candidates + candidate * attributes + attribute];
    };
    if (std::getenv("PF_DEBUG_POSE") != nullptr) {
        static bool printed = false;
        if (!printed) {
            printed = true;
            float maxConfidence = 0.0F;
            for (std::size_t candidate = 0; candidate < candidates; ++candidate)
                maxConfidence = std::max(maxConfidence, at(0, candidate, 4));
            std::fprintf(stderr, "PF_DEBUG_POSE shape=%zu x %zu x %zu endToEnd=%d maxConf=%f threshold=%f\n",
                         batchCount, candidates, attributes, endToEnd ? 1 : 0,
                         maxConfidence, params.confidenceThreshold);
        }
    }
    for (std::size_t batch = 0; batch < transforms.size(); ++batch) {
        const auto& transform = transforms[batch];
        for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
            const float confidence = at(batch, candidate, 4);
            if (confidence < params.confidenceThreshold) continue;
            PoseDetection detection;
            detection.confidence = confidence;
            const auto decodeX = [&](float value) {
                return (value - transform.padX) / transform.scale;
            };
            const auto decodeY = [&](float value) {
                return (value - transform.padY) / transform.scale;
            };
            if (endToEnd) {
                detection.left = std::clamp(decodeX(at(batch, candidate, 0)), 0.0F,
                                             static_cast<float>(transform.sourceWidth));
                detection.top = std::clamp(decodeY(at(batch, candidate, 1)), 0.0F,
                                           static_cast<float>(transform.sourceHeight));
                detection.right = std::clamp(decodeX(at(batch, candidate, 2)), detection.left,
                                              static_cast<float>(transform.sourceWidth));
                detection.bottom = std::clamp(decodeY(at(batch, candidate, 3)), detection.top,
                                               static_cast<float>(transform.sourceHeight));
            } else {
                const float centerX = decodeX(at(batch, candidate, 0));
                const float centerY = decodeY(at(batch, candidate, 1));
                const float width = at(batch, candidate, 2) / transform.scale;
                const float height = at(batch, candidate, 3) / transform.scale;
                detection.left = std::clamp(centerX - width * 0.5F, 0.0F,
                                            static_cast<float>(transform.sourceWidth));
                detection.top = std::clamp(centerY - height * 0.5F, 0.0F,
                                           static_cast<float>(transform.sourceHeight));
                detection.right = std::clamp(centerX + width * 0.5F, detection.left,
                                             static_cast<float>(transform.sourceWidth));
                detection.bottom = std::clamp(centerY + height * 0.5F, detection.top,
                                              static_cast<float>(transform.sourceHeight));
            }
            // Preserve the monotonic box invariant for malformed model outputs.
            if (detection.right < detection.left) detection.right = detection.left;
            if (detection.bottom < detection.top) detection.bottom = detection.top;
            const std::size_t keypointOffset = endToEnd ? 6U : 5U;
            detection.keypoints.reserve(params.keypointCount * 3U);
            for (std::size_t keypoint = 0; keypoint < params.keypointCount; ++keypoint) {
                detection.keypoints.push_back(std::clamp(decodeX(at(batch, candidate,
                    keypointOffset + keypoint * 3U)), 0.0F,
                    static_cast<float>(transform.sourceWidth)));
                detection.keypoints.push_back(std::clamp(decodeY(at(batch, candidate,
                    keypointOffset + keypoint * 3U + 1U)), 0.0F,
                    static_cast<float>(transform.sourceHeight)));
                detection.keypoints.push_back(at(batch, candidate,
                    keypointOffset + keypoint * 3U + 2U));
            }
            result[batch].push_back(std::move(detection));
        }
        if (!endToEnd) result[batch] = applyNms(std::move(result[batch]), params.nmsIouThreshold);
    }
    return result;
}

} // namespace

PoseEstimator::PoseEstimator(std::string modelPath, PoseEstimatorParams params)
    : modelPath_(std::move(modelPath)), params_(std::move(params)),
      inputWorkspace_(std::make_unique<detail::PoseInputWorkspace>())
{
    if (modelPath_.empty() || params_.inputWidth <= 0 || params_.inputHeight <= 0
        || params_.inputWidth > 4096 || params_.inputHeight > 4096
        || static_cast<std::uint64_t>(params_.inputWidth)
            * static_cast<std::uint64_t>(params_.inputHeight) > 64ULL * 1024ULL * 1024ULL
        || params_.keypointCount == 0 || params_.confidenceThreshold < 0.0F
        || params_.confidenceThreshold > 1.0F || params_.nmsIouThreshold <= 0.0F
        || params_.nmsIouThreshold > 1.0F || params_.batchSize > 64
        || params_.intraOpThreads > 256)
        throw std::invalid_argument("PoseEstimator: invalid parameters");
    if (params_.batchSize == 0) params_.batchSize = profileBatchSize(params_.profile);
}

PoseEstimator::~PoseEstimator() = default;

SessionHandle PoseEstimator::acquireSession()
{
    const auto session = processSessionCache().getOrCreate(
        ModelRef::fromPath(modelPath_),
        {params_.provider, 0, params_.profile, params_.intraOpThreads});
    if (!session.ok) throw std::runtime_error(session.error);
    if (!sessionSpec_.has_value() || sessionSpecKey_ != session.handle.cacheKey) {
        SessionSpec description = describeSession(session.handle);
        if (!description.ok) throw std::runtime_error(description.error);
        const auto& inputShape = description.input.shape;
        if (inputShape.size() != 4 || (inputShape[1] > 0 && inputShape[1] != 3)
            || (inputShape[2] > 0 && inputShape[2] != params_.inputHeight)
            || (inputShape[3] > 0 && inputShape[3] != params_.inputWidth)) {
            throw std::runtime_error("PoseEstimator: model input shape is not NCHW 3x"
                                     + std::to_string(params_.inputHeight) + "x"
                                     + std::to_string(params_.inputWidth));
        }
        if (inputShape[0] > 0 && static_cast<std::size_t>(inputShape[0]) != params_.batchSize)
            throw std::runtime_error("PoseEstimator: profile batch does not match model input batch");
        if (description.outputs.empty()) throw std::runtime_error("PoseEstimator: model has no outputs");
        sessionSpec_ = std::move(description);
        sessionSpecKey_ = session.handle.cacheKey;
    }
    return session.handle;
}

void PoseEstimator::prepare()
{
    const std::lock_guard lock(mutex_);
    (void)acquireSession();
}

std::vector<PoseDetection> PoseEstimator::infer(const PoseImage& image)
{
    const auto batches = inferBatch({image});
    return batches.empty() ? std::vector<PoseDetection> {} : batches.front();
}

std::vector<std::vector<PoseDetection>> PoseEstimator::inferBatch(const std::vector<PoseImage>& images)
{
    if (images.empty()) return {};
    const std::lock_guard lock(mutex_);
    const SessionHandle session = acquireSession();
    const auto* profileFlag = std::getenv("PF_DEBUG_INFERENCE");
    const bool profile = profileFlag && *profileFlag;
    static thread_local std::size_t calls = 0;
    static thread_local double preparationMs = 0, runMs = 0, decodingMs = 0;

    std::vector<std::vector<PoseDetection>> detections;
    detections.reserve(images.size());
    for (std::size_t offset = 0; offset < images.size(); offset += params_.batchSize) {
        const std::size_t count = std::min(params_.batchSize, images.size() - offset);
        const std::span<const PoseImage> chunk(images.data() + offset, count);
        std::vector<LetterboxTransform> transforms;
        const auto start = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
        const FloatTensor& input = inputWorkspace_->build(chunk, params_.batchSize, params_, transforms);
        const auto prepared = profile ? elapsed() : 0;
        const auto output = runFloat(session, input);
        const auto inferred = profile ? elapsed() : 0;
        if (!output.ok) throw std::runtime_error(output.error);
        if (output.outputs.empty()) continue;
        const auto chunkDetections = decodeOutput(output.outputs.front(), transforms, params_);
        for (std::size_t i = 0; i < count; ++i) detections.push_back(chunkDetections[i]);
        if (profile) {
            ++calls; preparationMs += prepared; runMs += inferred - prepared;
            decodingMs += elapsed() - inferred;
            if (calls == 1 || calls % 128 == 0)
                std::fprintf(stderr, "PF_DEBUG_INFERENCE pose_calls=%zu prepare_ms=%.3f inference_ms=%.3f decode_ms=%.3f batch=%zu\n",
                    calls, preparationMs, runMs, decodingMs, params_.batchSize);
        }
    }
    return detections;
}

} // namespace pfgpu
