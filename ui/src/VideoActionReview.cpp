#include "VideoActionReview.h"
#include <pfcore/PoseAssociation.hpp>
#include <pfcore/SceneDetector.hpp>
#include <pfservices/PfCache.hpp>
#include <pfgpu/VideoActionEstimator.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <memory>
#include <set>

namespace pfui {
namespace {
std::string hashFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file))throw std::runtime_error("Cannot fingerprint action-review model");
    return hash.result().toHex().toStdString();
}
QJsonArray pointsJson(const pfcore::PoseFrame& frame) {
    QJsonArray points;for(const auto& p:frame.keypoints)points.append(QJsonArray{p.x,p.y,p.confidence});return points;
}
std::vector<std::uint8_t> serialize(const pfcore::VideoActionEvidence& evidence) {
    QJsonArray times,scores,frozen;
    for(const auto time:evidence.frameTimes)times.append(time);
    if(evidence.observed)for(int i=0;i<80;++i) {scores.append(evidence.scores[i]);frozen.append(evidence.frozenScores[i]);}
    const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"source",QString::fromStdString(evidence.sourceId)},
        {"model",QString::fromStdString(evidence.modelHash)},{"start",evidence.start},{"end",evidence.end},
        {"observed",evidence.observed},{"continuous",evidence.continuous},{"associated",evidence.associated},
        {"torsoVisible",evidence.torsoVisible},{"times",times},{"scores",scores},{"frozen",frozen}}).toJson(QJsonDocument::Compact);
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size()};
}
std::optional<pfcore::VideoActionEvidence> deserialize(const std::vector<std::uint8_t>& bytes,
    const pfcore::VideoActionEvidence& expected) {
    QJsonParseError error;const auto document=QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size()),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return {};
    const auto o=document.object();
    if(o["schema"].toInt()!=1 || o["source"].toString().toStdString()!=expected.sourceId
        || o["model"].toString().toStdString()!=expected.modelHash || !o["start"].isDouble() || !o["end"].isDouble()
        || o["start"].toDouble()!=expected.start || o["end"].toDouble()!=expected.end)return {};
    for(const auto field:{"observed","continuous","associated","torsoVisible"})if(!o[field].isBool())return {};
    auto result=expected;result.observed=o["observed"].toBool();result.continuous=o["continuous"].toBool();
    result.associated=o["associated"].toBool();result.torsoVisible=o["torsoVisible"].toBool();
    if(!o["times"].isArray() || !o["scores"].isArray() || !o["frozen"].isArray())return {};
    const auto times=o["times"].toArray();if(times.size()>192)return {};
    double previous=-INFINITY;
    for(const auto value:times) {
        if(!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble()<=previous
            || value.toDouble()<expected.start-1e-6 || value.toDouble()>=expected.end-1e-6)return {};
        previous=value.toDouble();result.frameTimes.push_back(previous);
    }
    const auto scores=o["scores"].toArray(),frozen=o["frozen"].toArray();
    if(result.observed) {
        if(scores.size()!=80 || frozen.size()!=80 || times.size()<5 || !result.associated || !result.continuous || !result.torsoVisible)return {};
        for(int i=0;i<80;++i) {
            if(!scores[i].isDouble() || !frozen[i].isDouble())return {};
            result.scores[i]=scores[i].toDouble();result.frozenScores[i]=frozen[i].toDouble();
            if(!std::isfinite(result.scores[i]) || !std::isfinite(result.frozenScores[i])
                || result.scores[i]<0 || result.scores[i]>1 || result.frozenScores[i]<0 || result.frozenScores[i]>1)return {};
        }
    } else if(!scores.isEmpty() || !frozen.isEmpty())return {};
    return result;
}
bool visibleTorso(const pfcore::PoseFrame& frame) {
    if(frame.keypoints.size()<13)return false;
    for(const int joint:{5,6,11,12}) {
        const auto& p=frame.keypoints[joint];
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.confidence)
            || p.confidence<(joint<7 ? .5:.35))return false;
    }
    return true;
}
}

VideoActionReviewStats reviewMatchedVideoActions(std::vector<pfcore::MotionWindow>& windows,
    const std::vector<pfcore::MotionMatch>& matches,pfgpu::PoseEstimator& pose,
    const std::filesystem::path& poseModel,const std::filesystem::path& actionModel,
    const std::filesystem::path& cacheDirectory,const std::map<std::string,std::string>& sourceNamespaces,
    pfgpu::Provider provider,pfcore::VideoDecodeOptions options,
    const std::function<bool()>& cancelled,const std::function<void(std::size_t,std::size_t)>& progress) {
    VideoActionReviewStats stats;const auto stopped=[&] {return cancelled && cancelled();};
    struct Request {pfcore::VideoActionEvidence evidence;pfcore::PoseFrame anchor;std::string key;
        std::vector<std::size_t> destinations;};
    std::vector<Request> requests;std::set<std::size_t> selected;
    try {
        if(stopped() || matches.empty())return stats;
        const auto modelHash=hashFile(actionModel);
        if(modelHash!=pfcore::kVideoActionModelHash)throw std::runtime_error("Unverified trained video-action model");
        const auto identity=modelHash+"|pose="+hashFile(poseModel)+"|ort="+pfgpu::ortRuntimeVersion()
            +"|tf32="+qEnvironmentVariable("PF_AUDIT_CUDA_TF32").toStdString();
        std::map<std::string,std::size_t> lookup;
        for(const auto& match:matches) {
            if(match.headOnlyComparison || !match.appearanceVerified)continue;
            for(int side=0;side<2;++side) {
                const auto wi=side==0 ? match.leftIndex:match.rightIndex;
                if(wi>=windows.size())continue;
                auto& window=windows[wi];
                if(selected.insert(wi).second)window.videoActions.clear();
                const double start=side==0 ? match.leftStartSeconds:match.rightStartSeconds;
                const double end=side==0 ? match.leftEndSeconds:match.rightEndSeconds;
                if(window.staticFrameSet || window.sourceId.empty() || !std::isfinite(start) || !std::isfinite(end)
                    || start<0 || end-start<.5 || end-start>3 || window.frames.empty())continue;
                if(window.sceneStartSeconds>=0 && start<window.sceneStartSeconds-1e-6)continue;
                if(window.sceneEndSeconds>=0 && end>window.sceneEndSeconds+1e-6)continue;
                const pfcore::PoseFrame* anchor=nullptr;std::size_t count=0,torso=0;double first=INFINITY,last=-INFINITY;
                for(const auto& frame:window.frames)if(frame.timestampSeconds>=start-1e-6 && frame.timestampSeconds<end-1e-6) {
                    ++count;torso+=visibleTorso(frame);
                    first=std::min(first,frame.timestampSeconds);last=std::max(last,frame.timestampSeconds);
                    if(!anchor || std::abs(frame.timestampSeconds-(start+end)*.5)<std::abs(anchor->timestampSeconds-(start+end)*.5))anchor=&frame;
                }
                if(!anchor || !visibleTorso(*anchor) || count<3 || torso*4<count*3
                    || first-start>.35 || end-last>.35)continue;
                const QFileInfo sourceInfo(QString::fromStdString(window.sourceId));
                if(!sourceInfo.isFile())continue;
                const auto source=sourceNamespaces.find(window.sourceId);
                const auto data=QJsonDocument(QJsonObject{{"source",QString::fromStdString(window.sourceId)},
                    {"namespace",source==sourceNamespaces.end()?QString():QString::fromStdString(source->second)},
                    {"bytes",sourceInfo.size()},{"modified",sourceInfo.lastModified().toMSecsSinceEpoch()},
                    {"start",start},{"end",end},{"anchor",anchor->timestampSeconds},{"points",pointsJson(*anchor)},
                    {"model",QString::fromStdString(identity)},{"provider",static_cast<int>(provider)}}).toJson(QJsonDocument::Compact);
                const auto key="video-actions-v1|native-halfopen|fast-bilinear1280-short256|associated-midpoint|native-cut27-1-3|"
                    +QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex().toStdString();
                const auto [slot,inserted]=lookup.try_emplace(key,requests.size());
                if(inserted) {
                    Request request;request.evidence.sourceId=window.sourceId;request.evidence.modelHash=modelHash;
                    request.evidence.start=start;request.evidence.end=end;request.evidence.torsoVisible=true;
                    request.anchor=*anchor;request.key=key;requests.push_back(std::move(request));
                }
                requests[slot->second].destinations.push_back(wi);
            }
        }
        stats.requested=requests.size();if(requests.empty())return stats;
        std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b) {
            return a.evidence.sourceId!=b.evidence.sourceId ? a.evidence.sourceId<b.evidence.sourceId:a.evidence.start<b.evidence.start;
        });
        std::unique_ptr<pfservices::PfCache> cache;
        if(!cacheDirectory.empty())try {cache=std::make_unique<pfservices::PfCache>(cacheDirectory,64ULL*1024*1024);}catch(...) {}
        pfgpu::VideoActionEstimator actions(actionModel.string(),provider,8);
        pfcore::VideoDecoder decoder;std::string opened;
        std::map<std::string,std::vector<std::pair<std::string,std::vector<std::uint8_t>>>> pendingCacheWrites;
        options.maxWidth=1280;options.maxHeight=720;options.preferNvidia=false;
        std::size_t done=0;
        for(const auto& request:requests) {
            if(stopped())break;
            auto evidence=request.evidence;bool fromCache=false,cacheable=false;
            if(cache && sourceNamespaces.contains(evidence.sourceId))if(const auto bytes=cache->get(request.key))
                if(const auto saved=deserialize(*bytes,evidence)) {evidence=*saved;fromCache=true;++stats.cached;}
            if(!fromCache) {
                const QFileInfo before(QString::fromStdString(evidence.sourceId));
                if(opened!=evidence.sourceId) {decoder.open(evidence.sourceId,options);opened=evidence.sourceId;}
                const auto cuts=pfcore::SceneDetector(27,1,3).detectInterval(decoder,evidence.start,evidence.end,{},stopped);
                if(stopped())break;
                evidence.continuous=cuts.empty();cacheable=true;
                if(!cuts.empty())++stats.cuts;
                else {
                    decoder.setRgbaMaxDimensions(1280,720);decoder.seek(evidence.start);
                    std::vector<pfcore::DecodedFrame> pixels;pfcore::DecodedFrame image;
                    while(!stopped() && decoder.readNext(image,false)) {
                        if(image.timestampSeconds<evidence.start-1e-6)continue;
                        if(image.timestampSeconds>=evidence.end-1e-6)break;
                        if(pixels.size()>=192 || !decoder.convertCurrentFrameToRgba(image)) {cacheable=false;break;}
                        evidence.frameTimes.push_back(image.timestampSeconds);pixels.push_back(std::move(image));++stats.decoded;
                    }
                    if(stopped())break;
                    if(cacheable && pixels.size()>=5) {
                        const auto anchor=std::min_element(pixels.begin(),pixels.end(),[&](const auto& a,const auto& b) {
                            return std::abs(a.timestampSeconds-request.anchor.timestampSeconds)<std::abs(b.timestampSeconds-request.anchor.timestampSeconds);
                        });
                        if(std::abs(anchor->timestampSeconds-request.anchor.timestampSeconds)<=1e-6) {
                            const auto people=pose.infer({anchor->width,anchor->height,anchor->rgba.data()});
                            std::vector<pfcore::PoseFrame> detected;
                            for(const auto& person:people) {
                                pfcore::PoseFrame frame;frame.timestampSeconds=anchor->timestampSeconds;
                                for(std::size_t i=0;i+2<person.keypoints.size();i+=3)
                                    frame.keypoints.push_back({person.keypoints[i],person.keypoints[i+1],person.keypoints[i+2]});
                                detected.push_back(std::move(frame));
                            }
                            if(const auto association=pfcore::associateObservedPose(request.anchor,detected,anchor->width,anchor->height)) {
                                const auto& person=people[association->candidate];
                                if(person.confidence>=.4 && visibleTorso(detected[association->candidate])) {
                                    evidence.associated=true;
                                    const std::array<pfgpu::VideoActionSubject,1> subjects{{{person.left,person.top,person.right,person.bottom}}};
                                    std::vector<pfgpu::VideoActionFrame> frames;
                                    for(const auto& frame:pixels)frames.push_back({frame.width,frame.height,frame.rgba.data()});
                                    evidence.scores=actions.infer(frames,subjects).at(0);
                                    if(stopped())break;
                                    std::fill(frames.begin(),frames.end(),frames[16*(frames.size()-1)/31]);
                                    evidence.frozenScores=actions.infer(frames,subjects).at(0);evidence.observed=true;
                                }
                            }
                        }
                        if(!evidence.associated)++stats.unassociated;
                    }
                }
                const QFileInfo after(QString::fromStdString(evidence.sourceId));
                if(before.size()!=after.size() || before.lastModified()!=after.lastModified())
                    throw std::runtime_error("Source changed during video-action review");
                if(cache && cacheable && sourceNamespaces.contains(evidence.sourceId) && !stopped()) {
                    pendingCacheWrites[evidence.sourceId].emplace_back(request.key,serialize(evidence));
                }
            }
            stats.observed+=evidence.observed;
            for(const auto wi:request.destinations)windows[wi].videoActions.push_back(evidence);
            if(progress)progress(++done,stats.requested);
        }
        if(cache)for(const auto& [source,entries]:pendingCacheWrites) {
            std::string error;cache->putForSourceBatch(source,entries,error);
        }
        if(stopped())for(const auto wi:selected)windows[wi].videoActions.clear();
    }catch(const std::exception& error) {
        stats.error=error.what();for(const auto wi:selected)windows[wi].videoActions.clear();
    }
    return stats;
}
}
