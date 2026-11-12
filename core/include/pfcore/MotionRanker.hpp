#pragma once

#include <vector>
#include <map>

#include "pfcore/MotionMatcher.hpp"

namespace pfcore {

class MotionRanker {
public:
    static double organizationScore(const MotionMatch& match,bool requireIdentityEvidence=true) noexcept;
    static bool strongOrganizationEvidence(const MotionMatch& match,bool requireIdentityEvidence=true) noexcept;
    // Adds deterministic labels and rank scores, then orders strongest pairs
    // first. The input vector is intentionally mutable so export/UI consumers
    // receive the same order and metadata.
    static void rank(std::vector<MotionMatch>& matches,
                     const std::vector<MotionWindow>& windows);
    static bool organize(std::vector<MotionMatch>& matches,
        const std::vector<MotionWindow>& windows,
        const std::map<std::string,std::size_t>& selectedIdentities = {},
        const std::function<bool()>& cancelled = {},
        bool requireIdentityEvidence = true);
};

} // namespace pfcore
