#include "pfcore/MovementClassifier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace pfcore {
namespace {
struct Point { double x,y; };
double length(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
std::optional<Point> point(const PoseFrame& f,std::size_t i) {
    if(i>=f.keypoints.size())return {};
    const auto& p=f.keypoints[i];
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.confidence)||p.confidence<.35)return {};
    return Point{p.x,p.y};
}
Point midpoint(Point a,Point b) {return {(a.x+b.x)/2,(a.y+b.y)/2};}
double median(std::vector<double> values) {
    if(values.empty())return 0;
    const auto mid=values.begin()+values.size()/2;
    std::nth_element(values.begin(),mid,values.end());return *mid;
}
struct Observation {
    double time;
    std::array<std::optional<Point>,17> local;
    std::array<std::optional<double>,2> reach;
    std::optional<Point> head;
    std::optional<double> earVisibility;
    std::optional<double> headVerticalBodyScale;
    std::optional<double> shoulderWidth;
    std::optional<double> torsoSize;
    std::optional<double> hipWidth;
    std::optional<double> legReference;
};
struct Reference {bool torso=false;bool ears=false;};
bool insideScene(const MotionWindow& window,double time) {
    return (!std::isfinite(window.sceneStartSeconds) || window.sceneStartSeconds<0
        || time>=window.sceneStartSeconds-1e-6)
        && (!std::isfinite(window.sceneEndSeconds) || window.sceneEndSeconds<0
            || time<window.sceneEndSeconds-1e-6);
}
bool insideSceneRange(const MotionWindow& window,double start,double end) {
    return insideScene(window,start)
        && (!std::isfinite(window.sceneEndSeconds) || window.sceneEndSeconds<0
            || end<=window.sceneEndSeconds+1e-6);
}
Reference reference(const MotionWindow& window,double start,double end) {
    std::size_t total=0,hips=0,ears=0;
    for(const auto& f:window.frames)if(insideScene(window,f.timestampSeconds)
        && f.timestampSeconds>=start-1e-6 && f.timestampSeconds<=end+1e-6) {
        ++total;hips+=bool(point(f,11)&&point(f,12));ears+=bool(point(f,3)&&point(f,4));
    }
    return {total>0&&hips*4>=total*3,total>0&&ears*4>=total*3};
}
std::optional<Observation> observe(const PoseFrame& f,Reference ref) {
    auto left=point(f,5),right=point(f,6);
    if(!left||!right)return {};
    const auto center=midpoint(*left,*right);
    const double shoulders=length(*left,*right);
    if(shoulders<1e-8)return {};
    double scale=shoulders;
    Point down{0,1};
    const auto hipLeft=point(f,11),hipRight=point(f,12);
    if(ref.torso) {
        if(!hipLeft||!hipRight)return {};
        const double torso=length(center,midpoint(*hipLeft,*hipRight));
        if(torso>shoulders*.25) {
            scale=torso;const auto hips=midpoint(*hipLeft,*hipRight);
            down={(hips.x-center.x)/torso,(hips.y-center.y)/torso};
        }
    }
    Observation result;result.time=f.timestampSeconds;
    std::vector<double> bones;
    for(const auto& [a,b]:{std::pair{5,7},std::pair{6,8},std::pair{7,9},std::pair{8,10},
        std::pair{11,13},std::pair{12,14},std::pair{13,15},std::pair{14,16}}) {
        const auto p=point(f,a),q=point(f,b);
        if(p&&q && length(*p,*q)>shoulders*.1)bones.push_back(length(*p,*q));
    }
    if(ref.torso && bones.size()>=4) {
        const double skeletalScale=median(bones);
        result.shoulderWidth=((right->x-left->x)*down.y-(right->y-left->y)*down.x)/skeletalScale;
        result.torsoSize=scale/skeletalScale;
        if(f.keypoints[11].confidence>=.75 && f.keypoints[12].confidence>=.75)
            result.hipWidth=((hipRight->x-hipLeft->x)*down.y-(hipRight->y-hipLeft->y)*down.x)/skeletalScale;
        int observedLegs=0;
        for(const auto& [a,b]:{std::pair{11,13},std::pair{12,14},std::pair{13,15},std::pair{14,16}})
            if(point(f,a)&&point(f,b)&&f.keypoints[a].confidence>=.65&&f.keypoints[b].confidence>=.65)++observedLegs;
        if(observedLegs>=3)result.legReference=1;
    }
    for(std::size_t i=0;i<17;++i)if(const auto p=point(f,i))
        result.local[i]=Point{((p->x-center.x)*down.y-(p->y-center.y)*down.x)/scale,
            ((p->x-center.x)*down.x+(p->y-center.y)*down.y)/scale};
    for(int arm=0;arm<2;++arm) {
        const auto s=point(f,5+arm),e=point(f,7+arm),w=point(f,9+arm);
        if(s&&e&&w) {
            const double bone=length(*s,*e)+length(*e,*w);
            if(bone>scale*.15)result.reach[arm]=length(*s,*w)/bone;
        }
    }
    const auto nose=point(f,0),eyeLeft=point(f,1),eyeRight=point(f,2);
    if(nose&&eyeLeft&&eyeRight) {
        // A yaw turn can hide one ear while both eyes remain measurable. Keep
        // this independent cue; do not treat arbitrary low confidence as an
        // occlusion or switch the coordinate reference in mid-trajectory.
        if(f.keypoints.size()>4) {
            const double leftConfidence=f.keypoints[3].confidence;
            const double rightConfidence=f.keypoints[4].confidence;
            if(std::isfinite(leftConfidence)&&std::isfinite(rightConfidence)) {
                if(leftConfidence>=.75&&rightConfidence>=.75)result.earVisibility=0;
                else if(leftConfidence>=.75&&rightConfidence<.15)result.earVisibility=1;
                else if(rightConfidence>=.75&&leftConfidence<.15)result.earVisibility=-1;
            }
        }
        const auto eyes=midpoint(*eyeLeft,*eyeRight);
        const auto earsLeft=point(f,3),earsRight=point(f,4);
        const double span=ref.ears ? (earsLeft&&earsRight ? length(*earsLeft,*earsRight):0) : length(*eyeLeft,*eyeRight)*2;
        if(span>scale*.035) {
            // Express nose displacement in the eye basis, so uniform camera
            // translation, scale and roll do not masquerade as a head action.
            const double eyeSpan=length(*eyeLeft,*eyeRight);
            if(eyeSpan>1e-8) {
                const double ex=(eyeRight->x-eyeLeft->x)/eyeSpan,ey=(eyeRight->y-eyeLeft->y)/eyeSpan;
                result.head=Point{((nose->x-eyes.x)*ex+(nose->y-eyes.y)*ey)/span,
                    (-(nose->x-eyes.x)*ey+(nose->y-eyes.y)*ex)/span};
                // Projected shoulder width shrinks during a yaw/body turn.
                // In a cropped shot it cannot supply independent evidence of
                // vertical head motion: dividing by it invents a compound
                // action. Use this cue only with an observed torso reference.
                if(ref.torso)
                    result.headVerticalBodyScale=(-(nose->x-eyes.x)*ey+(nose->y-eyes.y)*ex)/scale;
            }
        }
    }
    return result;
}
struct Trace {
    double delta=0,extent=0;int reversals=0;bool returning=false;double coverage=0;
};
template<class Read> std::optional<Trace> trace(const std::vector<Observation>& samples,Read read,double prominence) {
    std::vector<double> values;std::vector<double> times;
    for(const auto& s:samples)if(const auto value=read(s)) {values.push_back(*value);times.push_back(s.time);}
    if(values.size()<5 || values.size()*4<samples.size()*3)return {};
    for(std::size_t i=1;i<times.size();++i)if(times[i]-times[i-1]>.65)return {};
    auto filtered=values;
    for(std::size_t i=1;i+1<values.size();++i)filtered[i]=median({values[i-1],values[i],values[i+1]});
    // Robust endpoints and temporal smoothing reject one-frame detector jumps.
    const auto n=std::min<std::size_t>(3,std::max<std::size_t>(1,values.size()/4));
    const double first=median(std::vector<double>(filtered.begin(),filtered.begin()+n));
    const double last=median(std::vector<double>(filtered.end()-n,filtered.end()));
    const auto [low,high]=std::minmax_element(filtered.begin(),filtered.end());
    Trace result;result.delta=last-first;result.extent=*high-*low;
    result.coverage=double(values.size())/samples.size();
    double pivot=first,extreme=first;int sign=0;
    for(const double v:filtered) {
        if(sign==0) {
            if(std::abs(v-pivot)>=prominence) {sign=v>pivot ? 1:-1;extreme=v;}
        } else if((v-extreme)*sign>=0)extreme=v;
        else if((extreme-v)*sign>=prominence) {++result.reversals;sign=-sign;extreme=v;}
    }
    result.returning=result.reversals>=1 && result.extent>=prominence*1.5
        && std::abs(result.delta)<result.extent*.35;
    return result;
}
struct Decision {GestureClass type=GestureClass::Unknown;double strength=0;};
Decision armDecision(const std::vector<Observation>& samples,int arm) {
    const auto coordinate=[&](bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.local[9+arm])return {};
        return vertical ? s.local[9+arm]->y:s.local[9+arm]->x;
    },.18);};
    const auto x=coordinate(false),y=coordinate(true);
    if(!x||!y)return {};
    if(x->extent<.18 && y->extent<.18)return {GestureClass::Static,std::min(x->coverage,y->coverage)};
    if(y->extent>=.28 && y->extent>x->extent*1.25) {
        const double strength=std::min(y->coverage,std::clamp(y->extent/.5,0.,1.));
        if(y->returning)return {GestureClass::RaiseLower,strength};
        if(std::abs(y->delta)>=y->extent*.65)
            return {y->delta<0 ? GestureClass::Raise:GestureClass::Lower,strength};
    }
    if(x->extent>=.35 && x->extent>y->extent*1.25 && x->reversals>=2)
        return {GestureClass::Wave,std::min(x->coverage,std::clamp(x->extent/.6,0.,1.))};
    const auto reach=trace(samples,[&](const Observation& s){return s.reach[arm];},.12);
    if(reach && reach->extent>=.2 && x->extent>=.25 && x->extent>y->extent*1.25) {
        const double strength=std::min(reach->coverage,std::clamp(reach->extent/.35,0.,1.));
        if(reach->returning)return {GestureClass::ReachReturn,strength};
        if(std::abs(reach->delta)>=reach->extent*.65)
            return {reach->delta>0 ? GestureClass::Push:GestureClass::Pull,strength};
    }
    return {};
}
Decision torsoDecision(const std::vector<Observation>& samples) {
    const auto shoulders=trace(samples,[](const Observation& s){return s.shoulderWidth;},.15);
    const auto torsoSize=trace(samples,[](const Observation& s){return s.torsoSize;},.15);
    const auto hips=trace(samples,[](const Observation& s){return s.hipWidth;},.12);
    const auto legs=trace(samples,[](const Observation& s){return s.legReference;},.15);
    const auto hipAxis=[&](int joint) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.local[joint])return {};
        return s.local[joint]->x;
    },.15);};
    const auto hipLeft=hipAxis(11),hipRight=hipAxis(12);
    if(shoulders&&torsoSize&&hips&&hipLeft&&hipRight && std::abs(shoulders->delta)>=.65
        && ((std::abs(hips->delta)>=.22 && hips->delta*shoulders->delta>0) || legs)
        && torsoSize->extent<.2
        && hipLeft->extent<.2 && hipRight->extent<.2)
        return {GestureClass::Turn,std::min(shoulders->coverage,std::clamp(std::abs(shoulders->delta)/.9,0.,1.))};
    return {};
}
Decision classifyAction(const std::vector<Observation>& samples,bool headOnly) {
    // A verified trunk rotation also rotates the face. Classify the body first
    // instead of mistaking a whole-body turn for an isolated head action.
    if(!headOnly) {
        const auto torso=torsoDecision(samples);
        if(torso.type==GestureClass::Turn && torso.strength>=.65)return torso;
    }
    std::vector<Decision> actions;
    for(int arm=0;arm<(headOnly ? 0:2);++arm) {
        const auto d=armDecision(samples,arm);
        if(d.type!=GestureClass::Static && d.type!=GestureClass::Unknown && d.strength>=.65)actions.push_back(d);
    }
    if(!actions.empty()) {
        auto result=actions.front();
        for(const auto& action:actions) {
            if(action.type!=result.type)result.type=GestureClass::Mixed;
            result.strength=std::min(result.strength,action.strength);
        }
        return result;
    }
    const auto head=[&](bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.head)return {};
        return vertical ? s.head->y:s.head->x;
    },.075);};
    const auto hx=head(false),hy=head(true);
    if(hx&&hy) {
        const auto ear=trace(samples,[](const Observation& s){return s.earVisibility;},.5);
        // Foreshortened eye separation makes the vertical nose ratio grow
        // during yaw too. A sustained ear occlusion agreeing with lateral
        // nose displacement distinguishes that case from an isolated nod.
        // It also rejects a confidence drop with no actual facial motion.
        if(ear && std::abs(ear->delta)>=.9 && std::abs(hx->delta)>=.11
            && hx->delta*ear->delta>0 && std::abs(hx->delta)>=hx->extent*.65
            && hy->extent<=hx->extent*1.2 && ear->coverage>=.9) {
            const auto vertical=trace(samples,[](const Observation& s){return s.headVerticalBodyScale;},.015);
            const bool compound=vertical && vertical->extent>=.035
                && (vertical->returning || std::abs(vertical->delta)>=vertical->extent*.65);
            return {compound ? GestureClass::Mixed:GestureClass::HeadTurn,
                std::min({ear->coverage,hx->coverage,std::clamp(std::abs(hx->delta)/hx->extent,0.,1.)})};
        }
        if(hx->extent>=.16 && hx->extent>hy->extent*1.3)
            return {GestureClass::HeadTurn,std::min(hx->coverage,std::clamp(hx->extent/.24,0.,1.))};
        if(hy->extent>=.16 && hy->extent>hx->extent*1.3)
            return {hy->returning ? GestureClass::HeadNod:GestureClass::HeadTilt,
                std::min(hy->coverage,std::clamp(hy->extent/.24,0.,1.))};
        if(headOnly && hx->extent<.075 && hy->extent<.075)
            return {GestureClass::Static,std::min(hx->coverage,hy->coverage)};
    }
    if(headOnly)return {};
    const auto jointAxis=[&](int joint,bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.local[joint])return {};
        return vertical ? s.local[joint]->y:s.local[joint]->x;
    },.15);};
    const auto leftFoot=jointAxis(15,false),rightFoot=jointAxis(16,false);
    const auto leftKnee=jointAxis(13,true),rightKnee=jointAxis(14,true);
    if(leftFoot&&rightFoot&&leftKnee&&rightKnee && leftFoot->extent>=.3 && rightFoot->extent>=.3
        && (leftFoot->reversals+rightFoot->reversals)>=2
        && std::max(leftKnee->extent,rightKnee->extent)>=.12)
        return {GestureClass::Step,std::min(leftFoot->coverage,rightFoot->coverage)};
    int observed=0;double maximum=0;
    for(int joint=0;joint<17;++joint) {
        const auto x=jointAxis(joint,false),y=jointAxis(joint,true);
        if(x&&y) {++observed;maximum=std::max({maximum,x->extent,y->extent});}
    }
    if(observed>=13 && maximum<.12)return {GestureClass::Static,std::clamp(1.-maximum/.2,0.,1.)};
    return {};
}
} // namespace

MovementClassification MovementClassifier::classifyForGrouping(const MotionWindow& window,double start,double end,bool headOnly) const {
    MovementClassification result;
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return result;
    const auto ref=reference(window,start,end);
    std::vector<Observation> observations;std::size_t total=0;double previous=-INFINITY;
    for(const auto& f:window.frames)if(insideScene(window,f.timestampSeconds)
        && f.timestampSeconds>=start-1e-6 && f.timestampSeconds<=end+1e-6) {
        if(!std::isfinite(f.timestampSeconds)||f.timestampSeconds<=previous)return result;
        previous=f.timestampSeconds;++total;
        if(auto o=observe(f,ref))observations.push_back(*o);
    }
    if(observations.size()<5 || observations.size()*4<total*3
        || observations.back().time-observations.front().time<.3)return result;
    const auto decision=classifyAction(observations,headOnly);
    if(decision.strength<.65)return result;
    result.gesture=decision.type;result.gestureConfidence=decision.strength;
    // Screen displacement remains separate from the body-relative action.
    const auto displacement=classify(window,start,end);
    result.direction=displacement.direction;result.directionConfidence=displacement.directionConfidence;
    return result;
}

MovementClassification MovementClassifier::combineForGrouping(const MovementClassification& a,const MovementClassification& b) {
    MovementClassification result;
    if(a.gesture==GestureClass::Unknown || b.gesture==GestureClass::Unknown
        || a.gestureConfidence<.65 || b.gestureConfidence<.65)return result;
    result.gesture=a.gesture==b.gesture ? a.gesture:GestureClass::Mixed;
    result.gestureConfidence=std::min(a.gestureConfidence,b.gestureConfidence);
    result.direction=a.direction==b.direction ? a.direction:MovementDirection::Mixed;
    result.directionConfidence=std::min(a.directionConfidence,b.directionConfidence);
    return result;
}

std::string MovementClassifier::classifyPoseForGrouping(const MotionWindow& window,double start,double end,bool headOnly) const {
    const std::string fallback=headOnly ? "head_pose":"body_pose";
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return fallback;
    const auto ref=reference(window,start,end);
    std::vector<std::string> votes;std::size_t total=0;
    for(const auto& frame:window.frames)if(insideScene(window,frame.timestampSeconds)
        && frame.timestampSeconds>=start-1e-6 && frame.timestampSeconds<=end+1e-6) {
        ++total;const auto o=observe(frame,ref);if(!o)continue;
        std::string vote;
        if(headOnly) {
            if(o->head && point(frame,3) && point(frame,4)) {
                if(std::abs(o->head->x)<.08)vote="head_front";
                else if(std::abs(o->head->x)>.25)vote="head_turned";
            }
        } else {
            const auto raised=[&](int arm) {
                return o->local[9+arm]&&o->local[5+arm]
                    && o->local[9+arm]->y<o->local[5+arm]->y-.2;
            };
            const auto extended=[&](int arm) {
                return o->reach[arm] && *o->reach[arm]>.94 && o->local[9+arm]
                    && length(*o->local[9+arm],*o->local[5+arm])>.65;
            };
            if(raised(0)||raised(1))vote="arms_raised";
            else if(o->local[9]&&o->local[10]&&o->local[7]&&o->local[8]
                && length(*o->local[9],*o->local[8])<.3
                && length(*o->local[10],*o->local[7])<.3)vote="arms_folded";
            else if(extended(0)||extended(1))vote="arms_extended";
            else {
                const auto kneeCos=[&](int side)->std::optional<double> {
                    const auto h=o->local[11+side],k=o->local[13+side],a=o->local[15+side];
                    if(!h||!k||!a)return {};
                    const double denominator=length(*h,*k)*length(*a,*k);
                    if(denominator<.01)return {};
                    return ((h->x-k->x)*(a->x-k->x)+(h->y-k->y)*(a->y-k->y))/denominator;
                };
                const auto left=kneeCos(0),right=kneeCos(1);
                if(left&&right) {
                    if(*left>-.3 && *right>-.3)vote="legs_bent";
                    else if(*left<-.94 && *right<-.94)vote="legs_straight";
                }
            }
        }
        if(!vote.empty())votes.push_back(vote);
    }
    if(total<3 || votes.size()*4<total*3)return fallback;
    for(const auto& vote:votes)
        if(std::count(votes.begin(),votes.end(),vote)*5>=static_cast<long long>(total)*4)return vote;
    return fallback;
}
} // namespace pfcore
