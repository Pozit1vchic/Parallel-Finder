#pragma once
#include <span>
#include <vector>

namespace pfcore {
struct DirectFaceSample {
    double timestampSeconds = 0;
    std::vector<float> embedding;
};
enum class FaceConsensusDecision { Insufficient, Supported, Contradicted };
struct FaceConsensusResult {
    FaceConsensusDecision decision = FaceConsensusDecision::Insufficient;
    std::size_t leftIndependent = 0, rightIndependent = 0;
    std::size_t leftSupported = 0, rightSupported = 0;
    // Mean of each supported sample's median cross-reference cosine. This is
    // an observed descriptor score, not a probability of correct identity.
    double meanSupportedSimilarity = 0;
};
// Experimental evidence check, not a replacement for matcher admission.
// Both ranges use their own source timeline. Missing samples prove nothing.
FaceConsensusResult compareDirectFaces(std::span<const DirectFaceSample> left,
    double leftStart, double leftEnd, std::span<const DirectFaceSample> right,
    double rightStart, double rightEnd);
}
