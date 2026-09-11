#include "pfgpu/PoseEstimator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pfgpu {
namespace {

FloatTensor makeInput(const PoseImage& image, const PoseEstimatorParams& params)
{
    if (!image.rgba || image.width <= 0 || image.height <= 0) throw std::invalid_argument("PoseEstimator: invalid image");
    FloatTensor tensor;
    tensor.shape = {1, 3, params.inputHeight, params.inputWidth};
    tensor.values.resize(static_cast<std::size_t>(3 * params.inputWidth * params.inputHeight));
    for (int y = 0; y < params.inputHeight; ++y) {
        const int sy = std::min(image.height - 1, y * image.height / params.inputHeight);
        for (int x = 0; x < params.inputWidth; ++x) {
            const int sx = std::min(image.width - 1, x * image.width / params.inputWidth);
            const auto* pixel = image.rgba + (static_cast<std::size_t>(sy) * image.width + sx) * 4;
            const std::size_t offset = static_cast<std::size_t>(y) * params.inputWidth + x;
            tensor.values[offset] = pixel[0] / 255.0F;
            tensor.values[static_cast<std::size_t>(params.inputWidth) * params.inputHeight + offset] = pixel[1] / 255.0F;
            tensor.values[static_cast<std::size_t>(2 * params.inputWidth) * params.inputHeight + offset] = pixel[2] / 255.0F;
        }
    }
    return tensor;
}

} // namespace

PoseEstimator::PoseEstimator(std::string modelPath, PoseEstimatorParams params)
    : modelPath_(std::move(modelPath)), params_(std::move(params))
{
    if (modelPath_.empty() || params_.inputWidth <= 0 || params_.inputHeight <= 0
        || params_.keypointCount == 0 || params_.confidenceThreshold < 0.0F
        || params_.confidenceThreshold > 1.0F) throw std::invalid_argument("PoseEstimator: invalid parameters");
}

std::vector<PoseDetection> PoseEstimator::infer(const PoseImage& image)
{
    const FloatTensor input = makeInput(image, params_);
    const auto session = sessions_.getOrCreate(ModelRef::fromPath(modelPath_), {params_.provider, 0, params_.profile});
    if (!session.ok) throw std::runtime_error(session.error);
    auto output = runFloat(session.handle, input);
    if (!output.ok) throw std::runtime_error(output.error);
    if (output.outputs.empty()) return {};
    const auto& tensor = output.outputs.front();
    // Ultralytics' current end-to-end pose export adds a class column after
    // the confidence score: [x1,y1,x2,y2,score,class,kpts...].  Older
    // exports use the compact [cx,cy,w,h,score,kpts...] layout.  Accept both
    // layouts because the model exporter is intentionally version-agnostic.
    const std::size_t compactAttributes = 5 + params_.keypointCount * 3;
    const std::size_t endToEndAttributes = 6 + params_.keypointCount * 3;
    if (tensor.shape.size() != 3) throw std::runtime_error("PoseEstimator: unsupported output rank");
    const bool channelsFirst = tensor.shape[1] == static_cast<std::int64_t>(compactAttributes)
        || tensor.shape[1] == static_cast<std::int64_t>(endToEndAttributes);
    const std::size_t attributes = channelsFirst
        ? static_cast<std::size_t>(tensor.shape[1])
        : static_cast<std::size_t>(tensor.shape[2]);
    const bool hasClassColumn = attributes == endToEndAttributes;
    if (attributes != compactAttributes && !hasClassColumn)
        throw std::runtime_error("PoseEstimator: unsupported YOLO-pose attribute count");
    const std::size_t candidates = channelsFirst ? static_cast<std::size_t>(tensor.shape[2]) : static_cast<std::size_t>(tensor.shape[1]);
    if ((!channelsFirst && tensor.shape[2] != static_cast<std::int64_t>(attributes)) || candidates == 0)
        throw std::runtime_error("PoseEstimator: unsupported YOLO-pose output shape");
    auto at = [&](std::size_t candidate, std::size_t attribute) -> float {
        return channelsFirst ? tensor.values[attribute * candidates + candidate]
                             : tensor.values[candidate * attributes + attribute];
    };
    std::vector<PoseDetection> detections;
    for (std::size_t c = 0; c < candidates; ++c) {
        const float confidence = at(c, 4);
        if (confidence < params_.confidenceThreshold) continue;
        PoseDetection detection;
        detection.confidence = confidence;
        const float sx = static_cast<float>(image.width) / params_.inputWidth;
        const float sy = static_cast<float>(image.height) / params_.inputHeight;
        if (hasClassColumn) {
            detection.left = std::clamp(at(c, 0) * sx, 0.0F, static_cast<float>(image.width));
            detection.top = std::clamp(at(c, 1) * sy, 0.0F, static_cast<float>(image.height));
            detection.right = std::clamp(at(c, 2) * sx, detection.left, static_cast<float>(image.width));
            detection.bottom = std::clamp(at(c, 3) * sy, detection.top, static_cast<float>(image.height));
        } else {
            const float cx = at(c, 0) * sx, cy = at(c, 1) * sy;
            const float width = at(c, 2) * sx, height = at(c, 3) * sy;
            detection.left = std::max(0.0F, cx - width * 0.5F); detection.top = std::max(0.0F, cy - height * 0.5F);
            detection.right = std::min(static_cast<float>(image.width), cx + width * 0.5F);
            detection.bottom = std::min(static_cast<float>(image.height), cy + height * 0.5F);
        }
        const std::size_t keypointOffset = hasClassColumn ? 6 : 5;
        detection.keypoints.reserve(params_.keypointCount * 3);
        for (std::size_t k = 0; k < params_.keypointCount; ++k) {
            detection.keypoints.push_back(at(c, keypointOffset + k * 3) * sx);
            detection.keypoints.push_back(at(c, keypointOffset + 1 + k * 3) * sy);
            detection.keypoints.push_back(at(c, keypointOffset + 2 + k * 3));
        }
        detections.push_back(std::move(detection));
    }
    return detections;
}

} // namespace pfgpu
