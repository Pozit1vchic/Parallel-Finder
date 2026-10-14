#pragma once

#include <array>
#include <span>

#include "pfcore/MovementClassifier.hpp"
#include "pfcore/HeadOrientationSample.hpp"

namespace pfcore {

// Temporal action evidence, not an identity measurement or calibrated accuracy.
// It deliberately has no Euler-angle input: full rotations remain continuous
// when yaw crosses 180 degrees or a face approaches a profile.
// All ranges are half-open [start,end), matching the exported video interval.
// A measured frame at the exclusive end cannot supply a pose or action phase.
[[nodiscard]] MovementClassification classifyHeadOrientation(
    std::span<const HeadOrientationSample> samples, double start, double end);
[[nodiscard]] std::string classifyHeadPoseOrientation(
    std::span<const HeadOrientationSample> samples, double start, double end);
// Refines a verified yaw action relative to the camera, never to an inferred
// off-screen partner. Generic head_turn is retained for uncertain endpoints,
// a sideways-to-sideways turn, or a turn away and back in the same interval.
[[nodiscard]] std::string classifyHeadTurnPhase(
    std::span<const HeadOrientationSample> samples, double start, double end);

} // namespace pfcore
