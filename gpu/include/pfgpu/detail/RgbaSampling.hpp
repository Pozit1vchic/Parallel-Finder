#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <pfgpu/ReIdEstimator.hpp>

namespace pfgpu::detail {
// Identical bilinear arithmetic to the scalar face sampler, sharing only
// coordinate validation and pixel addresses between the RGB channels.
inline std::array<float, 3> sampleRgb(const ReIdImage& image, double x, double y)
{
    if (!std::isfinite(x + y) || x < 0 || y < 0 || x > image.width - 1 || y > image.height - 1) return {};
    const int ix = static_cast<int>(x), iy = static_cast<int>(y);
    const int nx = std::min(ix + 1, image.width - 1), ny = std::min(iy + 1, image.height - 1);
    const auto at = [&](int px, int py) { return image.rgba + (static_cast<std::size_t>(py) * image.width + px) * 4; };
    const auto *p00 = at(ix, iy), *p10 = at(nx, iy), *p01 = at(ix, ny), *p11 = at(nx, ny);
    const double dx = x - ix, dy = y - iy;
    std::array<float, 3> result;
    for (int c = 0; c < 3; ++c)
        result[c] = static_cast<float>((1-dy) * ((1-dx)*p00[c] + dx*p10[c])
            + dy*((1-dx)*p01[c] + dx*p11[c]));
    return result;
}
} // namespace pfgpu::detail
