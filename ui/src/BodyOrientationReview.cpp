#include "BodyOrientationReview.h"
#include "BodyOrientationCache.h"
#include <pfcore/PathEncoding.hpp>
#include <pfcore/PoseAssociation.hpp>
#include <pfcore/DetectorOutputReplay.hpp>
#include <pfgpu/BodyOrientationEstimator.hpp>
#include <pfservices/PfCache.hpp>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QDateTime>
#include <algorithm>
#include <set>
#include <memory>
#include <stdexcept>

namespace pfui {
namespace {
std::string hashFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(pfcore::pathToUtf8(path)));QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly)||!hash.addData(&file))throw std::runtime_error("BODY_ORIENTATION_MODEL_READ_FAILED");
    return hash.result().toHex().toStdString();
}
std::string requestKey(const std::string& source,const pfcore::PoseFrame& frame,const std::string& nameSpace,
    const std::string& poseHash,const pfgpu::PoseEstimatorParams& params,pfgpu::Provider provider,const pfcore::VideoDecodeOptions& decode) {
    QJsonArray points;for(const auto& p:frame.keypoints)points.push_back(QJsonArray{p.x,p.y,p.confidence});
    const auto bytes=QJsonDocument(QJsonObject{{"source",QString::fromStdString(source)},{"namespace",QString::fromStdString(nameSpace)},
        {"poseModel",QString::fromStdString(poseHash)},{"points",points},{"time",frame.timestampSeconds},
        {"poseProvider",static_cast<int>(params.provider)},{"bodyProvider",static_cast<int>(provider)},
        {"poseProfile",QString::fromStdString(params.profile)},{"poseBatch",static_cast<double>(params.batchSize)},
        {"poseKeypoints",static_cast<double>(params.keypointCount)},
        {"decodeNvidia",decode.preferNvidia},{"decodeMinimumNvidiaPixels",static_cast<double>(decode.minimumNvidiaPixels)},
        {"decodeAutomaticConversion",decode.autoNvidiaConversion},{"decodeConversionThreads",decode.conversionThreads},
        {"decodeDevice",decode.nvidiaDevice},{"decodeResizeNvidia",decode.resizeOnNvidia},
        {"workingResolution",QJsonArray{1280,720}},
        {"inputWidth",params.inputWidth},{"inputHeight",params.inputHeight},{"threshold",params.confidenceThreshold},
        {"nms",params.nmsIouThreshold},{"outputMode",static_cast<int>(params.outputMode)},
        {"bodyModel",QString::fromLatin1(pfcore::kBodyOrientationModelHash)}}).toJson(QJsonDocument::Compact);
    return "body-orientation-v2|bilinear-byte1024|all17-replay1e4-score1e3-margin2e3|torso-anchor80|mutual-iou50-margin15|mirror25-crop20|"
        +QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
}
bool torsoVisible(const pfcore::PoseFrame& frame,int width,int height) {
    if(frame.keypoints.size()!=17)return false;
    const auto visible=[&](std::size_t index) {
        const auto& p=frame.keypoints[index];return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.confidence)
            &&p.confidence>=.5&&p.confidence<=1&&p.x>=0&&p.y>=0&&p.x<width&&p.y<height;
    };
    return visible(5)&&visible(6)&&(visible(11)||visible(12)||((visible(7)||visible(9))&&(visible(8)||visible(10))));
}
bool upperBodyAnchorVisible(const pfcore::PoseFrame& frame,const pfgpu::BodyOrientationRegion& box) {
    // Only used with an all-17-output replay, not a loose skeletal association.
    // A partial upper body can have no visible face, elbows or hips. The two
    // independent detectors and four real orientation views still have to agree.
    for(const std::size_t index:{5U,6U,11U,12U}) {
        const auto& p=frame.keypoints[index];
        if(p.confidence>=.8&&p.x>=box.left&&p.x<box.right&&p.y>=box.top&&p.y<box.bottom)return true;
    }
    return false;
}
}
BodyOrientationReviewStats reviewMatchedBodyOrientations(std::vector<pfcore::MotionWindow>& windows,
    const std::vector<pfcore::MotionMatch>& matches,pfgpu::PoseEstimator& pose,
    const std::filesystem::path& model,const std::filesystem::path& cacheDirectory,
    const std::map<std::string,std::string>& sourceNamespaces,pfgpu::Provider provider,
    pfcore::VideoDecodeOptions options,const std::function<bool()>& cancelled,
    const std::function<void(std::size_t,std::size_t)>& progress) {
    BodyOrientationReviewStats stats;const auto stopped=[&]{return cancelled&&cancelled();};
    if(stopped()||std::none_of(matches.begin(),matches.end(),[](const auto& match){return !match.headOnlyComparison;}))return stats;
    struct Request {std::string source,key;pfcore::PoseFrame frame;std::vector<std::pair<std::size_t,std::size_t>> destinations;};
    try {
        if(hashFile(model)!=pfcore::kBodyOrientationModelHash)throw std::runtime_error("BODY_ORIENTATION_MODEL_UNVERIFIED");
        const auto poseHash=hashFile(pfcore::pathFromUtf8(pose.modelPath()));
        std::unique_ptr<pfservices::PfCache> cache;
        if(!cacheDirectory.empty())cache=std::make_unique<pfservices::PfCache>(cacheDirectory,64ULL*1024*1024);
        std::vector<Request> requests;std::map<std::string,std::size_t> lookup;
        for(auto& window:windows)window.bodyOrientations.clear();
        const auto add=[&](std::size_t index,double start,double end) {
            if(index>=windows.size()||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start)return;
            auto& window=windows[index];
            if(window.sceneStartSeconds>=0&&window.sceneEndSeconds>window.sceneStartSeconds
                &&(start<window.sceneStartSeconds-1e-6||end>window.sceneEndSeconds+1e-6))return;
            std::vector<const pfcore::PoseFrame*> candidates;
            for(const auto& frame:window.frames)if(std::isfinite(frame.timestampSeconds)&&frame.timestampSeconds>=start&&frame.timestampSeconds<end)
                candidates.push_back(&frame);
            std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
            candidates.erase(std::unique(candidates.begin(),candidates.end(),[](auto a,auto b){return a->timestampSeconds==b->timestampSeconds;}),candidates.end());
            if(candidates.size()<3||candidates.back()->timestampSeconds-candidates.front()->timestampSeconds<.3)return;
            for(const auto position:std::set<std::size_t>{0,(candidates.size()-1)/2,candidates.size()-1}) {
                const auto& frame=*candidates[position];const auto found=sourceNamespaces.find(window.sourceId);
                const auto key=requestKey(window.sourceId,frame,found==sourceNamespaces.end()?"uncached":found->second,poseHash,pose.params(),provider,options);
                const auto [entry,inserted]=lookup.try_emplace(key,requests.size());
                if(inserted)requests.push_back({window.sourceId,key,frame,{}});
                auto& destinations=requests[entry->second].destinations;
                if(std::any_of(destinations.begin(),destinations.end(),[&](auto destination){return destination.first==index;}))continue;
                const auto slot=window.bodyOrientations.size();
                pfcore::BodyOrientationSample pending;pending.sourceId=window.sourceId;pending.time=frame.timestampSeconds;
                pending.modelHash=pfcore::kBodyOrientationModelHash;pending.reason="not_evaluated";
                window.bodyOrientations.push_back(pending);destinations.emplace_back(index,slot);
            }
        };
        for(const auto& match:matches)if(!match.headOnlyComparison) {
            if(stopped())return stats;
            add(match.leftIndex,match.leftStartSeconds,match.leftEndSeconds);add(match.rightIndex,match.rightStartSeconds,match.rightEndSeconds);
        }
        stats.requested=requests.size();if(progress)progress(0,stats.requested);
        if(requests.empty())return stats;
        std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b){return a.source!=b.source?a.source<b.source:a.frame.timestampSeconds<b.frame.timestampSeconds;});
        std::map<std::string,std::vector<std::pair<std::string,std::vector<std::uint8_t>>>> writes;
        pfgpu::BodyOrientationEstimator body(pfcore::pathToUtf8(model),provider,options.threads>0?std::min(options.threads,8):4);
        pfcore::VideoDecoder decoder;pfcore::DecodedFrame pixels;std::string opened;double decodedTime=-INFINITY;bool converted=false;
        options.maxWidth=1280;options.maxHeight=720;std::size_t done=0;
        for(const auto& request:requests) {
            if(stopped())break;
            pfcore::BodyOrientationSample sample;sample.sourceId=request.source;sample.time=request.frame.timestampSeconds;
            sample.modelHash=pfcore::kBodyOrientationModelHash;sample.reason="frame_unavailable";
            bool cached=false,cacheable=false;
            const auto generation=sourceNamespaces.find(request.source);
            const bool reusable=generation!=sourceNamespaces.end()&&!generation->second.empty();
            if(cache&&reusable)if(const auto bytes=cache->get(request.key))
                if(const auto known=body_detail::read(*bytes,request.source,sample.time)) {sample=*known;cached=true;++stats.cached;}
            if(!cached) {
                const QFileInfo before(QString::fromStdString(request.source));const auto stamp=before.lastModified();const auto size=before.size();
                if(opened!=request.source) {decoder.open(request.source,options);decoder.setRgbaMaxDimensions(1280,720);opened=request.source;decodedTime=-INFINITY;converted=false;}
                if(!std::isfinite(decodedTime)||sample.time-decodedTime>4) {decoder.seek(sample.time);decodedTime=-INFINITY;converted=false;}
                while(decodedTime+1e-6<sample.time&&!stopped()) {
                    if(!decoder.readNext(pixels,false))break;
                    decodedTime=pixels.timestampSeconds;converted=false;++stats.decoded;
                }
                if(!stopped()&&std::abs(decodedTime-sample.time)<=std::max(.002,.25/std::max(1.,decoder.info().frameRate))
                    &&(converted||decoder.convertCurrentFrameToRgba(pixels))) {
                    converted=true;cacheable=true;sample.reason="pose_unassociated";
                    const auto people=pose.infer({pixels.width,pixels.height,pixels.rgba.data()});++stats.poseInferences;
                    std::vector<pfcore::PoseFrame> fresh;
                    std::vector<pfgpu::BodyOrientationRegion> regions;
                    for(const auto& person:people) {
                        pfcore::PoseFrame f;f.timestampSeconds=sample.time;
                        for(std::size_t p=0;p+2<person.keypoints.size();p+=3)f.keypoints.push_back({person.keypoints[p],person.keypoints[p+1],person.keypoints[p+2]});
                        fresh.push_back(std::move(f));
                        regions.push_back({std::clamp<double>(person.left,0,pixels.width),std::clamp<double>(person.top,0,pixels.height),
                            std::clamp<double>(person.right,0,pixels.width),std::clamp<double>(person.bottom,0,pixels.height)});
                    }
                    const auto raw=pfcore::associateDetectorOutputReplay(request.frame,fresh,pixels.width,pixels.height);
                    const auto articulated=pfcore::associateObservedPose(request.frame,fresh,pixels.width,pixels.height);
                    std::optional<std::size_t> subject;
                    if(raw) {subject=raw->candidate;++stats.rawReplays;}
                    else if(articulated)subject=articulated->candidate;
                    if(subject) {
                        const auto index=*subject;const auto& region=regions[index];sample.poseAssociated=true;
                        sample.reason="torso_not_observed";
                        if(people[index].confidence>=.4&&(torsoVisible(fresh[index],pixels.width,pixels.height)
                            ||(raw&&upperBodyAnchorVisible(fresh[index],region)))
                            &&region.right-region.left>=16&&region.bottom-region.top>=32&&!stopped()) {
                            const pfgpu::PoseImage image{pixels.width,pixels.height,pixels.rgba.data()};
                            const auto original=body.infer(image);++stats.bodyInferences;if(stopped())break;
                            const auto mirrored=body.infer(image,true);++stats.bodyInferences;if(stopped())break;
                            const auto measured=pfgpu::associateBodyOrientations(regions,original,mirrored)[index];
                            sample.reason=pfgpu::bodyYawStatusCode(measured.status);
                            if(measured.yawDegrees) {
                                sample.yawDegrees=*measured.yawDegrees;sample.associationIou=measured.associationIou;
                                sample.associationMargin=measured.associationMargin;sample.mirrorDifference=measured.mirrorDifferenceDegrees;
                                const double w=region.right-region.left,h=region.bottom-region.top;
                                const int left=std::max(0,int(std::floor(region.left-.2*w))),top=std::max(0,int(std::floor(region.top-.15*h)));
                                const int right=std::min(pixels.width,int(std::ceil(region.right+.2*w))),bottom=std::min(pixels.height,int(std::ceil(region.bottom+.15*h)));
                                std::vector<std::uint8_t> crop(static_cast<std::size_t>(right-left)*(bottom-top)*4);
                                for(int y=top;y<bottom;++y)std::copy_n(pixels.rgba.data()+(static_cast<std::size_t>(y)*pixels.width+left)*4,(right-left)*4,
                                    crop.data()+static_cast<std::size_t>(y-top)*(right-left)*4);
                                auto croppedRegions=regions;
                                for(auto& r:croppedRegions) {r.left=std::clamp(r.left-left,0.,double(right-left));r.right=std::clamp(r.right-left,0.,double(right-left));
                                    r.top=std::clamp(r.top-top,0.,double(bottom-top));r.bottom=std::clamp(r.bottom-top,0.,double(bottom-top));}
                                const pfgpu::PoseImage cropImage{right-left,bottom-top,crop.data()};
                                const auto a=body.infer(cropImage);++stats.bodyInferences;if(stopped())break;
                                const auto b=body.infer(cropImage,true);++stats.bodyInferences;if(stopped())break;
                                const auto cropped=pfgpu::associateBodyOrientations(croppedRegions,a,b)[index];
                                sample.reason="crop_"+std::string(pfgpu::bodyYawStatusCode(cropped.status));
                                if(cropped.yawDegrees) {
                                    sample.cropDifference=std::abs(std::remainder(sample.yawDegrees-*cropped.yawDegrees,360.));
                                    sample.associationIou=std::min(sample.associationIou,cropped.associationIou);
                                    sample.associationMargin=std::min(sample.associationMargin,cropped.associationMargin);
                                    sample.mirrorDifference=std::max(sample.mirrorDifference,cropped.mirrorDifferenceDegrees);
                                    sample.observed=sample.cropDifference<=20;sample.reason=sample.observed?"observed":"crop_disagreement";
                                }
                            }
                        }
                    }
                }
                const QFileInfo after(QString::fromStdString(request.source));
                if(size!=after.size()||stamp!=after.lastModified())throw std::runtime_error("BODY_ORIENTATION_SOURCE_CHANGED");
                // Unconfirmed candidate angles have no public meaning. Keep
                // cold and cached unknown observations identical (JSON uses null).
                if(!sample.observed)sample.yawDegrees=0;
                if(cache&&reusable&&cacheable&&!stopped())writes[request.source].emplace_back(request.key,body_detail::serialize(sample));
            }
            stats.observed+=sample.observed;
            for(const auto [wi,slot]:request.destinations)windows[wi].bodyOrientations[slot]=sample;
            if(progress)progress(++done,stats.requested);
        }
        if(cache)for(const auto& [source,entries]:writes) {std::string error;if(!cache->putForSourceBatch(source,entries,error))stats.error="BODY_ORIENTATION_CACHE_WRITE_FAILED: "+error;}
    }catch(const std::exception& error) {
        stats.error=error.what();for(auto& window:windows)window.bodyOrientations.clear();
    }
    return stats;
}
}
