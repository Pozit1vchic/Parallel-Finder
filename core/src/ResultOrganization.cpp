#include <pfcore/MotionRanker.hpp>
#include <pfcore/SceneFamily.hpp>
#include <algorithm>
#include <set>
#include <map>
#include <numeric>
#include <cmath>

namespace pfcore {
bool MotionRanker::organize(std::vector<MotionMatch>& matches,const std::vector<MotionWindow>& windows,
    const std::map<std::string,std::size_t>& selectedIdentities,const std::function<bool()>& cancelled) {
    std::vector<SceneOccurrence> occurrences;occurrences.reserve(matches.size()*2);
    auto working=matches;
    for(auto& match:working) {
        if(cancelled&&cancelled())return false;
        for (bool left:{true,false}) {
            const auto index=left?match.leftIndex:match.rightIndex;
            const double start=left?match.leftStartSeconds:match.rightStartSeconds;
            const double end=left?match.leftEndSeconds:match.rightEndSeconds;
            SceneDescriptor descriptor;
            if(index<windows.size()) {
                const auto& window=windows[index];
                auto id=window.trackId;
                if(const auto found=selectedIdentities.find(window.sourceId);found!=selectedIdentities.end())id=found->second;
                descriptor=describeScene(window,start,end,id);
            }
            occurrences.push_back({index,start,end,match.similarity,std::move(descriptor),match.headOnlyComparison});
        }
        const auto& a=occurrences[occurrences.size()-2].descriptor;
        const auto& b=occurrences.back().descriptor;
        match.evidenceQuality=comparisonEvidenceQuality(a,b,match.headOnlyComparison);
        match.poseSimilarity=comparisonPoseSimilarity(a,b,match.headOnlyComparison).value_or(-1.);
        match.bestMatchScore=match.similarity*(.55+.45*match.evidenceQuality)
            *((match.appearanceVerified||match.faceVerified)?1.:.85);
    }
    using Shot=std::pair<std::string,std::size_t>;
    std::map<Shot,const MotionWindow*> sequences;
    for(const auto& window:windows)if(window.hasSceneIndex&&!window.sceneSequence.empty())
        sequences.try_emplace(Shot{window.sourceId,window.sceneIndex},&window);
    using Range=std::tuple<std::string,std::size_t,double,double>;
    std::map<std::pair<Range,Range>,bool> copiedRanges;
    const auto copied=[&](const SceneOccurrence& a,const SceneOccurrence& b) {
        if(!a.descriptor.sceneId||!b.descriptor.sceneId||a.descriptor.sceneId==b.descriptor.sceneId)return false;
        const auto left=sequences.find({a.descriptor.sourceId,*a.descriptor.sceneId});
        const auto right=sequences.find({b.descriptor.sourceId,*b.descriptor.sceneId});
        if(left==sequences.end()||right==sequences.end())return false;
        const Range ka{a.descriptor.sourceId,*a.descriptor.sceneId,a.start,a.end};
        const Range kb{b.descriptor.sourceId,*b.descriptor.sceneId,b.start,b.end};
        const auto key=std::minmax(ka,kb);
        const auto found=copiedRanges.find(key);if(found!=copiedRanges.end())return found->second;
        const bool equal=MotionMatcher::sameFootageRange(*left->second,a.start,a.end,*right->second,b.start,b.end,cancelled);
        copiedRanges.emplace(key,equal);return equal;
    };
    const auto families=buildSceneFamilies(occurrences,cancelled,copied);
    if(families.cancelled)return false;
    for(std::size_t i=0;i<working.size();++i) {
        auto& match=working[i];
        const auto& a=families.families[families.membership[i*2]];
        const auto& b=families.families[families.membership[i*2+1]];
        const auto& ca=occurrences[a.representative];const auto& cb=occurrences[b.representative];
        match.leftSceneFamily=a.canonicalId;match.rightSceneFamily=b.canonicalId;
        match.leftFamilySize=a.members.size();match.rightFamilySize=b.members.size();
        match.leftCanonicalStart=ca.start;match.leftCanonicalEnd=ca.end;
        match.rightCanonicalStart=cb.start;match.rightCanonicalEnd=cb.end;
    }
    std::vector<std::size_t> order(working.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b) {
        if(working[a].bestMatchScore!=working[b].bestMatchScore)return working[a].bestMatchScore>working[b].bestMatchScore;
        return std::tie(working[a].leftSourceId,working[a].leftStartSeconds,working[a].rightSourceId,working[a].rightStartSeconds)
            <std::tie(working[b].leftSourceId,working[b].leftStartSeconds,working[b].rightSourceId,working[b].rightStartSeconds);
    });
    std::set<std::pair<std::string,std::string>> pairs;
    std::vector<MotionMatch> unique;std::vector<SceneOccurrence> continuity;
    for(auto i:order) {
        const auto& m=working[i];
        const auto pair=std::minmax(m.leftSceneFamily,m.rightSceneFamily);
        if(!pair.first.empty() && pair.first==pair.second)continue;
        // Without a valid canonical identity, retain the result rather than
        // collapsing unknown evidence under an empty fingerprint.
        if(!pair.first.empty()&&!pair.second.empty()&&!pairs.emplace(pair.first,pair.second).second)continue;
        unique.push_back(m);continuity.push_back(occurrences[i*2]);
    }
    // Bounded greedy diversity: retain all independent matches, but prefer a
    // different occurrence over another small variation of the same family.
    std::set<std::string> usedFamilies;
    std::vector<MotionMatch> diverse;std::vector<SceneOccurrence> diverseContinuity;
    while(!unique.empty()) {
        if(cancelled&&cancelled())return false;
        std::size_t best=0;double score=-1;
        for(std::size_t i=0;i<unique.size();++i) {
            const double penalty=.04*((!unique[i].leftSceneFamily.empty()&&usedFamilies.contains(unique[i].leftSceneFamily))
                +(!unique[i].rightSceneFamily.empty()&&usedFamilies.contains(unique[i].rightSceneFamily)));
            if(unique[i].bestMatchScore-penalty>score) {score=unique[i].bestMatchScore-penalty;best=i;}
        }
        auto selected=unique[best];selected.redundancyPenalty=selected.bestMatchScore-score;
        usedFamilies.insert(selected.leftSceneFamily);usedFamilies.insert(selected.rightSceneFamily);
        diverse.push_back(std::move(selected));diverseContinuity.push_back(std::move(continuity[best]));
        unique.erase(unique.begin()+best);continuity.erase(continuity.begin()+best);
    }
    unique=std::move(diverse);continuity=std::move(diverseContinuity);
    const auto sequence=continuityOrder(continuity,cancelled);
    if(cancelled&&cancelled())return false;
    for(std::size_t rank=0;rank<sequence.size();++rank)unique[sequence[rank]].continuityRank=rank;
    matches=std::move(unique);return true;
}
}
