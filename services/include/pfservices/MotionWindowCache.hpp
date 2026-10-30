#pragma once
#include <pfcore/MotionMatcher.hpp>
#include <cstdint>
#include <span>
#include <vector>
#include <functional>

namespace pfservices {
// Versioned observation cache codec. Spans borrow only for the synchronous call.
std::vector<std::uint8_t> serializeMotionWindows(std::span<const pfcore::MotionWindow> windows, int sceneCount,
    const std::function<bool()>& cancelled = {});
bool deserializeMotionWindows(std::span<const std::uint8_t> input,
                              std::vector<pfcore::MotionWindow>& windows, int& sceneCount);
}
