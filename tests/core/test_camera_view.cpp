#include <pfcore/detail/CameraView.hpp>
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <random>

namespace {
// Frozen full-work reference: no prefix bound and the original accumulated
// denominator. It is intentionally independent of the optimized helper.
bool original(std::span<const float> a, std::span<const float> b)
{
    if (a.size() != 432 || b.size() != a.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!std::isfinite(a[i]) || !std::isfinite(b[i])
            || a[i] < 0 || b[i] < 0 || a[i] > 1 || b[i] > 1) return false;
    for (int shift = -1; shift <= 1; ++shift) {
        double difference = 0, weight = 0;
        for (int row = 0; row < 9; ++row) for (int column = std::max(0, -shift);
             column < std::min(16, 16 - shift); ++column) {
            const double w = ((column < 5 || column >= 11 ? 2.0 : 1.0)
                + (column + shift < 5 || column + shift >= 11 ? 2.0 : 1.0)) / 2.0;
            for (int channel = 0; channel < 3; ++channel) {
                difference += w * std::abs(a[(row*16+column)*3+channel]
                    - b[(row*16+column+shift)*3+channel]);
                weight += w;
            }
        }
        if (difference / weight <= .035) return true;
    }
    return false;
}
}

TEST(CameraView, PrefixBoundPreservesFullWorkDecisionForAllShiftsAndThresholdEdges)
{
    std::mt19937 rng(4519);
    std::uniform_real_distribution<float> pixels(0, 1), noise(-1, 1);
    std::array<float, 432> a, b;
    for (int trial = 0; trial < 20000; ++trial) {
        for (auto& value : a) value = pixels(rng);
        const int shift = trial % 3 - 1;
        const float amount = trial % 5 == 0 ? .5F : .035F + (trial % 21 - 10) * .0001F;
        for (int row = 0; row < 9; ++row) for (int col = 0; col < 16; ++col)
            for (int ch = 0; ch < 3; ++ch)
                b[(row*16+col)*3+ch] = std::clamp(a[(row*16+std::clamp(col-shift,0,15))*3+ch]
                    + noise(rng)*amount, 0.0F, 1.0F);
        ASSERT_EQ(pfcore::detail::sameCameraPixels(a,b), original(a,b)) << trial;
        ASSERT_EQ(pfcore::detail::sameCameraPixels(b,a), original(b,a)) << trial;
    }
    a.fill(0);
    for (float edge : {.035F, std::nextafter(.035F,0.0F), std::nextafter(.035F,1.0F)}) {
        b.fill(edge); EXPECT_EQ(pfcore::detail::sameCameraPixels(a,b), original(a,b));
    }
    b.fill(0);
    // A late invalid value must veto even when preceding rows are identical.
    for (float invalid : {-1.0F, 2.0F, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()}) {
        b.back() = invalid; EXPECT_FALSE(pfcore::detail::sameCameraPixels(a,b));
    }
    EXPECT_FALSE(pfcore::detail::sameCameraPixels(std::span(a).first(431),a));
}
