#include <pfcore/PathEncoding.hpp>
#include "HeadOrientationReview.h"
#include <pfgpu/FaceEstimator.hpp>
#include <pfgpu/HeadPoseEstimator.hpp>
#include <pfservices/PfCache.hpp>
#include <pfservices/HeadOrientationCache.hpp>
#include <QCryptographicHash>
#include <QFile>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>

namespace pfui {
namespace {
std::string hashFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(pfcore::pathToUtf8(path)));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read head-orientation model");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))throw std::runtime_error("Cannot fingerprint head-orientation model");
    return hash.result().toHex().toStdString();
}

}

HeadOrientationReviewStats reviewMatchedHeadOrientations(std::vector<pfcore::MotionWindow>& windows,
    const std::vector<pfcore::MotionMatch>& matches,const std::filesystem::path& model,
    const std::filesystem::path& detector,const std::filesystem::path& cacheDirectory,
    const std::map<std::string,std::string>& sourceNamespaces,pfgpu::Provider provider,
    pfcore::VideoDecodeOptions options,const std::function<bool()>& cancelled,
    const std::function<void(std::size_t,std::size_t)>& progress,std::shared_ptr<pfservices::CacheBudget> budget) {
    HeadOrientationReviewStats stats;
    struct TimeRange {double start=0,end=0;};
    std::map<std::size_t,std::vector<TimeRange>> rangesByWindow;
    const auto addRange=[&](std::size_t index,double start,double end) {
        if(index>=windows.size())return;
        auto& ranges=rangesByWindow[index];
        if(!std::isfinite(start) || !std::isfinite(end) || end<start)return;
        ranges.push_back({start,end});
    };
    for(const auto& match:matches) {
        addRange(match.leftIndex,match.leftStartSeconds,match.leftEndSeconds);
        addRange(match.rightIndex,match.rightStartSeconds,match.rightEndSeconds);
    }
    if(rangesByWindow.empty() || cancelled())return stats;
    struct Request {
        std::string source,key;pfcore::PoseFrame frame;
        std::vector<std::pair<std::size_t,std::size_t>> destinations;
    };
    try {
        const auto preparationStarted=std::chrono::steady_clock::now();
        const std::string modelHash=hashFile(model);
        // A shape-compatible replacement is not the audited trained model.
        if(modelHash!="8585173bcd7e7d1e5f14baee995cca285814d3f3cf4e1a20d6cafa70787400a3")
            throw std::runtime_error("Unverified head-orientation model");
        const std::string identity=modelHash+"|detector="+hashFile(detector);
        std::unique_ptr<pfservices::PfCache> cache;
        // This independent subtree can evict only its own inexpensive head
        // observations, never the user's forty-minute analysis entries.
        if(!cacheDirectory.empty())try {cache=std::make_unique<pfservices::PfCache>(cacheDirectory,256ULL*1024*1024,budget);}catch(...) {}
        std::vector<Request> requests;std::map<std::string,std::size_t> lookup;
        std::map<std::string,std::vector<std::pair<std::string,std::vector<std::uint8_t>>>> pendingCacheWrites;
        for(const auto& [wi,ranges]:rangesByWindow) {
            auto& window=windows[wi];window.headOrientations.clear();
            for(const auto& frame:window.frames) {
                const auto slot=window.headOrientations.size();
                window.headOrientations.push_back({frame.timestampSeconds,{},false});
                // Grouping consumes head evidence only inside each matched
                // clip interval. Decoding every sampled pose in the full
                // motion window needlessly replays large portions of source
                // videos after the matcher has already narrowed the result.
                const bool needed=ranges.empty() || std::any_of(ranges.begin(),ranges.end(),[&](const auto& range) {
                    return frame.timestampSeconds>=range.start-1e-6
                        && frame.timestampSeconds<=range.end+1e-6;
                });
                if(!needed)continue;
                const auto source=sourceNamespaces.find(window.sourceId);
                // No source-generation evidence means no persistent cache reuse.
                const auto key=pfservices::headOrientationCacheKey(window.sourceId,frame,source==sourceNamespaces.end() ? "uncached":source->second,identity,static_cast<int>(provider));
                const auto [entry,inserted]=lookup.try_emplace(key,requests.size());
                if(inserted)requests.push_back({window.sourceId,key,frame,{}});
                requests[entry->second].destinations.push_back({wi,slot});
            }
        }
        stats.requested=requests.size();
        std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b) {
            return a.source!=b.source ? a.source<b.source:a.frame.timestampSeconds<b.frame.timestampSeconds;
        });
        std::unique_ptr<pfgpu::FaceEstimator> faces;
        std::unique_ptr<pfgpu::HeadPoseEstimator> heads;
        stats.preparationMicros=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-preparationStarted).count());
        pfcore::VideoDecoder decoder;pfcore::DecodedFrame retained;
        std::string opened;double decodedTime=-INFINITY;bool converted=false;
        options.maxWidth=1280;options.maxHeight=720;
        std::size_t done=0;
        for(const auto& request:requests) {
            if(cancelled())break;
            pfcore::HeadOrientationSample sample{request.frame.timestampSeconds,{},false};
            bool fromCache=false,cacheable=false;
            const bool sourceKnown=sourceNamespaces.contains(request.source);
            if(cache && sourceKnown) {
                const auto cacheReadStarted=std::chrono::steady_clock::now();
                if(const auto bytes=cache->get(request.key))
                    if(const auto cached=pfservices::deserializeHeadOrientationSample(*bytes,sample.time)) {sample=*cached;fromCache=true;++stats.cached;}
                stats.cacheMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-cacheReadStarted).count());
            }
            if(!fromCache) {
                const auto& points=request.frame.keypoints;
                if(points.size()>=3 && points[0].confidence>=.5 && points[1].confidence>=.5 && points[2].confidence>=.5
                    && std::isfinite(points[0].x+points[0].y+points[1].x+points[1].y+points[2].x+points[2].y)) {
                    const auto decodeStarted=std::chrono::steady_clock::now();
                    if(opened!=request.source) {
                        decoder.open(request.source,options);decoder.setRgbaMaxDimensions(1280,720);
                        opened=request.source;decodedTime=-INFINITY;converted=false;
                    }
                    if(!std::isfinite(decodedTime) || sample.time-decodedTime>4.) {
                        decoder.seek(sample.time);decodedTime=-INFINITY;converted=false;
                    }
                    while(decodedTime+1e-6<sample.time && !cancelled()) {
                        if(!decoder.readNext(retained,false))break;
                        decodedTime=retained.timestampSeconds;converted=false;++stats.decoded;
                    }
                    const double tolerance=std::max(.002,.25/std::max(1.,decoder.info().frameRate));
                    if(!cancelled() && std::abs(decodedTime-sample.time)<=tolerance
                        && (converted || decoder.convertCurrentFrameToRgba(retained))) {
                        converted=true;cacheable=true;
                        stats.decodeMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now()-decodeStarted).count());
                        const auto& nose=points[0];
                        const double eyeSpan=std::hypot(points[1].x-points[2].x,points[1].y-points[2].y);
                        const double radius=std::max(48.,eyeSpan*4.);
                        const pfgpu::ReIdImage view{retained.width,retained.height,retained.rgba.data(),
                            static_cast<float>(std::max(0.,nose.x-radius)),static_cast<float>(std::max(0.,nose.y-radius)),
                            static_cast<float>(std::min<double>(retained.width,nose.x+radius)),
                            static_cast<float>(std::min<double>(retained.height,nose.y+radius))};
                        const std::array<double,2> expected={nose.x,nose.y};
                        if(!faces) {
                            const auto loadStarted=std::chrono::steady_clock::now();
                            faces=std::make_unique<pfgpu::FaceEstimator>(pfcore::pathToUtf8(detector),std::string{},provider);
                            ++stats.modelInitializations;
                            stats.preparationMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now()-loadStarted).count());
                        }
                        const auto faceStarted=std::chrono::steady_clock::now();
                        const auto box=faces->locateFace(view,expected);
                        stats.faceMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now()-faceStarted).count());
                        if(box) {
                            const auto [x,y,w,h]=*box;
                            const pfgpu::ReIdImage crop{retained.width,retained.height,retained.rgba.data(),
                                static_cast<float>(std::max(0.,std::floor(x-.4*w))),
                                static_cast<float>(std::max(0.,std::floor(y-.4*h))),
                                static_cast<float>(std::min<double>(retained.width,std::ceil(x+1.4*w))),
                                static_cast<float>(std::min<double>(retained.height,std::ceil(y+1.12*h)))};
                            if(crop.right-crop.left>=16 && crop.bottom-crop.top>=16
                                && crop.right-crop.left<=(crop.bottom-crop.top)*8
                                && crop.bottom-crop.top<=(crop.right-crop.left)*8)
                            {
                                if(!heads) {
                                    const auto loadStarted=std::chrono::steady_clock::now();
                                    heads=std::make_unique<pfgpu::HeadPoseEstimator>(pfcore::pathToUtf8(model),provider);
                                    ++stats.modelInitializations;
                                    stats.preparationMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now()-loadStarted).count());
                                }
                                const auto orientationStarted=std::chrono::steady_clock::now();
                                if(const auto rotation=heads->infer(crop)) {sample.rotation=*rotation;sample.observed=true;}
                                stats.orientationMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now()-orientationStarted).count());
                            }
                        }
                    }
                } else cacheable=true;
                if(cache && sourceKnown && cacheable && !cancelled()) {
                    pendingCacheWrites[request.source].emplace_back(request.key,pfservices::serializeHeadOrientationSample(sample));
                }
            }
            stats.observed+=sample.observed;
            for(const auto& [wi,si]:request.destinations)windows[wi].headOrientations[si]=sample;
            if(progress)progress(++done,stats.requested);
        }
        if(cache)for(const auto& [source,entries]:pendingCacheWrites) {
            std::string error;
            const auto cacheWriteStarted=std::chrono::steady_clock::now();
            cache->putForSourceBatch(source,entries,error);
            stats.cacheMicros+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-cacheWriteStarted).count());
        }
    }catch(const std::exception& error) {
        stats.error=error.what();
        for(const auto& [wi,ranges]:rangesByWindow) { (void)ranges;windows[wi].headOrientations.clear(); }
    }
    return stats;
}
}
