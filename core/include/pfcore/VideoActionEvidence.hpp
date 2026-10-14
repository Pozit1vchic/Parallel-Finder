#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace pfcore {
inline constexpr auto kVideoActionModelHash="b9316ea2de6a29282fa1567246dc74670052525319e48cbab02b6b91ff10c481";
// Independent post-match measurements; never identity or matcher admission.
struct VideoActionEvidence {
    std::string sourceId,modelHash;
    double start=0,end=0;
    bool observed=false,continuous=false,associated=false,torsoVisible=false;
    std::vector<double> frameTimes;
    std::array<double,80> scores{},frozenScores{};
};
inline std::optional<double> measuredWalking(const VideoActionEvidence& evidence,
    const std::string& source,double start,double end) {
    if(!evidence.observed || !evidence.continuous || !evidence.associated || !evidence.torsoVisible
        || source.empty() || evidence.sourceId!=source || evidence.modelHash!=kVideoActionModelHash
        || !std::isfinite(start) || !std::isfinite(end) || end-start<.5 || end-start>3
        || !std::isfinite(evidence.start) || !std::isfinite(evidence.end)
        || std::abs(evidence.start-start)>1e-6 || std::abs(evidence.end-end)>1e-6
        || evidence.frameTimes.size()<5 || evidence.frameTimes.size()>192)return {};
    double previous=-INFINITY;
    for(const double time:evidence.frameTimes) {
        if(!std::isfinite(time) || time<start-1e-6 || time>=end-1e-6 || time<=previous
            || (std::isfinite(previous) && time-previous>.2+1e-6))return {};
        previous=time;
    }
    if(evidence.frameTimes.front()-start>.2 || end-evidence.frameTimes.back()>.2
        || evidence.frameTimes.back()-evidence.frameTimes.front()<.3)return {};
    for(int i=0;i<80;++i)
        if(!std::isfinite(evidence.scores[i]) || !std::isfinite(evidence.frozenScores[i])
            || evidence.scores[i]<0 || evidence.scores[i]>1
            || evidence.frozenScores[i]<0 || evidence.frozenScores[i]>1)return {};
    // The raw score is evidence strength, not calibrated correctness. Demand
    // actual temporal evidence and reject ambiguous stand/run transitions.
    const double walk=evidence.scores[13];
    if(walk<.9 || walk-evidence.frozenScores[13]<.5
        || walk-evidence.scores[11]<.2 || walk-evidence.scores[9]<.2)return {};
    return walk;
}
}
