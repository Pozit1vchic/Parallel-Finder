#include <gtest/gtest.h>
#include <pfcore/SceneSequenceCache.hpp>
#include <limits>
#include <stdexcept>

TEST(SceneSequenceCache, ExactRoundTripIncludesEmptyShotsAndTiming) {
    pfcore::CachedSceneSequence a{42,1,2,std::vector<float>(1296,.123456789F),{1.05,1.3,1.55}};
    const std::vector<pfcore::CachedSceneSequence> input={a,{7,2,3,{},{}}};
    const auto bytes=pfcore::encodeSceneSequences(input);const auto decoded=pfcore::decodeSceneSequences(bytes);
    ASSERT_TRUE(decoded);ASSERT_EQ(decoded->size(),2U);
    EXPECT_EQ(decoded->front().pixels,a.pixels);EXPECT_EQ(decoded->front().times,a.times);
    EXPECT_EQ(decoded->front().scene,42U);EXPECT_EQ(decoded->front().start,1);
    EXPECT_TRUE(decoded->back().pixels.empty());
    EXPECT_EQ(bytes.size(),16+64+1296*sizeof(float)+3*sizeof(double));
}
TEST(SceneSequenceCache, RejectsTruncationOversizedCountsAndGarbage) {
    const std::vector<pfcore::CachedSceneSequence> input={{1,0,1,std::vector<float>(1296,.2F),{.1,.35,.6}}};
    const auto valid=pfcore::encodeSceneSequences(input);
    for(std::size_t length=0;length<valid.size();++length)
        EXPECT_FALSE(pfcore::decodeSceneSequences(std::span(valid).first(length)))<<length;
    auto bytes=valid;bytes.push_back(0);EXPECT_FALSE(pfcore::decodeSceneSequences(bytes));
    bytes=valid;bytes[0]^=1;EXPECT_FALSE(pfcore::decodeSceneSequences(bytes));
    bytes=valid;for(std::size_t i=40;i<48;++i)bytes[i]=255;
    EXPECT_FALSE(pfcore::decodeSceneSequences(bytes));
}
TEST(SceneSequenceCache, RejectsInvalidMeasurementsAndDuplicateShots) {
    pfcore::CachedSceneSequence a{1,0,1,std::vector<float>(1296,.2F),{.1,.35,.6}};
    EXPECT_THROW(pfcore::encodeSceneSequences(std::vector{a,a}),std::invalid_argument);
    a.pixels[0]=std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(pfcore::encodeSceneSequences(std::vector{a}),std::invalid_argument);
    a.pixels[0]=.2F;a.times[2]=1;
    EXPECT_THROW(pfcore::encodeSceneSequences(std::vector{a}),std::invalid_argument);
    a.times[2]=.2;
    EXPECT_THROW(pfcore::encodeSceneSequences(std::vector{a}),std::invalid_argument);
    a.times.clear();
    EXPECT_THROW(pfcore::encodeSceneSequences(std::vector{a}),std::invalid_argument);
}
