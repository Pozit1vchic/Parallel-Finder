#include <pfcore/SceneDescriptor.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace pfcore {
namespace {
bool validRotation(const std::array<double,9>& r) {
    for(double value:r)if(!std::isfinite(value))return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        double dot=0;for(int k=0;k<3;++k)dot+=r[i*3+k]*r[j*3+k];
        if(std::abs(dot-(i==j?1.:0.))>.002)return false;
    }
    const double determinant=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    return std::abs(determinant-1.)<=.002;
}
}
double angularDistanceDegrees(double a,double b) noexcept {
    if (!std::isfinite(a)||!std::isfinite(b)) return 180;
    return std::abs(std::remainder(a-b,360.));
}
ViewDirection viewDirection(double yaw) noexcept {
    if (!std::isfinite(yaw)) return ViewDirection::Unknown;
    const auto sector=static_cast<int>(std::floor((std::remainder(yaw,360.)+22.5)/45.));
    static constexpr ViewDirection labels[]={ViewDirection::Front,ViewDirection::QuarterLeft,
        ViewDirection::SideLeft,ViewDirection::BackQuarterLeft,ViewDirection::Back,
        ViewDirection::BackQuarterRight,ViewDirection::SideRight,ViewDirection::QuarterRight};
    return labels[(sector%8+8)%8];
}
SceneDescriptor describeScene(const MotionWindow& window,double start,double end,std::size_t identityId) {
    SceneDescriptor result;result.sourceId=window.sourceId;result.identityId=identityId;
    result.start=start;result.end=end;
    result.sceneContext=window.sceneContext;
    if (window.hasSceneIndex) result.sceneId=window.sceneIndex;
    if (!std::isfinite(start)||!std::isfinite(end)||end<=start) return result;
    const MovementClassifier classifier;
    result.motion=classifier.classifyForGrouping(window,start,end);
    const auto legacyPosture=classifier.classifyPostureForGrouping(window,start,end);
    if (legacyPosture=="standing") result.posture=ScenePosture::Standing;
    else if (legacyPosture=="seated_lying") result.posture=ScenePosture::SeatedOrLying;
    const auto point=[](const PoseFrame& frame,std::size_t j)->const Keypoint* {
        if (j>=frame.keypoints.size())return nullptr;
        const auto& p=frame.keypoints[j];
        return p.confidence>=.5 && std::isfinite(p.confidence) && std::isfinite(p.x)&&std::isfinite(p.y)?&p:nullptr;
    };
    std::size_t samples=0,normalized=0,horizontal=0,torsoSamples=0,headSamples=0;
    double first=std::numeric_limits<double>::infinity(),last=-first,confidence=0;
    double firstHead=first,lastHead=last,headConfidence=0,headVisible=0,rollX=0,rollY=0;
    std::array<double,2> firstCenter{},lastCenter{};
    for (const auto& frame:window.frames) {
        if (frame.timestampSeconds<start||frame.timestampSeconds>=end||!std::isfinite(frame.timestampSeconds))continue;
        ++samples;
        std::size_t headJoints=0;
        double headMinX=std::numeric_limits<double>::infinity(),headMinY=headMinX;
        double headMaxX=-headMinX,headMaxY=-headMinX;
        for(std::size_t j=0;j<5;++j)if(const auto* p=point(frame,j)) {
            ++headJoints;headConfidence+=std::clamp(p->confidence,0.,1.);
            headMinX=std::min(headMinX,p->x);headMaxX=std::max(headMaxX,p->x);
            headMinY=std::min(headMinY,p->y);headMaxY=std::max(headMaxY,p->y);
        }
        headVisible+=headJoints;
        if(headJoints>=3&&std::hypot(headMaxX-headMinX,headMaxY-headMinY)>1e-6) {
            ++headSamples;firstHead=std::min(firstHead,frame.timestampSeconds);lastHead=std::max(lastHead,frame.timestampSeconds);
        }
        for (std::size_t j=0;j<17;++j) if (const auto* p=point(frame,j)) confidence+=std::clamp(p->confidence,0.,1.);
        const auto *sl=point(frame,5),*sr=point(frame,6),*hl=point(frame,11),*hr=point(frame,12);
        if (!sl||!sr||!hl||!hr)continue;
        const double sx=(sl->x+sr->x)*.5,sy=(sl->y+sr->y)*.5;
        const double hx=(hl->x+hr->x)*.5,hy=(hl->y+hr->y)*.5;
        const double scale=std::hypot(hx-sx,hy-sy);
        if (scale<1e-6)continue;
        ++torsoSamples;
        rollX+=(hx-sx)/scale;rollY+=(hy-sy)/scale;
        if (std::abs(hx-sx)>.78*scale)++horizontal;
        if (frame.timestampSeconds<first) {first=frame.timestampSeconds;firstCenter={hx,hy};}
        if (frame.timestampSeconds>last) {last=frame.timestampSeconds;lastCenter={hx,hy};}
        ++normalized;
        for (std::size_t j=0;j<17;++j) if (const auto* p=point(frame,j)) {
            const double q=std::clamp(p->confidence,0.,1.);
            result.normalizedSkeleton[j*2]+=(p->x-hx)/scale*q;
            result.normalizedSkeleton[j*2+1]+=(p->y-hy)/scale*q;
            result.jointEvidence[j]+=q;
        }
    }
    double visible=0;
    for (std::size_t j=0;j<17;++j) {
        if (result.jointEvidence[j]>0) {
            result.normalizedSkeleton[j*2]/=result.jointEvidence[j];
            result.normalizedSkeleton[j*2+1]/=result.jointEvidence[j];
        }
        result.jointEvidence[j]=normalized?result.jointEvidence[j]/normalized:0;
        visible+=result.jointEvidence[j];
    }
    result.visibleCoverage=visible/17.;
    result.poseConfidence=samples?confidence/(samples*17.):0;
    result.headVisibleCoverage=samples?headVisible/(samples*5.):0;
    if(std::isfinite(firstHead)&&lastHead>firstHead)
        result.headTemporalCoverage=std::clamp((lastHead-firstHead)/(end-start),0.,1.);
    result.headEvidenceQuality=std::clamp(.5*(samples?headConfidence/(samples*5.):0)
        +.2*result.headVisibleCoverage+.3*result.headTemporalCoverage,0.,1.);
    if(headSamples<3)result.headEvidenceQuality*=.25;
    if(torsoSamples>=3 && std::hypot(rollX,rollY)/torsoSamples>=.75) {
        result.body.rollDegrees=std::atan2(rollX,rollY)*180./std::numbers::pi;
        result.body.confidence=double(torsoSamples)/std::max<std::size_t>(1,samples);
    }
    if (std::isfinite(first)&&last>first) {
        result.temporalCoverage=std::clamp((last-first)/(end-start),0.,1.);
        result.imageVelocity={(lastCenter[0]-firstCenter[0])/(last-first),(lastCenter[1]-firstCenter[1])/(last-first)};
    }
    if (result.posture==ScenePosture::SeatedOrLying && torsoSamples>=3 && horizontal*5>=torsoSamples*4)
        result.posture=ScenePosture::Lying;
    std::optional<ScenePosture> measuredPosture;
    bool postureConflict=false;
    for(const auto& observation:window.videoActions) {
        if(window.sceneEndSeconds>window.sceneStartSeconds
            && (start<window.sceneStartSeconds-1e-6 || end>window.sceneEndSeconds+1e-6))continue;
        const auto label=measuredSpecificPosture(observation,window.sourceId,start,end);
        if(label=="unknown")continue;
        const auto posture=label=="standing"?ScenePosture::Standing
            :label=="sitting"?ScenePosture::Sitting:ScenePosture::Lying;
        if(measuredPosture && *measuredPosture!=posture)postureConflict=true;
        else measuredPosture=posture;
    }
    if(postureConflict)result.posture=ScenePosture::Unknown;
    else if(measuredPosture)result.posture=*measuredPosture;
    // Geometry alone cannot reliably distinguish sitting/crouching/kneeling.
    // Specific sitting/lying refinement above requires independently measured
    // action evidence. Never borrow head rotation as the body's orientation.
    result.postureEvidence=postureConflict?EvidenceLevel::Ambiguous:result.posture==ScenePosture::Unknown?EvidenceLevel::Unknown
        : result.posture==ScenePosture::SeatedOrLying?EvidenceLevel::Ambiguous:EvidenceLevel::Probable;
    result.identityConfidence=std::clamp(std::max(window.faceConfidence,window.appearanceConfidence),0.,1.);
    const HeadOrientationSample* head=nullptr;
    double nearest=std::numeric_limits<double>::infinity();std::size_t headCount=0,totalHead=0;
    for (const auto& sample:window.headOrientations) {
        if (sample.time<start||sample.time>=end||!std::isfinite(sample.time))continue;
        ++totalHead;
        if (!sample.observed)continue;
        const auto& r=sample.rotation;
        if(!validRotation(r))continue;
        ++headCount;
        const double distance=std::abs(sample.time-(start+end)*.5);
        if (distance<nearest) {nearest=distance;head=&sample;}
    }
    if (head && headCount>=2) {
        const auto& r=head->rotation;
        result.head.rotation=r;
        result.head.yawDegrees=std::atan2(r[2],r[8])*180./std::numbers::pi;
        result.head.pitchDegrees=std::asin(std::clamp(-r[5],-1.,1.))*180./std::numbers::pi;
        result.head.rollDegrees=std::atan2(r[3],r[4])*180./std::numbers::pi;
        result.head.confidence=double(headCount)/std::max<std::size_t>(1,totalHead);
    }
    if(!(std::isfinite(window.sceneStartSeconds)&&std::isfinite(window.sceneEndSeconds)&&window.sceneStartSeconds>=0
        &&window.sceneEndSeconds>window.sceneStartSeconds&&(start<window.sceneStartSeconds-1e-6||end>window.sceneEndSeconds+1e-6))) {
        const auto body=summarizeBodyYaw(window.bodyOrientations,window.sourceId,start,end);
        result.body.yawDegrees=body.yawDegrees;
        result.bodyYawObservedSamples=body.observed;result.bodyYawRequestedSamples=body.total;
        result.bodyYawEvidence=body.ambiguous?EvidenceLevel::Ambiguous:body.yawDegrees?EvidenceLevel::Probable:EvidenceLevel::Unknown;
        if(body.yawDegrees)result.body.confidence=body.evidenceStrength;
    }
    result.evidenceQuality=std::clamp(.55*result.poseConfidence+.25*result.visibleCoverage+.20*result.temporalCoverage,0.,1.);
    if(normalized<3)result.evidenceQuality*=.25;
    return result;
}
std::optional<double> scenePoseDistance(const SceneDescriptor& a,const SceneDescriptor& b) {
    double distance=0,weight=0;std::size_t shared=0;
    for(std::size_t j=0;j<17;++j) {
        const double q=std::min(a.jointEvidence[j],b.jointEvidence[j]);
        if (q<.5)continue;
        ++shared;weight+=q;
        distance+=q*std::hypot(a.normalizedSkeleton[j*2]-b.normalizedSkeleton[j*2],a.normalizedSkeleton[j*2+1]-b.normalizedSkeleton[j*2+1]);
    }
    if(shared<5||weight<=0)return {};
    return distance/weight;
}
std::optional<double> sceneHeadDistance(const SceneDescriptor& a,const SceneDescriptor& b) {
    if(!a.head.rotation||!b.head.rotation||!validRotation(*a.head.rotation)||!validRotation(*b.head.rotation))return {};
    double dot=0;for(std::size_t i=0;i<9;++i)dot+=(*a.head.rotation)[i]*(*b.head.rotation)[i];
    if(!std::isfinite(dot))return {};
    return std::acos(std::clamp((dot-1.)*.5,-1.,1.))/std::numbers::pi;
}
double comparisonEvidenceQuality(const SceneDescriptor& a,const SceneDescriptor& b,bool headOnly) {
    return headOnly?std::min(a.headEvidenceQuality,b.headEvidenceQuality):std::min(a.evidenceQuality,b.evidenceQuality);
}
std::optional<double> comparisonPoseSimilarity(const SceneDescriptor& a,const SceneDescriptor& b,bool headOnly) {
    const auto distance=headOnly?sceneHeadDistance(a,b):scenePoseDistance(a,b);
    if(!distance)return {};
    return headOnly?std::clamp(1.-*distance,0.,1.):std::exp(-*distance);
}
}
