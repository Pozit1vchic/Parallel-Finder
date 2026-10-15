#include "pfcore/HeadOrientation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <vector>

namespace pfcore {
namespace {
using Matrix = std::array<double, 9>;
using Vector = std::array<double, 3>;

bool validRotation(const Matrix& r) {
    for (const auto v : r) if (!std::isfinite(v)) return false;
    for (int a=0; a<3; ++a) for (int b=0; b<3; ++b) {
        double dot=0;
        for (int k=0; k<3; ++k) dot+=r[a*3+k]*r[b*3+k];
        if (std::abs(dot-(a==b ? 1. : 0.))>.002) return false;
    }
    const double determinant=r[0]*(r[4]*r[8]-r[5]*r[7])
        -r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    return std::abs(determinant-1.)<=.002;
}

// Spatial increments use the camera axes, independently of the starting pose.
Vector increment(const Matrix& current, const Matrix& previous) {
    Matrix relative{};
    for (int a=0; a<3; ++a) for (int b=0; b<3; ++b)
        for (int k=0; k<3; ++k) relative[a*3+b]+=current[a*3+k]*previous[b*3+k];
    const double angle=std::acos(std::clamp((relative[0]+relative[4]+relative[8]-1.)/2.,-1.,1.));
    if (angle>70.*std::numbers::pi/180.) return {std::numeric_limits<double>::quiet_NaN(),0,0};
    if (angle<1e-7) return {};
    // Samples making a half-turn in one step are rejected by the caller;
    // their logarithm has an ambiguous axis and cannot establish an action.
    const double factor=angle/(2.*std::sin(angle))*180./std::numbers::pi;
    return {(relative[7]-relative[5])*factor,
            (relative[2]-relative[6])*factor,
            (relative[3]-relative[1])*factor};
}

double median(double a,double b,double c) {
    return a+b+c-std::min({a,b,c})-std::max({a,b,c});
}
} // namespace

MovementClassification classifyHeadOrientation(std::span<const HeadOrientationSample> input,
    double start,double end) {
    MovementClassification result;
    if (!std::isfinite(start)||!std::isfinite(end)||end-start<.3) return result;
    std::vector<const HeadOrientationSample*> valid;
    std::size_t total=0;
    double previousTime=-INFINITY;
    for (const auto& sample : input) {
        // Malformed ordering is rejected, even for an unobserved sample.
        if (!std::isfinite(sample.time)||sample.time<=previousTime) return result;
        previousTime=sample.time;
        if (sample.time<start-1e-6 || sample.time>=end-1e-6) continue;
        ++total;
        if (sample.observed && validRotation(sample.rotation)) valid.push_back(&sample);
    }
    if (valid.size()<5 || valid.size()*4<total*3) return result;
    if (valid.front()->time-start>.35 || end-valid.back()->time>.35
        || valid.back()->time-valid.front()->time<.3) return result;
    std::vector<Vector> path(1);
    for (std::size_t i=1;i<valid.size();++i) {
        const double dt=valid[i]->time-valid[i-1]->time;
        if (dt>.65) return result;
        const auto change=increment(valid[i]->rotation,valid[i-1]->rotation);
        double magnitude=0;
        for (const auto v : change) {
            if (!std::isfinite(v)) return result;
            magnitude+=v*v;
        }
        magnitude=std::sqrt(magnitude);
        // Refuse a crop/person switch instead of filtering it into a turn.
        if (magnitude>70. || magnitude/dt>500.) return result;
        Vector next=path.back();
        for (int axis=0;axis<3;++axis) next[axis]+=change[axis];
        path.push_back(next);
    }
    auto smooth=path;
    for (std::size_t i=1;i+1<path.size();++i)
        for (int axis=0;axis<3;++axis)
            smooth[i][axis]=median(path[i-1][axis],path[i][axis],path[i+1][axis]);
    // A crop at either end can be less stable than the surrounding poses.
    // Apply temporal support there too: otherwise an isolated boundary
    // estimate survives smoothing and manufactures a turn or compound action.
    for (int axis=0;axis<3;++axis) {
        double interiorLow=path[1][axis],interiorHigh=interiorLow;
        for(std::size_t i=1;i+1<path.size();++i){interiorLow=std::min(interiorLow,path[i][axis]);interiorHigh=std::max(interiorHigh,path[i][axis]);}
        const auto supportedEndpoint=[&](double edge,double next,double following) {
            const double firstStep=next-edge,secondStep=following-next;
            const bool isolated=interiorHigh-interiorLow<3 && std::abs(firstStep)>std::max(1.0,4*std::abs(secondStep));
            return isolated ? median(edge,next,following) : edge;
        };
        smooth.front()[axis]=supportedEndpoint(path[0][axis],path[1][axis],path[2][axis]);
        const auto n=path.size();
        smooth.back()[axis]=supportedEndpoint(path[n-1][axis],path[n-2][axis],path[n-3][axis]);
    }
    Vector extent{},delta{};
    for (int axis=0;axis<3;++axis) {
        double low=smooth.front()[axis],high=low;
        for (const auto& point : smooth) {
            low=std::min(low,point[axis]);high=std::max(high,point[axis]);
        }
        extent[axis]=high-low;
        delta[axis]=median(smooth[0][axis],smooth[1][axis],smooth[2][axis]);
        const auto n=smooth.size();
        delta[axis]=median(smooth[n-1][axis],smooth[n-2][axis],smooth[n-3][axis])-delta[axis];
    }
    const double yaw=extent[1],tilt=std::max(extent[0],extent[2]);
    const double maximum=std::max(yaw,tilt);
    if (maximum<8.) result.gesture=GestureClass::Static;
    else if (maximum<12.) return result; // explicit uncertainty band
    else if (std::min(yaw,tilt)>=12.) {
        result.gesture=GestureClass::Mixed;
        const bool nod=extent[0]>=12. && extent[0]>extent[2]*1.5
            && std::abs(delta[0])<extent[0]*.35;
        result.actions={GestureClass::HeadTurn,nod ? GestureClass::HeadNod:GestureClass::HeadTilt};
    }
    else if (yaw>=12. && yaw>tilt*1.5) result.gesture=GestureClass::HeadTurn;
    else if (tilt>=12. && tilt>yaw*1.5) {
        const bool nod=extent[0]>=12. && extent[0]>extent[2]*1.5
            && std::abs(delta[0])<extent[0]*.35;
        result.gesture=nod ? GestureClass::HeadNod:GestureClass::HeadTilt;
    } else return result;
    result.gestureConfidence=static_cast<double>(valid.size())/total;
    return result;
}

std::string classifyHeadTurnPhase(std::span<const HeadOrientationSample> input,double start,double end) {
    constexpr auto fallback="head_turn";
    const auto movement=classifyHeadOrientation(input,start,end);
    if(movement.gesture!=GestureClass::HeadTurn
        && std::find(movement.actions.begin(),movement.actions.end(),GestureClass::HeadTurn)==movement.actions.end())return fallback;
    std::vector<double> yaw;
    for(const auto& sample:input) {
        if(sample.time<start-1e-6 || sample.time>=end-1e-6 || !sample.observed
            || !validRotation(sample.rotation))continue;
        const auto& r=sample.rotation;
        // A steeply pitched head cannot provide reliable camera-relative yaw.
        const double pitch=std::atan2(-r[5],std::hypot(r[2],r[8]))*180./std::numbers::pi;
        if(std::abs(pitch)>35.)return fallback;
        const double angle=std::atan2(r[2],r[8])*180./std::numbers::pi;
        if(std::abs(angle)>100.)return fallback;
        yaw.push_back(std::abs(angle));
    }
    if(yaw.size()<5)return fallback;
    const double first=median(yaw[0],yaw[1],yaw[2]);
    const auto n=yaw.size();
    const double last=median(yaw[n-1],yaw[n-2],yaw[n-3]);
    // Require a frontal endpoint and a distinctly turned endpoint. Small
    // changes around a profile are still a turn, not a return to the camera.
    if(first<=20. && last>=35. && last-first>=20.)return "head_turn_away";
    if(first>=35. && last<=20. && first-last>=20.)return "head_turn_back";
    return fallback;
}

std::string classifyHeadPoseOrientation(std::span<const HeadOrientationSample> input,double start,double end) {
    constexpr auto unknown="head_pose";
    if(!std::isfinite(start)||!std::isfinite(end)||end-start<.3)return unknown;
    std::size_t total=0,valid=0,front=0,turned=0;
    double previous=-INFINITY,firstValid=-1,lastValid=-1;
    for(const auto& sample:input) {
        if(!std::isfinite(sample.time)||sample.time<=previous)return unknown;
        previous=sample.time;
        if(sample.time<start-1e-6 || sample.time>=end-1e-6)continue;
        ++total;
        if(!sample.observed || !validRotation(sample.rotation))continue;
        if(valid && sample.time-lastValid>.65)return unknown;
        if(!valid)firstValid=sample.time;
        lastValid=sample.time;++valid;
        const auto& r=sample.rotation;
        const double yaw=std::abs(std::atan2(r[2],r[8])*180./std::numbers::pi);
        const double pitch=std::abs(std::atan2(-r[5],std::hypot(r[2],r[8]))*180./std::numbers::pi);
        const double roll=std::abs(std::atan2(r[3],r[4])*180./std::numbers::pi);
        front+=yaw<=20. && pitch<=25. && roll<=20.;
        turned+=yaw>=35. && yaw<=100. && pitch<=35.;
    }
    if(valid<3 || valid*4<total*3 || lastValid-firstValid<.3
        || firstValid-start>.35 || end-lastValid>.35)return unknown;
    if(front*5>=total*4)return "head_front";
    if(turned*5>=total*4)return "head_turned";
    return unknown;
}
} // namespace pfcore
