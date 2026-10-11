#include "pfcore/SceneWindow.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

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
}
