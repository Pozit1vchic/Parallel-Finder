#pragma once

#include <pfcore/MotionMatcher.hpp>
#include <pfservices/PfCache.hpp>
#include <optional>
#include <span>

namespace pfservices {

// Exact final-result reuse. Inputs are hashed in order, with every matcher
// setting and the compiled algorithm contract; no approximate identity key.
class MatchCache {
public:
    static std::string key(std::span<const pfcore::MotionWindow> windows,
                           const pfcore::MotionMatcherParams& params);
    static std::optional<std::vector<pfcore::MotionMatch>> load(
        const PfCache& cache, const std::string& key,
        std::span<const pfcore::MotionWindow> windows);
    static bool store(PfCache& cache, const std::string& key,
                      std::span<const pfcore::MotionMatch> matches, std::string& error);
};

} // namespace pfservices
