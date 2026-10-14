#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace pfcore {
enum class HeadCovering { Unknown, None, Hood, Cap, Hat };
enum class PhonePosition { Unknown, Absent, Hand, Ear };
enum class HoodPhase { Unknown, PutOn, TakeOff };
enum class PhonePhase { Unknown, ToEar, FromEar };
struct VisualObjectSample {
    double time=0;
    std::string sourceKey;
    std::string subjectKey; // spatially associated track, NOT actor identity
    std::string modelKey;
    std::string frameKey; // actual decoded image fingerprint, not a frame number
    HeadCovering headCovering=HeadCovering::Unknown;
    PhonePosition phone=PhonePosition::Unknown;
};
struct VisualObjectHypothesis {
    HoodPhase hoodPhase=HoodPhase::Unknown;
    PhonePosition phone=PhonePosition::Unknown;
    std::size_t independentFrames=0;
    PhonePhase phonePhase=PhonePhase::Unknown;
};
// Temporal consistency of separately predicted object states. This does not
// certify a model's predictions or actor identity. A producer still needs
// validation on independent scenes before these hypotheses can be admitted.
inline VisualObjectHypothesis classifyVisualObjects(std::span<const VisualObjectSample> samples,
    double start,double end,bool continuityVerified,bool handMotionSupported)
{
    VisualObjectHypothesis result;
    if(!continuityVerified||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start)return result;
    std::vector<const VisualObjectSample*> selected;
    double previous=-1;
    for(const auto& sample:samples) {
        if(!std::isfinite(sample.time)||sample.time<0||sample.time<=previous)return {};
        previous=sample.time;
        if(sample.time>=start&&sample.time<end)selected.push_back(&sample);
    }
    if(selected.size()<3)return result;
    const auto& first=*selected.front();
    if(first.sourceKey.empty()||first.subjectKey.empty()||first.modelKey.empty())return result;
    std::unordered_set<std::string> frames;
    for(std::size_t i=0;i<selected.size();++i) {
        const auto& sample=*selected[i];
        if(sample.sourceKey!=first.sourceKey||sample.subjectKey!=first.subjectKey||sample.modelKey!=first.modelKey
            ||sample.frameKey.empty()||!frames.insert(sample.frameKey).second
            ||(i&&sample.time-selected[i-1]->time>.35))return {};
    }
    result.independentFrames=frames.size();
    // Phone location is a held object state, not evidence of looking at it,
    // speaking, or moving it. Unknown predictions still count in coverage.
    for(const auto position:{PhonePosition::Absent,PhonePosition::Hand,PhonePosition::Ear}) {
        const auto votes=std::count_if(selected.begin(),selected.end(),[&](const auto* sample){return sample->phone==position;});
        if(votes*5>=static_cast<std::ptrdiff_t>(selected.size()*4)) {result.phone=position;break;}
    }
    if(!handMotionSupported||selected.size()<4||selected.back()->time-first.time<.3)return result;
    // A held location is not an action. Require two independent observations
    // at each endpoint and a single visible hand <-> ear transition. Absent
    // or occluded phones cannot establish the other endpoint of this action.
    const auto phoneBefore=selected.front()->phone,phoneAfter=selected.back()->phone;
    if(((phoneBefore==PhonePosition::Hand&&phoneAfter==PhonePosition::Ear)
        ||(phoneBefore==PhonePosition::Ear&&phoneAfter==PhonePosition::Hand))
        &&selected[1]->phone==phoneBefore
        &&selected[selected.size()-2]->phone==phoneAfter) {
        bool transitioned=false,consistent=true;
        for(const auto* sample:selected) {
            if(sample->phone==phoneAfter)transitioned=true;
            else if(sample->phone!=phoneBefore||transitioned) {consistent=false;break;}
        }
        if(consistent)result.phonePhase=phoneBefore==PhonePosition::Hand
            ? PhonePhase::ToEar:PhonePhase::FromEar;
    }
    const auto state=[](HeadCovering covering) {
        switch(covering) {
            case HeadCovering::Hood:return 1;
            case HeadCovering::None:case HeadCovering::Cap:case HeadCovering::Hat:return 0;
            default:return -1;
        }
    };
    const int before=state(selected[0]->headCovering),after=state(selected.back()->headCovering);
    if(before<0||after<0||before==after||state(selected[1]->headCovering)!=before
        ||state(selected[selected.size()-2]->headCovering)!=after)return result;
    // Require one monotone transition. Do not bridge occluded or conflicting
    // object states, even if the first and last snapshots happen to agree.
    bool transitioned=false;
    for(const auto* sample:selected) {
        const int value=state(sample->headCovering);
        if(value<0)return result;
        if(value==after)transitioned=true;
        else if(transitioned)return result;
    }
    result.hoodPhase=before==0 ? HoodPhase::PutOn:HoodPhase::TakeOff;
    return result;
}
}
