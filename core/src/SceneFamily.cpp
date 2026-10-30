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
    if (source.empty()||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>1e9) return {};
    const auto text=source+"\n"+std::to_string(identityId)+"\n"+std::to_string(std::llround(start*1e6))+"\n"+std::to_string(std::llround(end*1e6));
    std::uint64_t hash=14695981039346656037ULL;
    for (const auto c:text) {hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;}
    std::ostringstream output;output<<"scene-v1-"<<std::hex<<hash;return output.str();
}
double representativeQuality(const SceneOccurrence& o) {
    const auto& d=o.descriptor;
    return std::clamp(.55*d.evidenceQuality+.20*d.identityConfidence+.15*d.temporalCoverage
        +.10*std::clamp(o.matchSimilarity,0.,1.)-.15*d.cameraInfluence.value_or(0.),0.,1.);
}
namespace {
auto key(const SceneOccurrence& o) {return std::tuple{o.descriptor.sourceId,o.descriptor.identityId,o.start,o.end,o.windowIndex};}
bool compatible(const SceneOccurrence& a,const SceneOccurrence& b,
    const std::function<bool(const SceneOccurrence&,const SceneOccurrence&)>& copiedFootage) {
    if (a.descriptor.sourceId!=b.descriptor.sourceId || a.descriptor.identityId!=b.descriptor.identityId
        || !a.descriptor.identityId || a.end<=a.start || b.end<=b.start) return false;
    // Distant repeated edits require pixel-sequence proof. Keep pose-only
    // equivalence local to a shot; recurring actions remain independent.
    if(copiedFootage && copiedFootage(a,b))return true;
    if (a.descriptor.sceneId && b.descriptor.sceneId && a.descriptor.sceneId!=b.descriptor.sceneId) return false;
    if (a.descriptor.sequenceId && b.descriptor.sequenceId && a.descriptor.sequenceId!=b.descriptor.sequenceId) return false;
    const auto& ca=a.descriptor.sceneContext;const auto& cb=b.descriptor.sceneContext;
    if(!ca.empty()&&!cb.empty()) {
        if(ca.size()!=cb.size())return false;
        double dot=0,na=0,nb=0;
        for(std::size_t i=0;i<ca.size();++i) {
            if(!std::isfinite(ca[i])||!std::isfinite(cb[i]))return false;
            dot+=double(ca[i])*cb[i];na+=double(ca[i])*ca[i];nb+=double(cb[i])*cb[i];
        }
        if(na<=1e-12||nb<=1e-12||dot/std::sqrt(na*nb)<.9)return false;
    }
    const double intersection=std::max(0.,std::min(a.end,b.end)-std::max(a.start,b.start));
    const double overlap=intersection/std::min(a.end-a.start,b.end-b.start);
    const double gap=std::max(a.start,b.start)-std::min(a.end,b.end);
    if (overlap+1e-9<.60 && !(gap>=0 && gap<=.12)) return false;
    const auto distance=scenePoseDistance(a.descriptor,b.descriptor);
    if (!distance || *distance>(overlap>=.60?.16:.08)) return false;
    const auto& x=a.descriptor.motion;const auto& y=b.descriptor.motion;
    if (x.gesture!=GestureClass::Unknown && y.gesture!=GestureClass::Unknown && x.gesture!=y.gesture) return false;
    if (x.direction!=MovementDirection::Unknown && y.direction!=MovementDirection::Unknown && x.direction!=y.direction) return false;
    if (a.descriptor.posture!=ScenePosture::Unknown && b.descriptor.posture!=ScenePosture::Unknown
        && a.descriptor.posture!=b.descriptor.posture) return false;
    // Missing pose or unknown identity never proves two scenes are duplicates.
    return true;
}
double transitionCost(const SceneOccurrence& a,const SceneOccurrence& b) {
    const auto& x=a.descriptor;const auto& y=b.descriptor;
    if(x.sourceId!=y.sourceId)return 1e9;
    const double time=std::abs(a.start-b.start);
    double cost=time*.15;
    if(x.sceneId&&y.sceneId&&x.sceneId!=y.sceneId)cost+=3;
    if(x.sequenceId&&y.sequenceId&&x.sequenceId!=y.sequenceId)cost+=12;
    cost+=scenePoseDistance(x,y).value_or(1.)*4;
    if(x.body.yawDegrees&&y.body.yawDegrees)cost+=angularDistanceDegrees(*x.body.yawDegrees,*y.body.yawDegrees)/90.;
    if(x.head.yawDegrees&&y.head.yawDegrees)cost+=angularDistanceDegrees(*x.head.yawDegrees,*y.head.yawDegrees)/180.;
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
        for (auto family:families) {
            const auto& members=result.families[family].members;
            if(std::all_of(members.begin(),members.end(),[&](auto j){return compatible(occurrence,occurrences[j],copiedFootage);})) {
                admitted=family;break;
            }
        }
        if(!admitted) {
            admitted=result.families.size();families.push_back(*admitted);
            SceneFamily family;family.representative=i;family.representativeQuality=representativeQuality(occurrence);
            result.families.push_back(std::move(family));
        }
        auto& family=result.families[*admitted];family.members.push_back(i);result.membership[i]=*admitted;
        const double quality=representativeQuality(occurrence);
        if (quality>family.representativeQuality || (quality==family.representativeQuality
            && key(occurrence)<key(occurrences[family.representative]))) {
            family.representative=i;family.representativeQuality=quality;
        }
    }
    for(auto& family:result.families) {
        const auto& o=occurrences[family.representative];
        family.canonicalId=canonicalSceneFingerprint(o.descriptor.sourceId,o.descriptor.identityId,o.start,o.end);
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
        if(!result.empty())for(auto it=remaining.begin();it!=remaining.end();++it) {
            const double cost=transitionCost(occurrences[result.back()],occurrences[*it]);
            if(cost<bestCost) {bestCost=cost;best=it;}
        }
        result.push_back(*best);remaining.erase(best);
    }
    return result;
}
}
