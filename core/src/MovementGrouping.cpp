#include "pfcore/MovementClassifier.hpp"
#include "pfcore/HeadOrientation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>
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
        && f.timestampSeconds>=start-1e-6 && f.timestampSeconds<end-1e-6) {
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
    double first=0,last=0,largestChange=0;
};
template<class Read> std::optional<Trace> trace(const std::vector<Observation>& samples,Read read,double prominence) {
    std::vector<double> values;std::vector<double> times;
    for(const auto& s:samples)if(const auto value=read(s)) {values.push_back(*value);times.push_back(s.time);}
    if(values.size()<5 || values.size()*4<samples.size()*3)return {};
    for(std::size_t i=1;i<times.size();++i)if(times[i]-times[i-1]>.65)return {};
    auto filtered=values;
    for(std::size_t i=1;i+1<values.size();++i)filtered[i]=median({values[i-1],values[i],values[i+1]});
    // The clip boundary is not a privileged measurement. Leaving its raw
    // detector output in the range turns one bad first/last pose into a
    // sustained action even though every interior sample is stationary.
    // Use the same three-observation support at both boundaries; a real
    // action must still be present in the neighbouring observations.
    const auto [interiorLow,interiorHigh]=std::minmax_element(values.begin()+1,values.end()-1);
    const bool quietInterior=*interiorHigh-*interiorLow<prominence*.5;
    const auto supportedEndpoint=[&](double edge,double next,double following) {
        const double firstStep=next-edge,secondStep=following-next;
        // Keep a measured continuing trend. Replacing every endpoint by its
        // neighbour shortens real turns/gait phases and can turn a small but
        // ambiguous movement into a falsely certain static classification.
          const bool isolated = quietInterior && std::abs(firstStep) > std::max(.025,4*std::abs(secondStep));
          return isolated ? median({edge,next,following}) : edge;
    };
    filtered.front()=supportedEndpoint(values[0],values[1],values[2]);
    const auto tail=values.size();
    filtered.back()=supportedEndpoint(values[tail-1],values[tail-2],values[tail-3]);
    // Robust endpoints and temporal smoothing reject one-frame detector jumps.
    const auto n=std::min<std::size_t>(3,std::max<std::size_t>(1,values.size()/4));
    const double first=median(std::vector<double>(filtered.begin(),filtered.begin()+n));
    const double last=median(std::vector<double>(filtered.end()-n,filtered.end()));
    const auto [low,high]=std::minmax_element(filtered.begin(),filtered.end());
    Trace result;result.delta=last-first;result.extent=*high-*low;
    result.first=first;result.last=last;
    for(std::size_t i=1;i<filtered.size();++i)result.largestChange=std::max(result.largestChange,std::abs(filtered[i]-filtered[i-1]));
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
bool supportedHeadMotion(const Trace& motion) {
    // Pose landmarks jitter around a stationary face. A range-only test turns
    // that jitter into a head turn; require either a sustained endpoint shift
    // or one clearly supported out-and-back action.
    return (motion.reversals==0 && std::abs(motion.delta)>=std::max(.08,motion.extent*.45))
        || (motion.returning && motion.reversals==1 && motion.extent>=.22);
}
struct Decision {
    GestureClass type=GestureClass::Unknown;
    double strength=0;
    std::vector<GestureClass> actions;
    Decision()=default;
    Decision(GestureClass action,double confidence):type(action),strength(confidence){}
};
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
    const auto hipAxis=[&](int joint) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.local[joint])return {};
        return s.local[joint]->x;
    },.15);};
    const auto hipLeft=hipAxis(11),hipRight=hipAxis(12);
    const bool continuousReversal=shoulders&&hips && shoulders->coverage>=.9 && hips->coverage>=.9
        && shoulders->first*shoulders->last<0 && hips->first*hips->last<0
        && std::min(std::abs(shoulders->first),std::abs(shoulders->last))>=.35
        && std::min(std::abs(hips->first),std::abs(hips->last))>=.22
        && shoulders->delta*hips->delta>0
        && shoulders->largestChange<shoulders->extent*.6 && hips->largestChange<hips->extent*.6;
    if(shoulders&&torsoSize&&hips&&hipLeft&&hipRight && std::abs(shoulders->delta)>=.65
        && std::abs(hips->delta)>=.22 && hips->delta*shoulders->delta>0
        && torsoSize->extent<.2
        && ((hipLeft->extent<.2 && hipRight->extent<.2) || continuousReversal))
        return {GestureClass::Turn,std::min(shoulders->coverage,std::clamp(std::abs(shoulders->delta)/.9,0.,1.))};
    return {};
}
Decision classifyAction(const std::vector<Observation>& samples,bool headOnly,bool geometricHead=true) {
    // A verified trunk rotation also rotates the face. Classify the body first
    // instead of mistaking a whole-body turn for an isolated head action.
    std::vector<Decision> actions;
    Decision torso;
    if(!headOnly) {
        const auto jointAxis=[&](int joint,bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
            if(!s.local[joint])return {};
            return vertical ? s.local[joint]->y:s.local[joint]->x;
        },.15);};
        const auto leftKnee=jointAxis(13,true),rightKnee=jointAxis(14,true);
        // Torso bending/rotation moves both planted feet together in the
        // body-relative coordinates. Require opposing foot articulation.
        const auto alternatingFeet=[&](bool vertical,double extent,double prominence,double maxCorrelation) {
        const auto footTrace=[&](int joint){return trace(samples,[&](const Observation& sample)->std::optional<double>{
            if(!sample.local[joint])return {};
            return vertical ? sample.local[joint]->y:sample.local[joint]->x;
        },prominence);};
        const auto leftFoot=footTrace(15),rightFoot=footTrace(16);
        if(!leftFoot||!rightFoot || leftFoot->extent<extent || rightFoot->extent<extent
            || leftFoot->reversals+rightFoot->reversals<2)return 0.0;
        std::vector<std::pair<double,double>> feet;
        for(const auto& sample:samples)if(sample.local[15]&&sample.local[16])
            feet.emplace_back(vertical ? sample.local[15]->y:sample.local[15]->x,
                vertical ? sample.local[16]->y:sample.local[16]->x);
        double correlation=1;
        if(feet.size()>=5 && feet.size()*4>=samples.size()*3) {
            double leftMean=0,rightMean=0;
            for(const auto& foot:feet){leftMean+=foot.first;rightMean+=foot.second;}
            leftMean/=feet.size();rightMean/=feet.size();
            double covariance=0,leftVariance=0,rightVariance=0;
            for(const auto& foot:feet){const double a=foot.first-leftMean,b=foot.second-rightMean;
                covariance+=a*b;leftVariance+=a*a;rightVariance+=b*b;}
            const double denominator=std::sqrt(leftVariance*rightVariance);
            if(denominator>1e-12)correlation=covariance/denominator;
        }
        return correlation<=maxCorrelation ? std::min(leftFoot->coverage,rightFoot->coverage):0.0;
        };
        // In a front/back view the alternating steps project vertically,
        // while horizontal coordinates may move together with the torso.
        // Keep the horizontal rule and require stronger opposing evidence
        // on the vertical axis, with two measured direction reversals.
        const double walking=std::max(alternatingFeet(false,.3,.15,-.35),
            samples.size()>=8 ? alternatingFeet(true,.12,.075,-.5):0.0);
        if(walking>0 && leftKnee&&rightKnee && std::max(leftKnee->extent,rightKnee->extent)>=.12)
            actions.emplace_back(GestureClass::Step,walking);
        torso=torsoDecision(samples);
        if(torso.type==GestureClass::Turn && torso.strength>=.65)actions.push_back(torso);
    }
    for(int arm=0;arm<(headOnly ? 0:2);++arm) {
        const auto d=armDecision(samples,arm);
        if(d.type!=GestureClass::Static && d.type!=GestureClass::Unknown && d.strength>=.65)actions.push_back(d);
    }
    if(!actions.empty()) {
        // A simultaneous confident arm and head action must not be labelled
        // as the arm alone. A trunk turn already explains the rotating face;
        // do not count that same motion twice as an independent head action.
        if(!headOnly && geometricHead && torso.type!=GestureClass::Turn) {
            const auto head=classifyAction(samples,true);
            if(head.type!=GestureClass::Static && head.type!=GestureClass::Unknown && head.strength>=.65)
                actions.push_back(head);
        }
        auto result=actions.front();
        result.actions.clear();
        for(const auto& action:actions) {
            if(action.type!=result.type)result.type=GestureClass::Mixed;
            result.strength=std::min(result.strength,action.strength);
            if(!action.actions.empty())result.actions.insert(result.actions.end(),action.actions.begin(),action.actions.end());
            else if(action.type!=GestureClass::Mixed)result.actions.push_back(action.type);
        }
        std::sort(result.actions.begin(),result.actions.end());
        result.actions.erase(std::unique(result.actions.begin(),result.actions.end()),result.actions.end());
        return result;
    }
    if(!headOnly) {
        // A visible, substantial but unclassified hand action must not be
        // silently replaced by the accompanying head motion (for example,
        // shovelling). Cropped head-only comparisons remain independent.
        for(int arm=0;arm<2;++arm)if(armDecision(samples,arm).type==GestureClass::Unknown) {
            const auto axis=[&](bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
                if(!s.local[9+arm])return {};
                return vertical ? s.local[9+arm]->y:s.local[9+arm]->x;
            },.18);};
            const auto x=axis(false),y=axis(true);
            if(x&&y && std::max(x->extent,y->extent)>=.28)return {};
        }
    }
    const auto head=[&](bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.head)return {};
        return vertical ? s.head->y:s.head->x;
    },.075);};
    const auto hx=head(false),hy=head(true);
    if(hx&&hy) {
      if(geometricHead) {
        const auto ear=trace(samples,[](const Observation& s){return s.earVisibility;},.5);
        // Foreshortened eye separation makes the vertical nose ratio grow
        // during yaw too. A sustained ear occlusion agreeing with lateral
        // nose displacement distinguishes that case from an isolated nod.
        // It also rejects a confidence drop with no actual facial motion.
        if(ear && std::abs(ear->delta)>=.9 && std::abs(hx->delta)>=.11
            && hx->delta*ear->delta>0 && std::abs(hx->delta)>=hx->extent*.65
            && hy->extent<=hx->extent*1.2 && ear->coverage>=.9 && supportedHeadMotion(*hx)) {
            const auto vertical=trace(samples,[](const Observation& s){return s.headVerticalBodyScale;},.015);
            const bool compound=vertical && vertical->extent>=.035
                && (vertical->returning || std::abs(vertical->delta)>=vertical->extent*.65);
            Decision decision{compound ? GestureClass::Mixed:GestureClass::HeadTurn,
                std::min({ear->coverage,hx->coverage,std::clamp(std::abs(hx->delta)/hx->extent,0.,1.)})};
            if(compound) {
                decision.actions={GestureClass::HeadTurn,
                    vertical->returning ? GestureClass::HeadNod:GestureClass::HeadTilt};
                decision.strength=std::min(decision.strength,vertical->coverage);
            }
            return decision;
        }
        if(hx->extent>=.16 && hx->extent>hy->extent*1.3 && supportedHeadMotion(*hx))
            return {GestureClass::HeadTurn,std::min(hx->coverage,std::clamp(hx->extent/.24,0.,1.))};
        if(hy->extent>=.16 && hy->extent>hx->extent*1.3 && supportedHeadMotion(*hy))
            return {hy->returning ? GestureClass::HeadNod:GestureClass::HeadTilt,
                std::min(hy->coverage,std::clamp(hy->extent/.24,0.,1.))};
      }
        // Keep the stillness boundary aligned with the movement threshold:
        // otherwise the interval between normal landmark jitter and a real
        // head action becomes a large bucket of false "unknown" results.
        if(headOnly && hx->extent<(geometricHead?.08:.12) && hy->extent<(geometricHead?.08:.12)) {
            return {GestureClass::Static,std::min(hx->coverage,hy->coverage)};
        }
    }
    if(headOnly)return {};
    const auto jointAxis=[&](int joint,bool vertical) {return trace(samples,[&](const Observation& s)->std::optional<double> {
        if(!s.local[joint])return {};
        return vertical ? s.local[joint]->y:s.local[joint]->x;
    },.15);};
    int observed=0;double maximum=0;
    // Facial landmarks have a larger detector jitter envelope than the body
    // skeleton. Evaluate body stillness from shoulders down, then require a
    // separate stable head trace before calling the whole person static.
    for(int joint=5;joint<17;++joint) {
        const auto x=jointAxis(joint,false),y=jointAxis(joint,true);
        if(x&&y) {++observed;maximum=std::max({maximum,x->extent,y->extent});}
    }
    const bool stableHead=!geometricHead || (hx&&hy&&hx->extent<.12&&hy->extent<.12);
    if(observed>=8 && maximum<.12 && stableHead)
        return {GestureClass::Static,std::clamp(1.-maximum/.2,0.,1.)};
    return {};
}

std::vector<HeadOrientationSample> headSceneEvidence(const MotionWindow& window) {
    auto samples=window.headOrientations;
    std::erase_if(samples,[&](const auto& s){return std::isfinite(s.time) && !insideScene(window,s.time);});
    return samples;
}
std::vector<HeadOrientationSample> headMotionEvidence(const MotionWindow& window,double start,double end) {
    auto samples=headSceneEvidence(window);
    // Cropping must not lose its reliable shoulder reference just because
    // unrelated samples elsewhere in the original window were occluded.
    std::erase_if(samples,[&](const auto& s){return std::isfinite(s.time)
        && (s.time<start-1e-6 || s.time>=end-1e-6);});
    std::vector<std::optional<double>> roll(samples.size());std::size_t observed=0;
    for(std::size_t i=0;i<samples.size();++i) {
        const auto frame=std::lower_bound(window.frames.begin(),window.frames.end(),samples[i].time,
            [](const auto& f,double t){return f.timestampSeconds<t;});
        if(frame==window.frames.end() || std::abs(frame->timestampSeconds-samples[i].time)>1e-6
            || frame->keypoints.size()<7 || frame->keypoints[5].confidence<.65 || frame->keypoints[6].confidence<.65)continue;
        const auto left=point(*frame,5),right=point(*frame,6);
        if(left&&right && length(*left,*right)>1e-6) {
            roll[i]=std::atan2(right->y-left->y,right->x-left->x);++observed;
        }
    }
    // Camera/body roll is independent evidence; subtract it only when a
    // consistent measured shoulder reference exists across the interval.
    if(samples.empty() || observed*4<samples.size()*3)return samples;
    for(std::size_t i=0;i<samples.size();++i) {
        if(!roll[i]) {samples[i].observed=false;continue;}
        const double angle=*roll[i],c=std::cos(angle),s=std::sin(angle);
        const auto r=samples[i].rotation;
        for(int k=0;k<3;++k) {
            samples[i].rotation[k]=c*r[k]+s*r[3+k];
            samples[i].rotation[3+k]=-s*r[k]+c*r[3+k];
        }
    }
    return samples;
}
bool smallUncertainHeadDrift(const MotionWindow& window,double start,double end) {
    const auto samples=headMotionEvidence(window,start,end);
    std::vector<const HeadOrientationSample*> observed;
    for(const auto& sample:samples)if(sample.observed
        && std::all_of(sample.rotation.begin(),sample.rotation.end(),[](double value){return std::isfinite(value);}))
        observed.push_back(&sample);
    if(observed.size()<5 || observed.back()->time-observed.front()->time<.3)return false;
    double maximum=0;
    for(std::size_t i=0;i<observed.size();++i)for(std::size_t j=i+1;j<observed.size();++j) {
        const auto& a=observed[i]->rotation;const auto& b=observed[j]->rotation;
        double trace=0;
        for(int row=0;row<3;++row)for(int column=0;column<3;++column)
            trace+=a[row*3+column]*b[row*3+column];
        const double cosine=std::clamp((trace-1.)*.5,-1.,1.);
        maximum=std::max(maximum,std::acos(cosine)*180./std::numbers::pi);
    }
    // The orientation model deliberately abstains in the 8–12 degree band.
    // At grouping time, treat this sub-action drift as static only when the
    // tracked body is also stable; larger or unobserved motion stays unknown.
    return maximum>=8. && maximum<12.;
}
MovementClassification classifyHeadWithGeometricFallback(const MotionWindow& window,
    const std::vector<Observation>& observations,double start,double end) {
    auto result=classifyHeadOrientation(headMotionEvidence(window,start,end),start,end);
    if(result.gesture==GestureClass::Unknown && !window.headOrientations.empty()) {
        const bool anyObserved=std::any_of(window.headOrientations.begin(),window.headOrientations.end(),
            [&](const auto& sample){return sample.time>=start-1e-6 && sample.time<end-1e-6 && sample.observed;});
        if(!anyObserved)return result;
    }
    if(result.gesture==GestureClass::Unknown) {
        // The learned orientation has an intentional uncertainty band. When
        // the face and shoulders are still observable, geometric evidence can
        // resolve that band without weakening the learned model's thresholds.
        const auto geometric=classifyAction(observations,true,true);
        if(geometric.type!=GestureClass::Unknown) {
            result.gesture=geometric.type;
            result.gestureConfidence=geometric.strength;
            result.actions=geometric.actions;
        }
    }
    return result;
}
} // namespace

MovementClassification MovementClassifier::classifyGeometricForGrouping(const MotionWindow& window,double start,double end,bool headOnly) const {
    MovementClassification result;
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return result;
    MovementClassification learnedHead;
    const bool hasLearnedHead=headOnly&&!window.headOrientations.empty();
    if(headOnly && !window.headOrientations.empty()) {
        learnedHead=classifyHeadOrientation(headMotionEvidence(window,start,end),start,end);
        if(learnedHead.gesture!=GestureClass::Unknown) {
            const auto displacement=classify(window,start,end);
            learnedHead.direction=displacement.direction;
            learnedHead.directionConfidence=displacement.directionConfidence;
            return learnedHead;
        }
        const bool anyObserved=std::any_of(window.headOrientations.begin(),window.headOrientations.end(),
            [&](const auto& sample){return sample.time>=start-1e-6 && sample.time<end-1e-6 && sample.observed;});
        if(!anyObserved)return learnedHead;
    }
    const auto ref=reference(window,start,end);
    std::vector<Observation> observations;std::size_t total=0;double previous=-INFINITY;
    for(const auto& f:window.frames)if(insideScene(window,f.timestampSeconds)
        && f.timestampSeconds>=start-1e-6 && f.timestampSeconds<end-1e-6) {
        if(!std::isfinite(f.timestampSeconds)||f.timestampSeconds<=previous)return result;
        previous=f.timestampSeconds;++total;
        if(auto o=observe(f,ref))observations.push_back(*o);
    }
    if(observations.size()<5 || observations.size()*4<total*3
        || observations.back().time-observations.front().time<.3
        || observations.front().time-start>.35 || end-observations.back().time>.35) {
        if(hasLearnedHead) {
            const auto displacement=classify(window,start,end);
            learnedHead.direction=displacement.direction;
            learnedHead.directionConfidence=displacement.directionConfidence;
            return learnedHead;
        }
        return result;
    }
    if(headOnly) {
        const auto decision=classifyAction(observations,true,window.headOrientations.empty());
        result.gesture=decision.type;
        result.gestureConfidence=decision.strength;
        result.actions=decision.actions;
        if(result.gesture==GestureClass::Unknown && hasLearnedHead
            && smallUncertainHeadDrift(window,start,end)) {
            result.gesture=GestureClass::Static;
            result.gestureConfidence=learnedHead.gestureConfidence;
        }
        if(result.gesture==GestureClass::Unknown && hasLearnedHead)result=learnedHead;
        const auto displacement=classify(window,start,end);
        result.direction=displacement.direction;result.directionConfidence=displacement.directionConfidence;
        return result;
    }
    const auto decision=classifyAction(observations,headOnly,window.headOrientations.empty());
    if(decision.strength<.65) {
        // Cropped body coverage and a visible unclassified body action are
        // different. Retain independently measured head motion only when no
        // arm/hip/leg joint has enough temporal coverage for a body decision.
        bool bodyObservable=false;
        for(int joint=7;joint<17;++joint) {
            std::size_t visible=0;
            for(const auto& frame:window.frames)if(insideScene(window,frame.timestampSeconds)
                && frame.timestampSeconds>=start-1e-6 && frame.timestampSeconds<end-1e-6)
                visible+=bool(point(frame,joint));
            if(total && visible*4>=total*3) {bodyObservable=true;break;}
        }
        if(!headOnly && !bodyObservable && !window.headOrientations.empty()) {
            auto head=classifyHeadWithGeometricFallback(window,observations,start,end);
            if(head.gesture!=GestureClass::Unknown && head.gesture!=GestureClass::Static
                && head.gestureConfidence>=.65) {
                head.headOnlyEvidence=true;
                const auto displacement=classify(window,start,end);
                head.direction=displacement.direction;head.directionConfidence=displacement.directionConfidence;
                return head;
            }
        }
        return result;
    }
    result.gesture=decision.type;result.gestureConfidence=decision.strength;
    result.actions=decision.actions;
    if(!window.headOrientations.empty()) {
        const auto head=classifyHeadWithGeometricFallback(window,observations,start,end);
        // Body-static evidence excluded facial keypoints above. An unobserved
        // head therefore cannot certify that the complete person is static.
        if(result.gesture==GestureClass::Static && head.gesture==GestureClass::Unknown
            && !smallUncertainHeadDrift(window,start,end))
            return {};
        const bool geometricHead=result.gesture==GestureClass::HeadTurn
            || result.gesture==GestureClass::HeadTilt || result.gesture==GestureClass::HeadNod;
        if(geometricHead) {
            result.gesture=head.gesture;result.gestureConfidence=head.gestureConfidence;
            result.actions=head.actions;
        } else if(head.gesture!=GestureClass::Unknown && head.gesture!=GestureClass::Static
            && result.gesture!=GestureClass::Unknown && result.gesture!=GestureClass::Turn) {
            // A measured body turn can explain camera-space head rotation.
            // Other simultaneous body/head actions are independent.
            if(result.gesture!=GestureClass::Mixed && result.gesture!=GestureClass::Static)
                result.actions.push_back(result.gesture);
            if(!head.actions.empty())result.actions.insert(result.actions.end(),head.actions.begin(),head.actions.end());
            else if(head.gesture!=GestureClass::Mixed)result.actions.push_back(head.gesture);
            std::sort(result.actions.begin(),result.actions.end());
            result.actions.erase(std::unique(result.actions.begin(),result.actions.end()),result.actions.end());
            result.gesture=result.gesture==GestureClass::Static ? head.gesture:GestureClass::Mixed;
            result.gestureConfidence=std::min(result.gestureConfidence,head.gestureConfidence);
        }
    }
    // Screen displacement remains separate from the body-relative action.
    const auto displacement=classify(window,start,end);
    result.direction=displacement.direction;result.directionConfidence=displacement.directionConfidence;
    return result;
}

MovementClassification MovementClassifier::classifyForGrouping(const MotionWindow& window,double start,double end,bool headOnly) const {
    auto result=classifyGeometricForGrouping(window,start,end,headOnly);
    if(headOnly || !insideSceneRange(window,start,end))return result;
    std::optional<double> walking;
    if(!window.staticFrameSet)for(const auto& evidence:window.videoActions)
        if(const auto strength=measuredWalking(evidence,window.sourceId,start,end))walking=strength;
    if(!walking)return result;
    if(result.gesture==GestureClass::Unknown || result.gesture==GestureClass::Static) {
        result.gesture=GestureClass::Step;result.actions={GestureClass::Step};
        result.gestureConfidence=*walking;
    } else if(result.gesture!=GestureClass::Step) {
        if(result.gesture!=GestureClass::Mixed)result.actions.push_back(result.gesture);
        result.actions.push_back(GestureClass::Step);
        std::sort(result.actions.begin(),result.actions.end());
        result.actions.erase(std::unique(result.actions.begin(),result.actions.end()),result.actions.end());
        result.gesture=GestureClass::Mixed;result.gestureConfidence=std::min(result.gestureConfidence,*walking);
    }
    result.headOnlyEvidence=false;
    const auto displacement=classify(window,start,end);
    result.direction=displacement.direction;result.directionConfidence=displacement.directionConfidence;
    return result;
}

MovementClassification MovementClassifier::combineForGrouping(const MovementClassification& a,const MovementClassification& b) {
    MovementClassification result;
    if(a.gesture==GestureClass::Unknown || b.gesture==GestureClass::Unknown
        || !std::isfinite(a.gestureConfidence) || !std::isfinite(b.gestureConfidence)
        || a.gestureConfidence<.65 || b.gestureConfidence<.65)return result;
    result.direction=a.direction==b.direction ? a.direction:MovementDirection::Mixed;
    result.directionConfidence=std::min(a.directionConfidence,b.directionConfidence);
    if(a.gesture==GestureClass::Static && b.gesture==GestureClass::Static) {
        result.gesture=GestureClass::Static;
        result.gestureConfidence=std::min(a.gestureConfidence,b.gestureConfidence);
        return result;
    }
    // Keep only action facets independently supported on both sides. Different
    // views can expose different secondary motion (for example, a torso turn
    // alongside a clearly measured walk), so requiring identical top-level
    // labels would discard the shared action and sort a real parallel as
    // Unknown. Intersecting facets avoids inventing a composite from the two
    // sides while retaining the action they demonstrably share.
    auto leftActions=a.actions,rightActions=b.actions;
    const auto canonicalize=[](auto& actions) {
        std::sort(actions.begin(),actions.end());
        actions.erase(std::unique(actions.begin(),actions.end()),actions.end());
    };
    canonicalize(leftActions);canonicalize(rightActions);
    if(a.actions.empty() && a.gesture!=GestureClass::Mixed)leftActions.push_back(a.gesture);
    if(b.actions.empty() && b.gesture!=GestureClass::Mixed)rightActions.push_back(b.gesture);
    if(a.gesture==GestureClass::Mixed && leftActions.empty())return result;
    if(b.gesture==GestureClass::Mixed && rightActions.empty())return result;
    std::vector<GestureClass> sharedActions;
    std::set_intersection(leftActions.begin(),leftActions.end(),rightActions.begin(),rightActions.end(),
        std::back_inserter(sharedActions));
    if(sharedActions.empty()) {result.conflictingActions=true;return result;}
    result.actions=std::move(sharedActions);
    result.gesture=result.actions.size()==1 ? result.actions.front():GestureClass::Mixed;
    result.headOnlyEvidence=(a.headOnlyEvidence||b.headOnlyEvidence)
        && (result.gesture!=GestureClass::Mixed || !result.actions.empty());
    result.gestureConfidence=std::min(a.gestureConfidence,b.gestureConfidence);
    return result;
}

std::string MovementClassifier::classifyHeadTurnForGrouping(const MotionWindow& window,double start,double end) const {
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return "head_turn";
    return classifyHeadTurnPhase(headMotionEvidence(window,start,end),start,end);
}

std::string MovementClassifier::classifyPoseForGrouping(const MotionWindow& window,double start,double end,bool headOnly) const {
    const std::string fallback=headOnly ? "head_pose":"body_pose";
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return fallback;
    if(headOnly && !window.headOrientations.empty())
        return classifyHeadPoseOrientation(headSceneEvidence(window),start,end);
    const auto ref=reference(window,start,end);
    std::vector<std::string> votes;std::size_t total=0;double previous=-INFINITY;
    for(const auto& frame:window.frames)if(insideScene(window,frame.timestampSeconds)
        && frame.timestampSeconds>=start-1e-6 && frame.timestampSeconds<end-1e-6) {
        if(!std::isfinite(frame.timestampSeconds)||frame.timestampSeconds<=previous)return fallback;
        previous=frame.timestampSeconds;
        ++total;const auto o=observe(frame,ref);if(!o)continue;
        std::string vote;
        if(headOnly) {
            if(o->head && point(frame,3) && point(frame,4)) {
                if(std::abs(o->head->x)<.08)vote="head_front";
                else if(std::abs(o->head->x)>.25)vote="head_turned";
            } else if(o->head && o->earVisibility && std::abs(*o->earVisibility)>.9
                && o->head->x * *o->earVisibility>.25
                && frame.keypoints[0].confidence>=.75
                && frame.keypoints[1].confidence>=.75 && frame.keypoints[2].confidence>=.75) {
                // A strongly observed profile can hide its far ear. Require
                // agreement with facial displacement; an arbitrary missing
                // ear or a frontal face is not evidence of a turned head.
                vote="head_turned";
            }
        } else {
            const auto raised=[&](int arm) {
                return o->local[9+arm]&&o->local[5+arm]
                    && o->local[9+arm]->y<o->local[5+arm]->y-.2;
            };
            const auto extended=[&](int arm) {
                return o->reach[arm] && *o->reach[arm]>.94 && o->local[9+arm]
                    && length(*o->local[9+arm],*o->local[5+arm])>.65
                    // A straight elbow also occurs with the hand resting at
                    // the person's side. That is not an extended-arm pose:
                    // require observed reach away from the shoulder column
                    // or a hand near/above shoulder height. The body basis
                    // keeps this independent of camera translation/roll.
                    && (std::abs(o->local[9+arm]->x-o->local[5+arm]->x)>.35
                        || o->local[9+arm]->y<o->local[5+arm]->y+.35);
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

std::string MovementClassifier::classifyPostureForGrouping(const MotionWindow& window,double start,double end) const {
    if(!std::isfinite(start)||!std::isfinite(end)||end<start||!insideSceneRange(window,start,end))return "unknown";
    std::string semantic="unknown";
    for(const auto& evidence:window.videoActions) {
        const auto measured=measuredPosture(evidence,window.sourceId,start,end);
        if(measured=="unknown")continue;
        if(semantic!="unknown" && semantic!=measured)return "unknown";
        semantic=measured;
    }
    std::vector<std::string> votes;
    std::size_t sampled=0;
    std::size_t postureObserved=0;
    double firstPostureTime=std::numeric_limits<double>::infinity();
    double lastPostureTime=-std::numeric_limits<double>::infinity();
    for(const auto& frame:window.frames) {
        if(!insideScene(window,frame.timestampSeconds)||frame.timestampSeconds<start-1e-6
            ||frame.timestampSeconds>=end-1e-6)continue;
        ++sampled;
        const auto sl=point(frame,5),sr=point(frame,6),hl=point(frame,11),hr=point(frame,12);
        if(!sl||!sr||!hl||!hr)continue;
        const auto shoulder=midpoint(*sl,*sr),hip=midpoint(*hl,*hr);
        const double torso=length(shoulder,hip);
        if(torso<1e-6)continue;
        const double dx=std::abs(hip.x-shoulder.x)/torso,dy=std::abs(hip.y-shoulder.y)/torso;
        // A near-horizontal shoulder-to-hip axis is strong evidence of a
        // reclined/lying body, even when the lower body is cropped out.
        if(dx>.78 && dx>dy*1.35) {
            ++postureObserved;
            votes.emplace_back("seated_lying");
            firstPostureTime=std::min(firstPostureTime,frame.timestampSeconds);
            lastPostureTime=std::max(lastPostureTime,frame.timestampSeconds);
            continue;
        }
        if(dy<.68 || dy<dx*1.08)continue;
        const auto kneeAngleCos=[&](int side)->std::optional<double> {
            const auto h=point(frame,11+side),k=point(frame,13+side),a=point(frame,15+side);
            if(!h||!k||!a)return {};
            const double upper=length(*h,*k),lower=length(*a,*k);
            if(upper<torso*.18||lower<torso*.18)return {};
            return ((h->x-k->x)*(a->x-k->x)+(h->y-k->y)*(a->y-k->y))/(upper*lower);
        };
        const auto left=kneeAngleCos(0),right=kneeAngleCos(1);
        const auto thighHorizontal=[&](int side) {
            const auto h=point(frame,11+side),k=point(frame,13+side);
            return h&&k&&std::abs(h->x-k->x)>std::abs(h->y-k->y)*1.15;
        };
        const auto thighVertical=[&](int side) {
            const auto h=point(frame,11+side),k=point(frame,13+side);
            return h&&k&&std::abs(h->y-k->y)>std::abs(h->x-k->x)*1.2;
        };
        const bool bothLegs=left&&right;
        bool postureVote=false;
        if(bothLegs && (*left>-.72 || *right>-.72)
            && (thighHorizontal(0)||thighHorizontal(1))) {
            ++postureObserved;
            votes.emplace_back("seated_lying");
            postureVote=true;
        } else if(bothLegs && *left<-.88 && *right<-.88
            && thighVertical(0)&&thighVertical(1)
            && hip.y>shoulder.y+torso*.45) {
            ++postureObserved;
            votes.emplace_back("standing");
            postureVote=true;
        }
        if(postureVote) {
            firstPostureTime=std::min(firstPostureTime,frame.timestampSeconds);
            lastPostureTime=std::max(lastPostureTime,frame.timestampSeconds);
        }
    }
    // Missing lower-body keypoints are common in close-ups and occlusions.
    // They are not negative posture votes, but enough valid geometry must be
    // spread across the clip before a group can be assigned.
    const double requiredSpan=std::min(.3,(end-start)*.5);
    if(sampled<3||postureObserved<3||postureObserved*2<sampled||votes.size()*4<postureObserved*3
        ||!std::isfinite(firstPostureTime)||lastPostureTime-firstPostureTime<requiredSpan)return semantic;
    const auto seated=static_cast<std::size_t>(std::count(votes.begin(),votes.end(),"seated_lying"));
    const auto standing=static_cast<std::size_t>(std::count(votes.begin(),votes.end(),"standing"));
    std::string geometric="unknown";
    if(seated*5>=postureObserved*4)geometric="seated_lying";
    else if(standing*5>=postureObserved*4)geometric="standing";
    if(geometric!="unknown" && semantic!="unknown" && geometric!=semantic)return "unknown";
    return geometric!="unknown"?geometric:semantic;
}
} // namespace pfcore
