#include <pfgpu/detail/RgbaSampling.hpp>
#include <gtest/gtest.h>
#include <bit>
#include <limits>
#include <random>

TEST(RgbaSampling, SharedCoordinatesPreserveEveryScalarChannelBit)
{
    std::mt19937 random(1932);
    std::vector<std::uint8_t> pixels(47 * 31 * 4);
    for (auto& pixel : pixels) pixel = static_cast<std::uint8_t>(random());
    pfgpu::ReIdImage image{47, 31, pixels.data(), 0, 0, 47, 31};
    const auto scalar = [&](double x, double y, int channel) -> float {
        if (!std::isfinite(x+y) || x < 0 || y < 0 || x > image.width-1 || y > image.height-1) return 0;
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        const int nx = std::min(ix+1, image.width-1), ny = std::min(iy+1, image.height-1);
        const auto at = [&](int px, int py) { return image.rgba[(static_cast<std::size_t>(py)*image.width+px)*4+channel]; };
        const double dx = x-ix, dy = y-iy;
        return static_cast<float>((1-dy)*((1-dx)*at(ix,iy)+dx*at(nx,iy))
            + dy*((1-dx)*at(ix,ny)+dx*at(nx,ny)));
    };
    const auto check = [&](double x, double y) {
        const auto rgb = pfgpu::detail::sampleRgb(image, x, y);
        for (int c = 0; c < 3; ++c) EXPECT_EQ(std::bit_cast<std::uint32_t>(rgb[c]), std::bit_cast<std::uint32_t>(scalar(x,y,c)));
    };
    for (const double x : {-1., 0., .5, 45.999, 46., 47., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        for (const double y : {-1., 0., .5, 29.999, 30., 31.}) check(x,y);
    std::uniform_real_distribution<double> xs(-2, 49), ys(-2, 33);
    for (int i = 0; i < 10000; ++i) check(xs(random), ys(random));
}
