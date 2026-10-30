#pragma once
#include <pfcore/SceneDescriptor.hpp>
#include <span>

namespace pfcore {
struct SceneOccurrence {
    std::size_t windowIndex=0;
    double start=0,end=0;
    double matchSimilarity=0;
    SceneDescriptor descriptor;
    bool headOnly=false;
};
struct SceneFamily {
    std::string canonicalId;
    std::vector<std::size_t> members;
    std::size_t representative=0;
    double representativeQuality=0;
};
struct SceneFamilySet {
    std::vector<SceneFamily> families;
    std::vector<std::size_t> membership;
    bool cancelled=false;
};
std::string canonicalSceneFingerprint(const std::string& source,std::size_t identityId,double start,double end);
double representativeQuality(const SceneOccurrence& occurrence);
SceneFamilySet buildSceneFamilies(std::span<const SceneOccurrence> occurrences,
    const std::function<bool()>& cancelled = {},
    const std::function<bool(const SceneOccurrence&,const SceneOccurrence&)>& copiedFootage = {});
// Practical local ordering. Shot boundaries and temporal distance remain
// costs, not fixed-minute bins or a replacement for continuous pose geometry.
std::vector<std::size_t> continuityOrder(std::span<const SceneOccurrence> occurrences,
    const std::function<bool()>& cancelled = {});
}
