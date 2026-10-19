#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <pfcore/MotionMatcher.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <pfgpu/Provider.hpp>

namespace pfui {
struct HeadOrientationReviewStats {
    std::size_t requested=0,cached=0,observed=0,decoded=0;
    std::uint64_t preparationMicros=0,cacheMicros=0;
    std::uint64_t decodeMicros=0,faceMicros=0,orientationMicros=0;
    std::string error;
};
// Runs only after matching, and measures only poses inside matched clip ranges;
// it cannot change identity admission, pair selection or the old pose cache.
// Source requests are ordered and shared across overlapping result intervals.
HeadOrientationReviewStats reviewMatchedHeadOrientations(
    std::vector<pfcore::MotionWindow>& windows,const std::vector<pfcore::MotionMatch>& matches,
    const std::filesystem::path& model,const std::filesystem::path& detector,
    const std::filesystem::path& cacheDirectory,const std::map<std::string,std::string>& sourceNamespaces,
    pfgpu::Provider provider,pfcore::VideoDecodeOptions options,
    const std::function<bool()>& cancelled,
    const std::function<void(std::size_t,std::size_t)>& progress);
}
