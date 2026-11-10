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
struct Triplet {std::array<double,36> sums{};bool valid=false,bounded=false;};
struct Scene {const MotionWindow* sequence=nullptr;std::map<std::size_t,Track> tracks;std::vector<Triplet> triplets;};
std::vector<Triplet> prepareTriplets(const MotionWindow& window,const std::function<bool()>& cancelled) {
    const auto count=window.sceneSequenceTimes.size();
    std::vector<std::array<double,36>> frames(count);
    std::vector<bool> finite(count,true);
    std::vector<bool> bounded(count,true);
    for(std::size_t f=0;f<count;++f) {
        if((f%64)==0 && cancelled && cancelled())return {};
        for(std::size_t pixel=0;pixel<144;++pixel)for(std::size_t channel=0;channel<3;++channel) {
            const auto value=window.sceneSequence[f*432+pixel*3+channel];
            if(!std::isfinite(value)) {finite[f]=false;continue;}
            if(value<0 || value>1)bounded[f]=false;
            const auto block=(pixel/16/3)*4+(pixel%16/4);
            frames[f][block*3+channel]+=value;
        }
    }
    std::vector<Triplet> result(count>=3 ? count-2 : 0);
    for(std::size_t i=0;i<result.size();++i) {
        const auto first=window.sceneSequenceTimes[i+1]-window.sceneSequenceTimes[i];
        const auto second=window.sceneSequenceTimes[i+2]-window.sceneSequenceTimes[i+1];
        auto& triplet=result[i];
        triplet.valid=finite[i] && finite[i+1] && finite[i+2] && std::isfinite(first) && std::isfinite(second)
            && first>=.1 && first<=.4 && second>=.1 && second<=.4 && first+second>=.3;
        triplet.bounded=bounded[i] && bounded[i+1] && bounded[i+2];
        if(triplet.valid)for(std::size_t j=0;j<triplet.sums.size();++j)
            triplet.sums[j]=frames[i][j]+frames[i+1][j]+frames[i+2][j];
    }
    return result;
}
}

std::vector<RepeatedSceneIdentityLink> auditRepeatedSceneIdentityLinks(
    const std::vector<CharacterCandidate>& candidates,const std::vector<MotionWindow>& windows,
    const std::function<bool()>& cancelled,std::string_view source,
    const std::function<void(std::size_t,std::size_t)>& progress)
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
    for(auto& [key,scene]:scenes)if(scene.sequence) {
        scene.triplets=prepareTriplets(*scene.sequence,cancelled);
        const auto frameCount=scene.sequence->sceneSequenceTimes.size();
        if((cancelled && cancelled()) || scene.triplets.size()!=(frameCount>=3 ? frameCount-2 : 0))return {};
    }
    std::map<std::pair<std::size_t,std::size_t>,RepeatedSceneIdentityLink> links;
    std::size_t completedPairs=0;
    const auto totalPairs=scenes.size()*(scenes.size()-std::min<std::size_t>(1,scenes.size()))/2;
    if(progress)progress(0,totalPairs);
    for(auto a=scenes.begin();a!=scenes.end();++a) {
        if(cancelled && cancelled())return {};
        for(auto b=std::next(a);b!=scenes.end();++b) {
            if(cancelled && cancelled())return {};
            if(progress)progress(completedPairs++,totalPairs);
            if(!a->second.sequence || a->second.sequence->sceneSequenceTimes.size()<3)continue;
            if(a->first.first!=b->first.first || !b->second.sequence
               || b->second.sequence->sceneSequenceTimes.size()<3)continue;
            // Reject scene pairs with no eligible body pair before comparing pixels.
            struct BodyPair {const Track* left;const Track* right;double similarity;};
            std::vector<BodyPair> bodyPairs;
            for(const auto& [idA,ta]:a->second.tracks)for(const auto& [idB,tb]:b->second.tracks) {
                if(ta.owner==tb.owner || !ta.body || !tb.body)continue;
                const auto similarity=cosine(ta.body->appearanceEmbedding,tb.body->appearanceEmbedding);
                if(similarity>=.85)bodyPairs.push_back({&ta,&tb,similarity});
            }
            if(bodyPairs.empty())continue;
            const auto& x=*a->second.sequence;const auto& y=*b->second.sequence;
            if(std::abs(x.sceneStartSeconds-y.sceneStartSeconds)<4)continue;
            for(std::size_t i=0;i+2<x.sceneSequenceTimes.size();++i) {
            if((i%8)==0) {
                if(cancelled && cancelled())return {};
                if(progress)progress(completedPairs-1,totalPairs);
            }
            for(std::size_t j=0;j+2<y.sceneSequenceTimes.size();++j) {
                const auto& cx=a->second.triplets[i];const auto& cy=b->second.triplets[j];
                if(!cx.valid || !cy.valid)continue;
                // By the triangle inequality, the sum of absolute differences
                // between block sums cannot exceed the full pixel L1 error.
                // Reject only pairs which already exceed its existing limit.
                double lowerBound=0;
                for(std::size_t k=0;k<cx.sums.size();++k) {
                    lowerBound+=std::abs(cx.sums[k]-cy.sums[k]);
                    if(cx.bounded && cy.bounded && lowerBound>.08*(3*432)+.001)break;
                }
                if(cx.bounded && cy.bounded && lowerBound>.08*(3*432)+.001)continue;
                double error=0,variationX=0,variationY=0;
                for(std::size_t k=0;k<3*432;++k) {
                    const auto p=x.sceneSequence[i*432+k],q=y.sceneSequence[j*432+k];
                    if(!std::isfinite(p) || !std::isfinite(q)){error=INFINITY;break;}
                    error+=std::abs(p-q);
                    if(error>=.08*(3*432))break;
                    if(k>=432) {
                        variationX+=std::abs(p-x.sceneSequence[i*432+k-432]);
                        variationY+=std::abs(q-y.sceneSequence[j*432+k-432]);
                    }
                }
                error/=3*432;
                const double requiredVariation=error<.05 ? .004 : .01;
                if(error>=.08 || variationX/(2*432)<requiredVariation || variationY/(2*432)<requiredVariation)continue;
                for(const auto& pair:bodyPairs) {
                    const auto& ta=*pair.left;const auto& tb=*pair.right;
                    const auto body=pair.similarity;
                    if(body<(error>=.025 && error<.05 ? .90 : .85))continue;
                    std::set<double> leftTimes,rightTimes;
                    for(std::size_t k=0;k<3;++k) {
                        const auto* fa=closest(ta.frames,x.sceneSequenceTimes[i+k]);
                        const auto* fb=closest(tb.frames,y.sceneSequenceTimes[j+k]);
                        if(fa && fb && poseDistance(*fa,*fb)<(error<.05 ? .25 : .15)) {
                            leftTimes.insert(fa->timestampSeconds);rightTimes.insert(fb->timestampSeconds);
                        }
                    }
                    const std::size_t requiredPoses=error<.05 ? 2 : 3;
                    if(leftTimes.size()<requiredPoses || rightTimes.size()<requiredPoses)continue;
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
    if(progress)progress(totalPairs,totalPairs);
    return result;
}

std::size_t mergeRepeatedSceneIdentities(std::vector<CharacterCandidate>& candidates,
    const std::vector<MotionWindow>& windows,const std::function<bool()>& cancelled,std::string_view source,
    const std::function<void(std::size_t,std::size_t)>& progress)
{
    const auto links=auditRepeatedSceneIdentityLinks(candidates,windows,cancelled,source,progress);
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
