#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <pfcore/MotionMatcher.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <pfgpu/PoseEstimator.hpp>

namespace pfui {
struct VideoActionReviewContext {
    std::filesystem::path poseModel,actionModel,cacheDirectory;
    std::map<std::string,std::string> sourceNamespaces;
    pfgpu::Provider poseProvider=pfgpu::Provider::Cpu,actionProvider=pfgpu::Provider::Cpu;
    std::string poseProfile="b1";
};
struct VideoActionReviewStats {
    std::size_t requested=0,cached=0,observed=0,decoded=0,cuts=0,unassociated=0;
    std::string error;
};
// Post-match classifier only. Re-detect the selected person at an original
// pose timestamp and check native cut continuity before evaluating its clip.
VideoActionReviewStats reviewMatchedVideoActions(std::vector<pfcore::MotionWindow>& windows,
    const std::vector<pfcore::MotionMatch>& matches,pfgpu::PoseEstimator& pose,
    const std::filesystem::path& poseModel,const std::filesystem::path& actionModel,
    const std::filesystem::path& cacheDirectory,const std::map<std::string,std::string>& sourceNamespaces,
    pfgpu::Provider provider,pfcore::VideoDecodeOptions options,
    const std::function<bool()>& cancelled,
    const std::function<void(std::size_t,std::size_t)>& progress={});
}
