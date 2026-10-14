#include "pfcore/SceneWindow.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <map>
#include <limits>

namespace pfcore {
std::vector<SceneWindowPart> splitMotionWindowAtScenes(const MotionWindow& window,
    std::span<const ObservedSceneRange> scenes)
{
    if(scenes.empty() || !std::isfinite(window.sceneStartSeconds)
        || !std::isfinite(window.sceneEndSeconds) || window.sceneStartSeconds<0
        || window.sceneEndSeconds<=window.sceneStartSeconds)
        throw std::invalid_argument("Scene partition requires known original shot bounds");
    std::set<std::size_t> ids;
    double previous=window.sceneStartSeconds;
    for(const auto& scene:scenes) {
        if(!std::isfinite(scene.start) || !std::isfinite(scene.end)
            || scene.end<=scene.start || std::abs(scene.start-previous)>1e-6
            || !ids.insert(scene.sceneIndex).second)
            throw std::invalid_argument("Scene partition must be contiguous with unique IDs");
        previous=scene.end;
    }
    if(std::abs(previous-window.sceneEndSeconds)>1e-6)
        throw std::invalid_argument("Scene partition must cover the original shot");
    double previousTime=-INFINITY;
    for(const auto& frame:window.frames) {
        if(!std::isfinite(frame.timestampSeconds) || frame.timestampSeconds<=previousTime
            || frame.timestampSeconds<window.sceneStartSeconds
            || frame.timestampSeconds>=window.sceneEndSeconds)
            throw std::invalid_argument("Cached poses must be ordered inside the original shot");
        previousTime=frame.timestampSeconds;
    }
    std::vector<SceneWindowPart> result;
    const bool changed=scenes.size()>1;
    for(const auto& scene:scenes) {
        const auto first=std::lower_bound(window.frames.begin(),window.frames.end(),scene.start,
            [](const auto& frame,double t){return frame.timestampSeconds<t;});
        const auto last=std::lower_bound(first,window.frames.end(),scene.end,
            [](const auto& frame,double t){return frame.timestampSeconds<t;});
        if(first==last)continue;
        SceneWindowPart part{window,changed};
        auto& split=part.window;
        split.frames.assign(first,last);
        split.sceneStartSeconds=scene.start;
        split.sceneEndSeconds=scene.end;
        split.sceneIndex=scene.sceneIndex;
        split.hasSceneIndex=true;
        if(changed) {
            split.appearanceEmbedding.clear();split.appearanceConfidence=0;
            split.faceEmbedding.clear();split.faceConfidence=0;
            split.sceneContext.clear();split.sceneView.clear();split.sceneViewSampled=false;
            split.measuredFaces.clear();
            split.directFaces.clear();split.directFaceModel.clear();
            split.videoActions.clear();
            std::erase_if(split.headOrientations,[&](const auto& sample) {
                return !std::isfinite(sample.time) || sample.time<scene.start || sample.time>=scene.end;
            });
            split.sceneSequence.clear();split.sceneSequenceTimes.clear();
            if(window.sceneSequence.size()==window.sceneSequenceTimes.size()*432) {
                for(std::size_t i=0;i<window.sceneSequenceTimes.size();++i) {
                    const auto time=window.sceneSequenceTimes[i];
                    if(!std::isfinite(time) || time<scene.start || time>=scene.end)continue;
                    split.sceneSequenceTimes.push_back(time);
                    split.sceneSequence.insert(split.sceneSequence.end(),
                        window.sceneSequence.begin()+i*432,window.sceneSequence.begin()+(i+1)*432);
                }
            }
        }
        result.push_back(std::move(part));
    }
    return result;
}

std::vector<SceneWindowPart> splitSourceWindowsAtCuts(std::span<const MotionWindow> windows,
    std::span<const double> confirmedCuts)
{
    if(!std::all_of(confirmedCuts.begin(),confirmedCuts.end(),[](double time) {
        return std::isfinite(time)&&time>=0;
    }) || !std::is_sorted(confirmedCuts.begin(),confirmedCuts.end()))
        throw std::invalid_argument("Confirmed cuts must be finite and ordered");
    if(windows.empty())return {};
    const auto& source=windows.front().sourceId;
    std::size_t nextId=0;
    for(const auto& window:windows) {
        if(window.sourceId!=source)
            throw std::invalid_argument("Cut partition requires one source");
        if(window.hasSceneIndex) {
            if(window.sceneIndex==std::numeric_limits<std::size_t>::max())
                throw std::invalid_argument("Scene ID space exhausted");
            nextId=std::max(nextId,window.sceneIndex+1);
        }
    }
    using Range=std::pair<double,double>;
    std::map<Range,std::vector<ObservedSceneRange>> partitions;
    // Allocate in source-time order, independent of chunk enumeration.
    for(const auto& window:windows) {
        if(!std::isfinite(window.sceneStartSeconds)||!std::isfinite(window.sceneEndSeconds)
            ||window.sceneStartSeconds<0||window.sceneEndSeconds<=window.sceneStartSeconds)
            throw std::invalid_argument("Cut partition requires known shot bounds");
        const auto first=std::upper_bound(confirmedCuts.begin(),confirmedCuts.end(),window.sceneStartSeconds);
        if(first!=confirmedCuts.end()&&*first<window.sceneEndSeconds)
            partitions.try_emplace(Range{window.sceneStartSeconds,window.sceneEndSeconds});
    }
    for(auto& [range,partition]:partitions) {
        double start=range.first;
        for(auto cut=std::upper_bound(confirmedCuts.begin(),confirmedCuts.end(),start);
            cut!=confirmedCuts.end()&&*cut<range.second;++cut) {
            if(*cut==start)continue;
            if(nextId==std::numeric_limits<std::size_t>::max())
                throw std::invalid_argument("Scene ID space exhausted");
            partition.push_back({start,*cut,nextId++});start=*cut;
        }
        if(nextId==std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("Scene ID space exhausted");
        partition.push_back({start,range.second,nextId++});
    }
    std::vector<SceneWindowPart> result;
    for(const auto& window:windows) {
        const auto found=partitions.find({window.sceneStartSeconds,window.sceneEndSeconds});
        if(found==partitions.end()) {result.push_back({window,false});continue;}
        auto parts=splitMotionWindowAtScenes(window,found->second);
        for(auto& part:parts)result.push_back(std::move(part));
    }
    return result;
}
}
