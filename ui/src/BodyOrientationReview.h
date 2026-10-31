#pragma once
#include <pfcore/MotionMatcher.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <pfgpu/PoseEstimator.hpp>
#include <filesystem>
#include <functional>
#include <map>

namespace pfui {
struct BodyOrientationReviewStats {
    std::size_t requested=0,cached=0,observed=0,decoded=0,poseInferences=0,bodyInferences=0,rawReplays=0;
    std::string error;
};
BodyOrientationReviewStats reviewMatchedBodyOrientations(std::vector<pfcore::MotionWindow>& windows,
    const std::vector<pfcore::MotionMatch>& matches,pfgpu::PoseEstimator& pose,
    const std::filesystem::path& model,const std::filesystem::path& cacheDirectory,
    const std::map<std::string,std::string>& sourceNamespaces,pfgpu::Provider provider,
    pfcore::VideoDecodeOptions options,const std::function<bool()>& cancelled,
    const std::function<void(std::size_t,std::size_t)>& progress={});
}
