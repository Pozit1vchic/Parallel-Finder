#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <optional>

namespace pfcore {
struct CachedSceneSequence {
    std::uint64_t scene=0;
    double start=0,end=0;
    std::vector<float> pixels;
    std::vector<double> times;
};
// Native binary observations, not executable input. The versioned format is
// little-endian and rejects truncated, oversized or non-finite measurements.
std::vector<std::uint8_t> encodeSceneSequences(std::span<const CachedSceneSequence> scenes);
std::optional<std::vector<CachedSceneSequence>> decodeSceneSequences(std::span<const std::uint8_t> bytes);
}
