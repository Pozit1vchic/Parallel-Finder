#include "pfgpu/detail/PoseInput.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace pfgpu::detail {
namespace {
constexpr std::size_t kMaxInputElements = 64ULL * 1024ULL * 1024ULL;
const std::array<float, 256>& normalizedBytes()
{
    static const auto table = [] {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<float>(i) / 255.0F;
        return values;
    }();
    return table;
}
}

void PoseInputWorkspace::mapImage(const PoseImage& image, const PoseEstimatorParams& params)
{
    if (!image.rgba || image.width <= 0 || image.height <= 0)
        throw std::invalid_argument("PoseEstimator: invalid image");
    if (image.width == imageWidth_ && image.height == imageHeight_
        && params.inputWidth == inputWidth_ && params.inputHeight == inputHeight_) return;
    transform_.sourceWidth = image.width;
    transform_.sourceHeight = image.height;
    transform_.scale = std::min(static_cast<float>(params.inputWidth) / image.width,
                                static_cast<float>(params.inputHeight) / image.height);
    const int resizedWidth = std::max(1, static_cast<int>(std::lround(image.width * transform_.scale)));
    const int resizedHeight = std::max(1, static_cast<int>(std::lround(image.height * transform_.scale)));
    transform_.padX = (params.inputWidth - resizedWidth) * 0.5F;
    transform_.padY = (params.inputHeight - resizedHeight) * 0.5F;
    sourceX_.assign(static_cast<std::size_t>(params.inputWidth), -1);
    sourceY_.assign(static_cast<std::size_t>(params.inputHeight), -1);
    firstX_ = params.inputWidth; lastX_ = 0;
    for (int x = 0; x < params.inputWidth; ++x) {
        const float value = (static_cast<float>(x) - transform_.padX) / transform_.scale;
        if (value >= 0.0F && value < image.width) {
            sourceX_[x] = std::clamp(static_cast<int>(value), 0, image.width - 1);
            firstX_ = std::min(firstX_, x); lastX_ = x + 1;
        }
    }
    for (int y = 0; y < params.inputHeight; ++y) {
        const float value = (static_cast<float>(y) - transform_.padY) / transform_.scale;
        if (value >= 0.0F && value < image.height)
            sourceY_[y] = std::clamp(static_cast<int>(value), 0, image.height - 1);
    }
    imageWidth_ = image.width; imageHeight_ = image.height;
    inputWidth_ = params.inputWidth; inputHeight_ = params.inputHeight;
}

const FloatTensor& PoseInputWorkspace::build(std::span<const PoseImage> images,
                                           std::size_t batchSize,
                                           const PoseEstimatorParams& params,
                                           std::vector<LetterboxTransform>& transforms)
{
    if (images.empty() || batchSize == 0 || images.size() > batchSize
        || params.inputWidth <= 0 || params.inputHeight <= 0)
        throw std::invalid_argument("PoseEstimator: invalid input batch");
    const auto plane = static_cast<std::size_t>(params.inputWidth) * params.inputHeight;
    if (plane > kMaxInputElements / 3U || batchSize > kMaxInputElements / (3U * plane))
        throw std::invalid_argument("PoseEstimator: input batch exceeds tensor limit");
    tensor_.shape = {static_cast<std::int64_t>(batchSize), 3, params.inputHeight, params.inputWidth};
    tensor_.values.resize(batchSize * 3U * plane);
    transforms.clear(); transforms.reserve(batchSize);
    const auto& bytes = normalizedBytes();
    constexpr float pad = 114.0F / 255.0F;
    for (std::size_t batch = 0; batch < images.size(); ++batch) {
        const auto& image = images[batch];
        mapImage(image, params);
        transforms.push_back(transform_);
        auto* red = tensor_.values.data() + batch * 3U * plane;
        auto* green = red + plane;
        auto* blue = green + plane;
        for (int y = 0; y < params.inputHeight; ++y) {
            const auto row = static_cast<std::size_t>(y) * params.inputWidth;
            const int sourceY = sourceY_[y];
            if (sourceY < 0 || firstX_ >= lastX_) {
                for (auto* channel : {red, green, blue})
                    std::fill_n(channel + row, params.inputWidth, pad);
                continue;
            }
            for (auto* channel : {red, green, blue}) {
                std::fill_n(channel + row, firstX_, pad);
                std::fill(channel + row + lastX_, channel + row + params.inputWidth, pad);
            }
            const auto* sourceRow = image.rgba + static_cast<std::size_t>(sourceY) * image.width * 4U;
            // No per-pixel pad branch, division, or initial full-buffer fill.
            // Every output value is written exactly once on a reused buffer.
            for (int x = firstX_; x < lastX_; ++x) {
                const auto* pixel = sourceRow + static_cast<std::size_t>(sourceX_[x]) * 4U;
                red[row + x] = bytes[pixel[0]];
                green[row + x] = bytes[pixel[1]];
                blue[row + x] = bytes[pixel[2]];
            }
        }
    }
    // Fixed-batch tail uses identical input pixels, not a second resize pass.
    for (std::size_t batch = images.size(); batch < batchSize; ++batch) {
        std::copy_n(tensor_.values.data() + (images.size() - 1U) * 3U * plane,
                    3U * plane, tensor_.values.data() + batch * 3U * plane);
        transforms.push_back(transforms[images.size() - 1U]);
    }
    return tensor_;
}
} // namespace pfgpu::detail
