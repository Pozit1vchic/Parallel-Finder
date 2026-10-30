#include <pfcore/SceneDescriptor.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace pfcore {
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
    std::size_t samples=0,normalized=0,horizontal=0,torsoSamples=0;
    double first=std::numeric_limits<double>::infinity(),last=-first,confidence=0;
    std::array<double,2> firstCenter{},lastCenter{};
    for (const auto& frame:window.frames) {
        if (frame.timestampSeconds<start||frame.timestampSeconds>=end||!std::isfinite(frame.timestampSeconds))continue;
        ++samples;
        for (std::size_t j=0;j<17;++j) if (const auto* p=point(frame,j)) confidence+=std::clamp(p->confidence,0.,1.);
        const auto *sl=point(frame,5),*sr=point(frame,6),*hl=point(frame,11),*hr=point(frame,12);
        if (!sl||!sr||!hl||!hr)continue;
        const double sx=(sl->x+sr->x)*.5,sy=(sl->y+sr->y)*.5;
        const double hx=(hl->x+hr->x)*.5,hy=(hl->y+hr->y)*.5;
        const double scale=std::hypot(hx-sx,hy-sy);
        if (scale<1e-6)continue;
        ++torsoSamples;
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
    if (std::isfinite(first)&&last>first) {
        result.temporalCoverage=std::clamp((last-first)/(end-start),0.,1.);
        result.imageVelocity={(lastCenter[0]-firstCenter[0])/(last-first),(lastCenter[1]-firstCenter[1])/(last-first)};
    }
    if (result.posture==ScenePosture::SeatedOrLying && torsoSamples>=3 && horizontal*5>=torsoSamples*4)
        result.posture=ScenePosture::Lying;
    // Sitting/crouching/kneeling and signed body yaw are not reliably
    // distinguishable from every monocular COCO skeleton. Preserve ambiguity;
    // never borrow the head rotation as the body's orientation.
    result.postureEvidence=result.posture==ScenePosture::Unknown?EvidenceLevel::Unknown
        : result.posture==ScenePosture::SeatedOrLying?EvidenceLevel::Ambiguous:EvidenceLevel::Probable;
    result.identityConfidence=std::clamp(std::max(window.faceConfidence,window.appearanceConfidence),0.,1.);
    const HeadOrientationSample* head=nullptr;
    double nearest=std::numeric_limits<double>::infinity();std::size_t headCount=0,totalHead=0;
    for (const auto& sample:window.headOrientations) {
        if (sample.time<start||sample.time>=end||!std::isfinite(sample.time))continue;
        ++totalHead;
        if (!sample.observed)continue;
        const auto& r=sample.rotation;
        bool valid=true;
        for (double v:r)if(!std::isfinite(v))valid=false;
        for (int i=0;i<3;++i)for(int j=0;j<3;++j) {
            double dot=0;for(int k=0;k<3;++k)dot+=r[i*3+k]*r[j*3+k];
            if(std::abs(dot-(i==j?1.:0.))>.002)valid=false;
        }
        const double determinant=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
        if(!valid||std::abs(determinant-1.)>.002)continue;
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
    result.evidenceQuality=std::clamp(.55*result.poseConfidence+.25*result.visibleCoverage+.20*result.temporalCoverage,0.,1.);
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
}
