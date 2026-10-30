#pragma once
#include <pfcore/DominantPerson.hpp>
#include <array>
#include <span>

namespace pfcore {
struct ObservedFaceLandmarks {
    bool observed=false;
    std::array<double,10> points{};
};
// One measured face can own only one body in a source frame. Ambiguous
// association removes identity evidence rather than lending a neighbour's face.
void enforceUniqueFaceOwnership(std::vector<PersonDetection>& people,
    std::span<const ObservedFaceLandmarks> faces);
}
