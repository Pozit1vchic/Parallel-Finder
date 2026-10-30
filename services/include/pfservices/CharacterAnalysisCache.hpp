#pragma once
#include <pfcore/DominantPerson.hpp>
#include <span>
#include <cstdint>

namespace pfservices {
struct CharacterAnalysis {
    std::vector<pfcore::CharacterCandidate> candidates;
    std::vector<pfcore::MotionWindow> windows;
    int sceneCount = 0;
};
// The outer PfCache key fingerprints source, models, inference settings and
// pipeline version. This envelope versions the candidate/window relationship.
// Readers validate into a temporary object and publish only a complete record.
std::vector<std::uint8_t> serializeCharacterAnalysis(const CharacterAnalysis& analysis,
    const std::function<bool()>& cancelled = {});
bool deserializeCharacterAnalysis(std::span<const std::uint8_t> bytes, CharacterAnalysis& analysis);
}
