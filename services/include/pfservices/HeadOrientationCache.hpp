#pragma once
#include <pfcore/MotionMatcher.hpp>
#include <optional>

namespace pfservices {
// Existing v1 contract, shared with read-only diagnostics. Provider is the
// persisted numeric provider code; it is not resolved or initialized here.
std::string headOrientationCacheKey(const std::string& source,const pfcore::PoseFrame& frame,
    const std::string& sourceNamespace,const std::string& modelIdentity,int providerCode);
std::optional<pfcore::HeadOrientationSample> deserializeHeadOrientationSample(
    const std::vector<std::uint8_t>& bytes,double expectedTime);
std::vector<std::uint8_t> serializeHeadOrientationSample(const pfcore::HeadOrientationSample& sample);
}
