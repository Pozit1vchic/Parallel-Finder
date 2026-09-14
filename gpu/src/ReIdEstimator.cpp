#include "pfgpu/ReIdEstimator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pfgpu {
namespace {

struct InputShape {
    int width = 128;
    int height = 256;
    std::size_t batch = 1;
};

InputShape inspectInput(const SessionSpec& description,
                        const ReIdEstimatorParams& params,
                        bool& channelsFirst)
{
    const auto& shape = description.input.shape;
    if (shape.size() != 4) throw std::runtime_error("ReIdEstimator: model input must be rank 4");
    InputShape result;
    if (shape[0] > 0) result.batch = static_cast<std::size_t>(shape[0]);
    if (result.batch != 1) {
        throw std::runtime_error("ReIdEstimator: only batch-1 ReID models are supported");
    }
    if (shape[1] == 3 || shape[1] <= 0) {
        channelsFirst = true;
        result.height = shape[2] > 0 ? static_cast<int>(shape[2]) : params.inputHeight;
        result.width = shape[3] > 0 ? static_cast<int>(shape[3]) : params.inputWidth;
    } else if (shape[3] == 3 || shape[3] <= 0) {
        channelsFirst = false;
        result.height = shape[1] > 0 ? static_cast<int>(shape[1]) : params.inputHeight;
        result.width = shape[2] > 0 ? static_cast<int>(shape[2]) : params.inputWidth;
    } else {
        throw std::runtime_error("ReIdEstimator: expected NCHW/NHWC RGB input");
    }
    if (result.width <= 0 || result.height <= 0 || result.width > 2048 || result.height > 2048)
        throw std::runtime_error("ReIdEstimator: invalid model input dimensions");
    return result;
}

FloatTensor makeInput(const ReIdImage& image, const InputShape& shape, bool channelsFirst)
{
    if (!image.rgba || image.width <= 0 || image.height <= 0)
        throw std::invalid_argument("ReIdEstimator: invalid frame");
    const float left = std::clamp(image.left, 0.0F, static_cast<float>(image.width - 1));
    const float top = std::clamp(image.top, 0.0F, static_cast<float>(image.height - 1));
    const float right = std::clamp(image.right, left + 1.0F, static_cast<float>(image.width));
    const float bottom = std::clamp(image.bottom, top + 1.0F, static_cast<float>(image.height));
    const float cropWidth = std::max(1.0F, right - left);
    const float cropHeight = std::max(1.0F, bottom - top);
    const std::size_t plane = static_cast<std::size_t>(shape.width) * shape.height;
    FloatTensor tensor;
    tensor.shape = {1, channelsFirst ? 3 : shape.height,
                    channelsFirst ? shape.height : shape.width,
                    channelsFirst ? shape.width : 3};
    tensor.values.resize(3U * plane);
    // ImageNet normalization is the convention used by the common OSNet
    // exports.  It also keeps models exported from Torchreid/FastReID
    // numerically compatible without an external image library.
    constexpr float mean[] = {0.485F, 0.456F, 0.406F};
    constexpr float stdev[] = {0.229F, 0.224F, 0.225F};
    for (int y = 0; y < shape.height; ++y) {
        const int sourceY = std::clamp(static_cast<int>(((y + 0.5F) * cropHeight
            / static_cast<float>(shape.height)) + top), 0, image.height - 1);
        for (int x = 0; x < shape.width; ++x) {
            const int sourceX = std::clamp(static_cast<int>(((x + 0.5F) * cropWidth
                / static_cast<float>(shape.width)) + left), 0, image.width - 1);
            const auto* pixel = image.rgba
                + (static_cast<std::size_t>(sourceY) * image.width + sourceX) * 4U;
            const std::size_t offset = static_cast<std::size_t>(y) * shape.width + x;
            const float channels[3] = {pixel[0] / 255.0F, pixel[1] / 255.0F, pixel[2] / 255.0F};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const float normalized = (channels[channel] - mean[channel]) / stdev[channel];
                if (channelsFirst) tensor.values[channel * plane + offset] = normalized;
                else tensor.values[offset * 3U + channel] = normalized;
            }
        }
    }
    return tensor;
}

std::vector<float> normalizedEmbedding(const FloatTensor& tensor)
{
    if (tensor.values.empty()) throw std::runtime_error("ReIdEstimator: model returned an empty embedding");
    std::vector<float> embedding = tensor.values;
    double norm = 0.0;
    for (const float value : embedding) {
        if (!std::isfinite(value)) throw std::runtime_error("ReIdEstimator: model returned NaN/Inf");
        norm += static_cast<double>(value) * value;
    }
    norm = std::sqrt(norm);
    if (!(norm > std::numeric_limits<double>::epsilon()))
        throw std::runtime_error("ReIdEstimator: model returned a zero embedding");
    for (float& value : embedding) value = static_cast<float>(value / norm);
    return embedding;
}

} // namespace

ReIdEstimator::ReIdEstimator(std::string modelPath, ReIdEstimatorParams params)
    : modelPath_(std::move(modelPath)), params_(std::move(params))
{
    if (modelPath_.empty() || params_.inputWidth <= 0 || params_.inputHeight <= 0
        || params_.intraOpThreads > 256)
        throw std::invalid_argument("ReIdEstimator: invalid parameters");
}

std::vector<float> ReIdEstimator::infer(const ReIdImage& image)
{
    const auto batch = inferBatch({image});
    return batch.empty() ? std::vector<float> {} : batch.front();
}

std::vector<std::vector<float>> ReIdEstimator::inferBatch(const std::vector<ReIdImage>& images)
{
    if (images.empty()) return {};
    const auto session = sessions_.getOrCreate(ModelRef::fromPath(modelPath_),
                                               {params_.provider, 0, params_.profile,
                                                params_.intraOpThreads});
    if (!session.ok) throw std::runtime_error(session.error);
    if (!sessionSpec_.has_value()) {
        SessionSpec description = describeSession(session.handle);
        if (!description.ok) throw std::runtime_error(description.error);
        if (description.outputs.empty()) throw std::runtime_error("ReIdEstimator: model has no outputs");
        InputShape shape = inspectInput(description, params_, channelsFirst_);
        // The current implementation intentionally uses one crop per ORT run.
        // It accepts one image at a time, avoiding a static-batch/profile mismatch
        // across the different OSNet exports users commonly have.
        if (shape.batch != 1) throw std::runtime_error("ReIdEstimator: model batch must be 1");
        sessionSpec_ = std::move(description);
    }
    const InputShape shape = inspectInput(*sessionSpec_, params_, channelsFirst_);
    std::vector<std::vector<float>> result;
    result.reserve(images.size());
    for (const auto& image : images) {
        const auto output = runFloat(session.handle, makeInput(image, shape, channelsFirst_));
        if (!output.ok) throw std::runtime_error(output.error);
        if (output.outputs.empty()) throw std::runtime_error("ReIdEstimator: model returned no outputs");
        result.push_back(normalizedEmbedding(output.outputs.front()));
    }
    return result;
}

} // namespace pfgpu
