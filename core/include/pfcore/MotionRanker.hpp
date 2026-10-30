#pragma once

#include <vector>
#include <map>

#include "pfcore/MotionMatcher.hpp"

namespace pfcore {

class MotionRanker {
public:
    // Adds deterministic labels and rank scores, then orders strongest pairs
    // first. The input vector is intentionally mutable so export/UI consumers
    // receive the same order and metadata.
    static void rank(std::vector<MotionMatch>& matches,
                     const std::vector<MotionWindow>& windows);
    static bool organize(std::vector<MotionMatch>& matches,
        const std::vector<MotionWindow>& windows,
        const std::map<std::string,std::size_t>& selectedIdentities = {},
        const std::function<bool()>& cancelled = {});
};

} // namespace pfcore
