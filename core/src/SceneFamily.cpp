#include <pfcore/SceneFamily.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <sstream>
#include <tuple>
#include <map>

namespace pfcore {
std::string canonicalSceneFingerprint(const std::string& source,std::size_t identityId,double start,double end) {
    if (source.empty()||!identityId||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>1e9) return {};
    const auto text=source+"\n"+std::to_string(identityId)+"\n"+std::to_string(std::llround(start*1e6))+"\n"+std::to_string(std::llround(end*1e6));
    std::uint64_t hash=14695981039346656037ULL;
    for (const auto c:text) {hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;}
    std::ostringstream output;output<<"scene-v1-"<<std::hex<<hash;return output.str();
}
double representativeQuality(const SceneOccurrence& o) {
    const auto& d=o.descriptor;
    const auto unit=[](double v){return std::isfinite(v)?std::clamp(v,0.,1.):0.;};
    return std::clamp(.55*unit(o.headOnly?d.headEvidenceQuality:d.evidenceQuality)+.20*unit(d.identityConfidence)
        +.15*unit(o.headOnly?d.headTemporalCoverage:d.temporalCoverage)
        +.10*unit(o.matchSimilarity)-.15*unit(d.cameraInfluence.value_or(0.)),0.,1.);
}
namespace {
auto key(const SceneOccurrence& o) {return std::tuple{o.descriptor.sourceId,o.descriptor.identityId,o.start,o.end,o.windowIndex,o.headOnly};}
}
SceneFamilyComparison compareSceneFamilyOccurrences(const SceneOccurrence& a,const SceneOccurrence& b,
    const std::function<bool(const SceneOccurrence&,const SceneOccurrence&)>& copiedFootage) {
    SceneFamilyComparison result;
    const auto reject=[&](SceneFamilyReason reason){result.reason=reason;return result;};
    if(!std::isfinite(a.start)||!std::isfinite(a.end)||!std::isfinite(b.start)||!std::isfinite(b.end)
        ||a.start<0||b.start<0||a.end<=a.start||b.end<=b.start)return result;
    if (a.descriptor.sourceId!=b.descriptor.sourceId || a.descriptor.identityId!=b.descriptor.identityId
        || !a.descriptor.identityId || a.descriptor.sourceId.empty())return reject(SceneFamilyReason::DifferentIdentity);
    const double intersection=std::max(0.,std::min(a.end,b.end)-std::max(a.start,b.start));
    const double overlap=intersection/std::min(a.end-a.start,b.end-b.start);
    result.temporalOverlap=overlap;
    // Distant repeated edits require pixel-sequence proof. Keep pose-only
    // equivalence local to a shot; recurring actions remain independent.
    if(copiedFootage && copiedFootage(a,b)) {result.accepted=true;result.reason=SceneFamilyReason::CopiedFootage;return result;}
    if (a.descriptor.sceneId && b.descriptor.sceneId && a.descriptor.sceneId!=b.descriptor.sceneId)return reject(SceneFamilyReason::DifferentScene);
    if (a.descriptor.sequenceId && b.descriptor.sequenceId && a.descriptor.sequenceId!=b.descriptor.sequenceId)return reject(SceneFamilyReason::DifferentSequence);
    const auto& ca=a.descriptor.sceneContext;const auto& cb=b.descriptor.sceneContext;
    if(!ca.empty()&&!cb.empty()) {
        if(ca.size()!=cb.size())return reject(SceneFamilyReason::DifferentContext);
        double dot=0,na=0,nb=0;
        for(std::size_t i=0;i<ca.size();++i) {
            if(!std::isfinite(ca[i])||!std::isfinite(cb[i]))return reject(SceneFamilyReason::DifferentContext);
            dot+=double(ca[i])*cb[i];na+=double(ca[i])*ca[i];nb+=double(cb[i])*cb[i];
        }
        if(na<=1e-12||nb<=1e-12)return reject(SceneFamilyReason::DifferentContext);
        result.contextSimilarity=dot/std::sqrt(na*nb);
        if(result.contextSimilarity<.9)return reject(SceneFamilyReason::DifferentContext);
    }
    const double gap=std::max(a.start,b.start)-std::min(a.end,b.end);
    if (overlap+1e-9<.60 && !(gap>=0 && gap<=.12))return reject(SceneFamilyReason::DistantOccurrence);
    // Mixed-scope comparisons need evidence both occurrences actually share.
    // A head-only match does not establish the body pose as its motion.
    const auto distance=a.headOnly||b.headOnly?sceneHeadDistance(a.descriptor,b.descriptor):scenePoseDistance(a.descriptor,b.descriptor);
    if (!distance||!std::isfinite(*distance))return reject(SceneFamilyReason::MissingPose);
    result.poseDistance=*distance;
    if (*distance>(overlap>=.60?.16:.08))return reject(SceneFamilyReason::DifferentPose);
    const auto& x=a.descriptor.motion;const auto& y=b.descriptor.motion;
    if (x.gesture!=GestureClass::Unknown && y.gesture!=GestureClass::Unknown && x.gesture!=y.gesture)return reject(SceneFamilyReason::DifferentAction);
    if (x.direction!=MovementDirection::Unknown && y.direction!=MovementDirection::Unknown && x.direction!=y.direction)return reject(SceneFamilyReason::DifferentDirection);
    if (a.descriptor.posture!=ScenePosture::Unknown && b.descriptor.posture!=ScenePosture::Unknown
        && a.descriptor.posture!=b.descriptor.posture)return reject(SceneFamilyReason::DifferentPosture);
    // Missing pose or unknown identity never proves two scenes are duplicates.
    result.accepted=true;result.reason=overlap+1e-9>=.60?SceneFamilyReason::OverlappingEvent:SceneFamilyReason::AdjacentEvent;
    return result;
}
namespace {
double transitionCost(const SceneOccurrence& a,const SceneOccurrence& b) {
    const auto& x=a.descriptor;const auto& y=b.descriptor;
    if(x.sourceId!=y.sourceId)return 1e9;
    const double time=std::abs(a.start-b.start);
    double cost=time*.15;
    if(x.sceneId&&y.sceneId&&x.sceneId!=y.sceneId)cost+=3;
    if(x.sequenceId&&y.sequenceId&&x.sequenceId!=y.sequenceId)cost+=12;
    cost+=scenePoseDistance(x,y).value_or(1.)*4;
    if(x.body.yawDegrees&&y.body.yawDegrees)cost+=angularDistanceDegrees(*x.body.yawDegrees,*y.body.yawDegrees)/90.;
    if(x.body.rollDegrees&&y.body.rollDegrees)cost+=angularDistanceDegrees(*x.body.rollDegrees,*y.body.rollDegrees)/90.;
    if(x.head.yawDegrees&&y.head.yawDegrees)cost+=angularDistanceDegrees(*x.head.yawDegrees,*y.head.yawDegrees)/180.;
    if(x.head.pitchDegrees&&y.head.pitchDegrees)cost+=angularDistanceDegrees(*x.head.pitchDegrees,*y.head.pitchDegrees)/180.;
    if(x.head.rollDegrees&&y.head.rollDegrees)cost+=angularDistanceDegrees(*x.head.rollDegrees,*y.head.rollDegrees)/180.;
    if(x.motion.gesture!=y.motion.gesture)cost+=1;
    if(x.posture!=y.posture)cost+=1;
    return cost;
}
}
SceneFamilySet buildSceneFamilies(std::span<const SceneOccurrence> occurrences,const std::function<bool()>& cancelled,
    const std::function<bool(const SceneOccurrence&,const SceneOccurrence&)>& copiedFootage) {
    SceneFamilySet result;result.membership.resize(occurrences.size());
    std::vector<std::size_t> ordered(occurrences.size());std::iota(ordered.begin(),ordered.end(),0);
    std::sort(ordered.begin(),ordered.end(),[&](auto a,auto b){return key(occurrences[a])<key(occurrences[b]);});
    // Only families from the same source/identity are scanned. Complete-link
    // admission prevents transitive overlap chains from joining distinct events.
    std::map<std::pair<std::string,std::size_t>,std::vector<std::size_t>> partitions;
    for (const auto i:ordered) {
        if(cancelled&&cancelled())return {{},{},true};
        const auto& occurrence=occurrences[i];
        auto& families=partitions[{occurrence.descriptor.sourceId,occurrence.descriptor.identityId}];
        std::optional<std::size_t> admitted;
        std::vector<SceneFamilyComparison> admissionProof;
        for (auto family:families) {
            const auto& members=result.families[family].members;
            std::vector<SceneFamilyComparison> proof;proof.reserve(members.size());
            for(auto j:members) {
                if(cancelled&&cancelled())return {{},{},true};
                const auto comparison=compareSceneFamilyOccurrences(occurrence,occurrences[j],copiedFootage);
                if(cancelled&&cancelled())return {{},{},true};
                if(!comparison.accepted)break;
                proof.push_back(comparison);
            }
            if(proof.size()==members.size()) {admitted=family;admissionProof=std::move(proof);break;}
        }
        if(!admitted) {
            admitted=result.families.size();families.push_back(*admitted);
            SceneFamily family;family.representative=i;family.representativeQuality=representativeQuality(occurrence);
            result.families.push_back(std::move(family));
        }
        auto& family=result.families[*admitted];result.membership[i]=*admitted;
        const double quality=representativeQuality(occurrence);
        const bool replace=quality>family.representativeQuality || (quality==family.representativeQuality
            && key(occurrence)<key(occurrences[family.representative]));
        if(family.members.empty()||replace) {
            family.representativeComparisons=std::move(admissionProof);
            family.representativeComparisons.push_back({true,SceneFamilyReason::Representative,1,-1,-1});
            family.representative=i;family.representativeQuality=quality;
        } else {
            const auto position=std::find(family.members.begin(),family.members.end(),family.representative)-family.members.begin();
            family.representativeComparisons.push_back(admissionProof[position]);
        }
        family.members.push_back(i);
    }
    for(auto& family:result.families) {
        if(cancelled&&cancelled())return {{},{},true};
        const auto& o=occurrences[family.representative];
        family.canonicalId=canonicalSceneFingerprint(o.descriptor.sourceId,o.descriptor.identityId,o.start,o.end);
    }
    // The fingerprint contains only identity and interval, while admission also
    // requires compatible pose, scene and context. Two rejected families can
    // therefore have the same fingerprint. Treat those IDs as unproven rather
    // than letting result deduplication merge independent or uncertain events.
    std::map<std::string,std::size_t> fingerprintCounts;
    for(const auto& family:result.families)
        if(!family.canonicalId.empty())++fingerprintCounts[family.canonicalId];
    for(auto& family:result.families) {
        if(cancelled&&cancelled())return {{},{},true};
        if(!family.canonicalId.empty()&&fingerprintCounts[family.canonicalId]>1)
            family.canonicalId.clear();
    }
    return result;
}
std::vector<std::size_t> continuityOrder(std::span<const SceneOccurrence> occurrences,const std::function<bool()>& cancelled) {
    std::vector<std::size_t> remaining(occurrences.size()),result;std::iota(remaining.begin(),remaining.end(),0);
    std::sort(remaining.begin(),remaining.end(),[&](auto a,auto b){return key(occurrences[a])<key(occurrences[b]);});
    result.reserve(remaining.size());
    while(!remaining.empty()) {
        if(cancelled&&cancelled())return {};
        auto best=remaining.begin();double bestCost=std::numeric_limits<double>::infinity();
        std::size_t checked=0;
        if(!result.empty())for(auto it=remaining.begin();it!=remaining.end();++it) {
            if((checked++%64)==0&&cancelled&&cancelled())return {};
            const double cost=transitionCost(occurrences[result.back()],occurrences[*it]);
            if(cost<bestCost) {bestCost=cost;best=it;}
        }
        result.push_back(*best);remaining.erase(best);
    }
    return result;
}
std::vector<std::size_t> pairedContinuityOrder(std::span<const std::array<SceneOccurrence,2>> occurrences,
    const std::function<bool()>& cancelled) {
    std::vector<std::size_t> remaining(occurrences.size()),result;
    std::iota(remaining.begin(),remaining.end(),0);
    std::sort(remaining.begin(),remaining.end(),[&](auto a,auto b) {
        return std::tuple{key(occurrences[a][0]),key(occurrences[a][1]),a}
            <std::tuple{key(occurrences[b][0]),key(occurrences[b][1]),b};
    });
    result.reserve(remaining.size());
    while(!remaining.empty()) {
        if(cancelled&&cancelled())return {};
        auto best=remaining.begin();double bestCost=std::numeric_limits<double>::infinity();
        std::size_t checked=0;
        if(!result.empty())for(auto it=remaining.begin();it!=remaining.end();++it) {
            if((checked++%64)==0&&cancelled&&cancelled())return {};
            const auto& previous=occurrences[result.back()];const auto& next=occurrences[*it];
            const double cost=transitionCost(previous[0],next[0])+transitionCost(previous[1],next[1]);
            if(cost<bestCost) {bestCost=cost;best=it;}
        }
        result.push_back(*best);remaining.erase(best);
    }
    return result;
}

}
