#include "pfcore/RepeatedSceneIdentity.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace pfcore {
namespace {
double cosine(const std::vector<float>& a,const std::vector<float>& b) {
    if(a.empty() || a.size()!=b.size())return -1;
    double dot=0,aa=0,bb=0;
    for(std::size_t i=0;i<a.size();++i) {
        if(!std::isfinite(a[i]) || !std::isfinite(b[i]))return -1;
        dot+=a[i]*b[i];aa+=a[i]*a[i];bb+=b[i]*b[i];
    }
    return aa>1e-12 && bb>1e-12 ? dot/std::sqrt(aa*bb) : -1;
}
double poseDistance(const PoseFrame& a,const PoseFrame& b) {
    std::vector<double> distances;
    double ax0=INFINITY,ax1=-INFINITY,ay0=INFINITY,ay1=-INFINITY;
    double bx0=INFINITY,bx1=-INFINITY,by0=INFINITY,by1=-INFINITY;
    for(std::size_t i=0;i<std::min(a.keypoints.size(),b.keypoints.size());++i) {
        const auto& x=a.keypoints[i];const auto& y=b.keypoints[i];
        if(x.confidence<.5 || y.confidence<.5 || !std::isfinite(x.x) || !std::isfinite(x.y)
           || !std::isfinite(y.x) || !std::isfinite(y.y))continue;
        distances.push_back(std::hypot(x.x-y.x,x.y-y.y));
        ax0=std::min(ax0,x.x);ax1=std::max(ax1,x.x);ay0=std::min(ay0,x.y);ay1=std::max(ay1,x.y);
        bx0=std::min(bx0,y.x);bx1=std::max(bx1,y.x);by0=std::min(by0,y.y);by1=std::max(by1,y.y);
    }
    if(distances.size()<3)return INFINITY;
    const auto spread=std::min(std::hypot(ax1-ax0,ay1-ay0),std::hypot(bx1-bx0,by1-by0));
    if(spread<20)return INFINITY;
    std::sort(distances.begin(),distances.end());
    const auto middle=distances.size()/2;
    const auto median=distances.size()%2 ? distances[middle] : (distances[middle-1]+distances[middle])*.5;
    return median/spread;
}
const PoseFrame* closest(const std::vector<const PoseFrame*>& frames,double time) {
    const PoseFrame* best=nullptr;double distance=.10000001;
    for(const auto* frame:frames)if(std::isfinite(frame->timestampSeconds)
        && std::abs(frame->timestampSeconds-time)<distance) {
        best=frame;distance=std::abs(frame->timestampSeconds-time);
    }
    return best;
}
struct Track {std::size_t id=0,owner=0;const MotionWindow* body=nullptr;std::vector<const PoseFrame*> frames;};
struct Scene {const MotionWindow* sequence=nullptr;std::map<std::size_t,Track> tracks;};
}

std::vector<RepeatedSceneIdentityLink> auditRepeatedSceneIdentityLinks(
    const std::vector<CharacterCandidate>& candidates,const std::vector<MotionWindow>& windows,
    const std::function<bool()>& cancelled,std::string_view source)
{
    std::map<std::size_t,std::size_t> owners;
    for(const auto& candidate:candidates)for(auto id:candidate.trackIds)
        if(!owners.emplace(id,candidate.id).second)return {}; // ambiguous ownership
    std::map<std::pair<std::string,std::size_t>,Scene> scenes;
    for(const auto& w:windows) {
        if((!source.empty() && w.sourceId!=source) || !w.hasSceneIndex || !owners.contains(w.trackId))continue;
        auto& scene=scenes[{w.sourceId,w.sceneIndex}];
        if(!w.sceneSequence.empty() && w.sceneSequence.size()==w.sceneSequenceTimes.size()*432)
            scene.sequence=&w;
        auto& track=scene.tracks[w.trackId];track.id=w.trackId;track.owner=owners[w.trackId];
        if(!track.body && cosine(w.appearanceEmbedding,w.appearanceEmbedding)>.99)track.body=&w;
        for(const auto& frame:w.frames)track.frames.push_back(&frame);
    }
    std::map<std::pair<std::size_t,std::size_t>,RepeatedSceneIdentityLink> links;
    for(auto a=scenes.begin();a!=scenes.end();++a) {
        if(cancelled && cancelled())return {};
        if(!a->second.sequence || a->second.sequence->sceneSequenceTimes.size()<3)continue;
        for(auto b=std::next(a);b!=scenes.end();++b) {
            if(cancelled && cancelled())return {};
            if(a->first.first!=b->first.first || !b->second.sequence
               || b->second.sequence->sceneSequenceTimes.size()<3)continue;
            const auto& x=*a->second.sequence;const auto& y=*b->second.sequence;
            if(std::abs(x.sceneStartSeconds-y.sceneStartSeconds)<4)continue;
            for(std::size_t i=0;i+2<x.sceneSequenceTimes.size();++i) {
            if((i%8)==0 && cancelled && cancelled())return {};
            for(std::size_t j=0;j+2<y.sceneSequenceTimes.size();++j) {
                const auto validTimes=[](const auto& times,std::size_t at) {
                    const auto first=times[at+1]-times[at],second=times[at+2]-times[at+1];
                    return std::isfinite(first) && std::isfinite(second)
                        && first>=.1 && first<=.4 && second>=.1 && second<=.4
                        && first+second>=.3;
                };
                if(!validTimes(x.sceneSequenceTimes,i) || !validTimes(y.sceneSequenceTimes,j))continue;
                double error=0,variationX=0,variationY=0;
                for(std::size_t k=0;k<3*432;++k) {
                    const auto p=x.sceneSequence[i*432+k],q=y.sceneSequence[j*432+k];
                    if(!std::isfinite(p) || !std::isfinite(q)){error=INFINITY;break;}
                    error+=std::abs(p-q);
                    if(error>=.025*(3*432))break;
                    if(k>=432) {
                        variationX+=std::abs(p-x.sceneSequence[i*432+k-432]);
                        variationY+=std::abs(q-y.sceneSequence[j*432+k-432]);
                    }
                }
                error/=3*432;
                if(error>=.025 || variationX/(2*432)<.005 || variationY/(2*432)<.005)continue;
                for(const auto& [unusedA,ta]:a->second.tracks)for(const auto& [unusedB,tb]:b->second.tracks) {
                    if(ta.owner==tb.owner || !ta.body || !tb.body)continue;
                    const auto body=cosine(ta.body->appearanceEmbedding,tb.body->appearanceEmbedding);
                    if(body<.85)continue;
                    std::set<double> leftTimes,rightTimes;
                    for(std::size_t k=0;k<3;++k) {
                        const auto* fa=closest(ta.frames,x.sceneSequenceTimes[i+k]);
                        const auto* fb=closest(tb.frames,y.sceneSequenceTimes[j+k]);
                        if(fa && fb && poseDistance(*fa,*fb)<.25) {
                            leftTimes.insert(fa->timestampSeconds);rightTimes.insert(fb->timestampSeconds);
                        }
                    }
                    if(leftTimes.size()<2 || rightTimes.size()<2)continue;
                    const auto key=std::minmax(ta.owner,tb.owner);
                    RepeatedSceneIdentityLink link{ta.owner,tb.owner,ta.id,tb.id,error,body};
                    auto [found,inserted]=links.try_emplace({key.first,key.second},link);
                    if(!inserted && error<found->second.sceneError)found->second=link;
                }
            }
            }
        }
    }
    std::vector<RepeatedSceneIdentityLink> result;
    std::map<std::size_t,std::size_t> degree;
    for(const auto& [key,link]:links) {++degree[key.first];++degree[key.second];}
    for(const auto& [key,link]:links) {
        if(cancelled && cancelled())return {};
        // Competing plausible matches are ambiguous. Do not resolve them by
        // iteration order, even when one has a slightly lower pixel error.
        if(degree[key.first]!=1 || degree[key.second]!=1)continue;
        bool conflict=false;
        for(const auto& left:windows) {
            if((!source.empty() && left.sourceId!=source) || !owners.contains(left.trackId) || owners[left.trackId]!=key.first)continue;
            for(const auto& right:windows) {
                if(!owners.contains(right.trackId) || owners[right.trackId]!=key.second
                   || left.sourceId!=right.sourceId)continue;
                if(left.faceConfidence>=.45 && right.faceConfidence>=.45
                   && cosine(left.faceEmbedding,left.faceEmbedding)>.99
                   && cosine(right.faceEmbedding,right.faceEmbedding)>.99
                   && cosine(left.faceEmbedding,right.faceEmbedding)<.30) {conflict=true;break;}
                if(left.hasSceneIndex && right.hasSceneIndex && left.sceneIndex==right.sceneIndex)
                    for(const auto& a:left.frames)for(const auto& b:right.frames)
                        if(std::abs(a.timestampSeconds-b.timestampSeconds)<1e-6 && poseDistance(a,b)>=.05)
                            conflict=true;
                if(conflict)break;
            }
            if(conflict)break;
        }
        if(!conflict)result.push_back(link);
    }
    return result;
}

std::size_t mergeRepeatedSceneIdentities(std::vector<CharacterCandidate>& candidates,
    const std::vector<MotionWindow>& windows,const std::function<bool()>& cancelled,std::string_view source)
{
    const auto links=auditRepeatedSceneIdentityLinks(candidates,windows,cancelled,source);
    if(cancelled && cancelled())return 0;
    std::set<std::size_t> removed;
    std::size_t merged=0;
    for(const auto& link:links) {
        auto left=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.id==link.leftCandidate;});
        auto right=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.id==link.rightCandidate;});
        if(left==candidates.end() || right==candidates.end() || left==right)continue;
        if(left->id>right->id)std::swap(left,right);
        if(removed.contains(left->id) || removed.contains(right->id))continue;
        const auto previousCount=left->observationCount;
        const auto total=previousCount+right->observationCount;
        if(total)left->evidenceQuality=(left->evidenceQuality*previousCount
            +right->evidenceQuality*right->observationCount)/total;
        left->observationCount=total;
        left->presenceSeconds+=right->presenceSeconds;
        left->distinctIdentity=left->distinctIdentity || right->distinctIdentity;
        if(right->prototype.faceEvidence>left->prototype.faceEvidence) {
            left->prototype.face=right->prototype.face;left->prototype.faceEvidence=right->prototype.faceEvidence;
        }
        if(right->prototype.bodyEvidence>left->prototype.bodyEvidence) {
            left->prototype.body=right->prototype.body;left->prototype.bodyEvidence=right->prototype.bodyEvidence;
        }
        left->trackIds.insert(left->trackIds.end(),right->trackIds.begin(),right->trackIds.end());
        std::sort(left->trackIds.begin(),left->trackIds.end());
        left->appearances.insert(left->appearances.end(),right->appearances.begin(),right->appearances.end());
        removed.insert(right->id);++merged;
    }
    std::erase_if(candidates,[&](const auto& candidate){return removed.contains(candidate.id);});
    return merged;
}
}
