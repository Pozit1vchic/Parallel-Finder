#pragma once
#include <algorithm>
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
    bool observed=false,continuous=false,associated=false,torsoVisible=false,postureVisible=false;
    std::vector<double> frameTimes;
    std::array<double,80> scores{},frozenScores{};
};
inline bool validVideoActionEvidence(const VideoActionEvidence& evidence,
    const std::string& source,double start,double end,bool requireFullTorso=true) {
    if(!evidence.observed || !evidence.continuous || !evidence.associated
        || (requireFullTorso?!evidence.torsoVisible:!evidence.postureVisible)
        || source.empty() || evidence.sourceId!=source || evidence.modelHash!=kVideoActionModelHash
        || !std::isfinite(start) || !std::isfinite(end) || end-start<.5 || end-start>3
        || !std::isfinite(evidence.start) || !std::isfinite(evidence.end)
        || std::abs(evidence.start-start)>1e-6 || std::abs(evidence.end-end)>1e-6
        || evidence.frameTimes.size()<5 || evidence.frameTimes.size()>192)return false;
    double previous=-INFINITY;
    for(const double time:evidence.frameTimes) {
        if(!std::isfinite(time) || time<start-1e-6 || time>=end-1e-6 || time<=previous
            || (std::isfinite(previous) && time-previous>.2+1e-6))return false;
        previous=time;
    }
    if(evidence.frameTimes.front()-start>.2 || end-evidence.frameTimes.back()>.2
        || evidence.frameTimes.back()-evidence.frameTimes.front()<.3)return false;
    for(int i=0;i<80;++i)
        if(!std::isfinite(evidence.scores[i]) || !std::isfinite(evidence.frozenScores[i])
            || evidence.scores[i]<0 || evidence.scores[i]>1
            || evidence.frozenScores[i]<0 || evidence.frozenScores[i]>1)return false;
    return true;
}
inline std::optional<double> measuredWalking(const VideoActionEvidence& evidence,
    const std::string& source,double start,double end) {
    if(!validVideoActionEvidence(evidence,source,start,end))return {};
    // The raw score is evidence strength, not calibrated correctness. Demand
    // actual temporal evidence and reject ambiguous stand/run transitions.
    const double walk=evidence.scores[13];
    if(walk<.9 || walk-evidence.frozenScores[13]<.5
        || walk-evidence.scores[11]<.2 || walk-evidence.scores[9]<.2)return {};
    return walk;
}
// AVA v2.2 class ids 8/11/12 are lie/sleep, sit and stand. The model emits
// uncalibrated sigmoid scores, so posture labels require temporal and frozen
// views to agree with a clear margin. Any ambiguity stays unknown.
inline std::string measuredPosture(const VideoActionEvidence& evidence,
    const std::string& source,double start,double end) {
    if(!validVideoActionEvidence(evidence,source,start,end,false))return "unknown";
    constexpr int lying=7,sitting=10,standing=11; // AVA id minus one
    const double reclined=std::max(evidence.scores[lying],evidence.scores[sitting]);
    const double reclinedFrozen=std::max(evidence.frozenScores[lying],evidence.frozenScores[sitting]);
    const double upright=evidence.scores[standing],uprightFrozen=evidence.frozenScores[standing];
    if(reclined>=.65 && reclinedFrozen>=.55 && reclined-upright>=.2
        && reclinedFrozen-uprightFrozen>=.15)return "seated_lying";
    if(upright>=.7 && uprightFrozen>=.6 && upright-reclined>=.25
        && uprightFrozen-reclinedFrozen>=.2)return "standing";
    return "unknown";
}
}
