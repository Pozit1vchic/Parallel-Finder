#include <pfservices/HeadOrientationCache.hpp>
#include <gtest/gtest.h>

TEST(HeadOrientationCache, RetainsTheExistingV1RequestKeyBytes) {
    const pfcore::PoseFrame frame{1.25,{{1,2,.75},{3,4,.5}}};
    const auto key=pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|100|200|generation=","model|detector=face",2);
    // SHA-256 of the independently enumerated canonical compact JSON fixture.
    EXPECT_EQ(key,"head-orientation-v1|PIL-resize256-center224|unaligned-yunet-nose|fp32|"
        "287960d7a7139119aeb4d772a91f667298758e1773ba81fad9e7043466df63e8");
    auto changed=frame;changed.keypoints[0].x+=.01;
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",changed,"clip.mp4|100|200|generation=","model|detector=face",2));
    changed=frame;changed.timestampSeconds+=.01;
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",changed,"clip.mp4|100|200|generation=","model|detector=face",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("other.mp4",frame,"clip.mp4|100|200|generation=","model|detector=face",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|101|200|generation=","model|detector=face",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|100|200|generation=reset","model|detector=face",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|100|200|generation=","different-model|detector=face",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|100|200|generation=","model|detector=different",2));
    EXPECT_NE(key,pfservices::headOrientationCacheKey("clip.mp4",frame,"clip.mp4|100|200|generation=","model|detector=face",4));
}

TEST(HeadOrientationCache, PreservesMeasuredRotationsAndExplicitObservationGaps) {
    const pfcore::HeadOrientationSample observed{1.25,{1,0,0,0,1,0,0,0,1},true};
    const auto bytes=pfservices::serializeHeadOrientationSample(observed);
    const auto restored=pfservices::deserializeHeadOrientationSample(bytes,1.25);
    ASSERT_TRUE(restored);EXPECT_TRUE(restored->observed);EXPECT_EQ(restored->rotation,observed.rotation);
    EXPECT_FALSE(pfservices::deserializeHeadOrientationSample(bytes,1.250001));
    auto missing=observed;missing.observed=false;
    const auto gap=pfservices::deserializeHeadOrientationSample(pfservices::serializeHeadOrientationSample(missing),1.25);
    ASSERT_TRUE(gap);EXPECT_FALSE(gap->observed);
    EXPECT_EQ(gap->rotation,(std::array<double,9>{}));
}

TEST(HeadOrientationCache, RejectsMalformedOrWronglyTypedObservedSamples) {
    for(const std::string json:{"not-json","{}","{\"time\":1.25,\"observed\":1}",
        "{\"time\":1.25,\"observed\":true,\"rotation\":[1,0,0]}",
        "{\"time\":1.25,\"observed\":true,\"rotation\":[1,0,0,0,1,0,0,0,\"x\"]}"}) {
        EXPECT_FALSE(pfservices::deserializeHeadOrientationSample({json.begin(),json.end()},1.25))<<json;
    }
}
