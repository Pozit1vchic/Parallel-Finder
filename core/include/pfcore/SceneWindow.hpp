#pragma once

#include "pfcore/MotionMatcher.hpp"
#include <span>
#include <vector>

namespace pfcore {
struct ObservedSceneRange {
    double start = 0;
    double end = 0;
    std::size_t sceneIndex = 0;
};
struct SceneWindowPart {
    MotionWindow window;
    // Old shot-level prototypes are invalid after a discovered internal cut,
    // even if this particular chunk's poses were all on one side of the cut.
    // Callers must obtain new identity evidence before admitting these parts.
    bool identityNeedsRefresh = false;
};

// Repartition cached poses using verified, contiguous [start,end) shots.
// Input and cache remain unchanged. Invalid/incomplete scene coverage throws;
// no cut is guessed from pose motion. Scene IDs must be supplied consistently
// across all chunks/tracks belonging to these same observed shots.
std::vector<SceneWindowPart> splitMotionWindowAtScenes(const MotionWindow& window,
    std::span<const ObservedSceneRange> scenes);
}
