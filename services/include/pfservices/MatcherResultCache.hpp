#pragma once
#include <pfcore/MotionMatcher.hpp>
#include <span>
#include <string_view>

namespace pfservices {
std::string matcherResultCacheKey(const std::array<std::uint8_t,32>& fingerprint,
                                 std::string_view observationDependencies);
// Raw search results only, before rank/classification/scene organization.
// The caller binds records to the exact ordered input fingerprint and versions
// search semantics separately from result-only classifiers.
std::vector<std::uint8_t> serializeMatcherResults(std::span<const pfcore::MotionMatch> matches);
bool deserializeMatcherResults(std::span<const std::uint8_t> bytes,
    const std::vector<pfcore::MotionWindow>& windows,std::vector<pfcore::MotionMatch>& matches);
}
