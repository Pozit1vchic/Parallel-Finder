#pragma once
#include "pfcore/DominantPerson.hpp"
#include <string_view>

namespace pfcore {
// Candidate links preserve independent scene, pose, body and conflict checks.
struct RepeatedSceneIdentityLink {
    std::size_t leftCandidate = 0, rightCandidate = 0;
    std::size_t leftTrack = 0, rightTrack = 0;
    double sceneError = 0, bodySimilarity = 0;
};
std::vector<RepeatedSceneIdentityLink> auditRepeatedSceneIdentityLinks(
    const std::vector<CharacterCandidate>& candidates,
    const std::vector<MotionWindow>& windows,
    const std::function<bool()>& cancelled = {}, std::string_view source = {});
// Applies only unambiguous, disjoint links. Existing appearances keep their
// order and provenance so their previously saved preview keys stay valid.
std::size_t mergeRepeatedSceneIdentities(std::vector<CharacterCandidate>& candidates,
    const std::vector<MotionWindow>& windows, const std::function<bool()>& cancelled = {},
    std::string_view source = {});
}
