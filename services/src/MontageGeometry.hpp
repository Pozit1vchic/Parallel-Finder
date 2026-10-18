#pragma once

#include <pfcore/VideoDecoder.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace pfservices::detail {

inline std::optional<std::pair<int, int>> montageCanvas(const pfcore::VideoInfo& info,
    int maxWidth = 0, int maxHeight = 0)
{
    if (maxWidth < 0 || maxHeight < 0 || maxWidth == 1 || maxHeight == 1
        || maxWidth > 32768 || maxHeight > 32768) return std::nullopt;
    if (info.width <= 0 || info.height <= 0 || !std::isfinite(info.sampleAspectRatio)
        || info.sampleAspectRatio <= 0.0 || !std::isfinite(info.rotationDegrees)) return std::nullopt;
    const double width = std::max(2.0, std::round(info.width * info.sampleAspectRatio));
    const double height = std::max(2.0, static_cast<double>(info.height));
    // Match the decoder's 64M-pixel resource limit, before converting metadata
    // to int. Extreme SAR otherwise causes undefined float-to-int conversion.
    if (!std::isfinite(width) || width > std::numeric_limits<int>::max()
        || width * height > 64.0 * 1024.0 * 1024.0) return std::nullopt;
    std::pair<int, int> canvas{static_cast<int>(width) / 2 * 2,
                               std::max(2, info.height / 2 * 2)};
    if (std::abs(info.rotationDegrees) > 45.0 && std::abs(info.rotationDegrees) < 135.0)
        std::swap(canvas.first, canvas.second);
    if (maxWidth > 0 || maxHeight > 0) {
        const double widthScale = maxWidth > 0 ? static_cast<double>(maxWidth) / canvas.first : 1.0;
        const double heightScale = maxHeight > 0 ? static_cast<double>(maxHeight) / canvas.second : 1.0;
        const double scale = std::min({1.0, widthScale, heightScale});
        canvas.first = std::max(2, static_cast<int>(std::floor(canvas.first * scale)) / 2 * 2);
        canvas.second = std::max(2, static_cast<int>(std::floor(canvas.second * scale)) / 2 * 2);
    }
    return canvas;
}

} // namespace pfservices::detail
