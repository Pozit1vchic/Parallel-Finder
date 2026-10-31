#include <pfcore/PoseAssociation.hpp>
#include <pfcore/DetectorOutputReplay.hpp>
#include <gtest/gtest.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>

namespace {
pfcore::PoseFrame pose() {
    pfcore::PoseFrame result;result.timestampSeconds=1;
    for(int i=0;i<17;++i)result.keypoints.push_back({100.+i*10,100.+i*15,.9});
    return result;
}
TEST(PoseAssociation, CorrespondenceDoesNotDependOnDetectionOrder) {
    const auto expected=pose();auto displaced=expected;
    for(auto& point:displaced.keypoints)point.x+=50;
    const std::vector candidates{displaced,expected};
    const auto match=pfcore::associateObservedPose(expected,candidates,1280,720);
    ASSERT_TRUE(match);EXPECT_EQ(match->candidate,1U);EXPECT_EQ(match->commonJoints,17U);
    EXPECT_DOUBLE_EQ(match->normalizedMedianError,0);
}
TEST(DetectorOutputReplay, RecoversTheSameSparseObservationWithoutCallingItArticulatedPose) {
    auto expected=pose();for(auto& p:expected.keypoints)p.confidence=.01;
    expected.keypoints[5].confidence=.9;expected.keypoints[6].confidence=.9;
    auto other=expected;for(auto& p:other.keypoints)p.x+=50;
    const std::vector candidates{other,expected};
    EXPECT_FALSE(pfcore::associateObservedPose(expected,candidates,1280,720));
    const auto replay=pfcore::associateDetectorOutputReplay(expected,candidates,1280,720);
    ASSERT_TRUE(replay);EXPECT_EQ(replay->candidate,1U);EXPECT_DOUBLE_EQ(replay->maximumPointError,0);
}
TEST(DetectorOutputReplay, RequiresEveryRawTripleAndUniqueSameTimestampOutput) {
    const auto expected=pose();
    for(int mode=0;mode<7;++mode) {
        auto changed=expected;
        switch(mode) {
        case 0:changed.keypoints[16].x+=1;break;
        case 1:changed.keypoints[16].confidence-=.01;break;
        case 2:changed.timestampSeconds+=.01;break;
        case 3:changed.keypoints[0].x=std::numeric_limits<double>::quiet_NaN();break;
        case 4:changed.keypoints[0].confidence=1.1;break;
        case 5:changed.keypoints[0].x=3000;break;
        case 6:changed.keypoints.pop_back();break;
        }
        EXPECT_FALSE(pfcore::associateDetectorOutputReplay(expected,std::span(&changed,1),1280,720))<<mode;
    }
    EXPECT_FALSE(pfcore::associateDetectorOutputReplay(expected,std::vector{expected,expected},1280,720));
    auto degenerate=expected;for(auto& p:degenerate.keypoints)p={100,100,.9};
    EXPECT_FALSE(pfcore::associateDetectorOutputReplay(degenerate,std::span(&degenerate,1),1280,720));
}
TEST(PoseAssociation, RejectsAmbiguityMissingJointsAndDifferentTimestamps) {
    const auto expected=pose();auto other=expected;
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::vector{expected,other},1280,720));
    other.timestampSeconds+=.02;
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::span(&other,1),1280,720));
    other=expected;for(int i=5;i<17;++i)other.keypoints[i].confidence=0;
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::span(&other,1),1280,720));
    other=expected;for(auto& point:other.keypoints)point.x+=20;
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::span(&other,1),1280,720));
    other=expected;for(int i=5;i<17;++i)other.keypoints[i].x+=100;
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::span(&other,1),1280,720));
    other=expected;other.timestampSeconds=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(pfcore::associateObservedPose(expected,std::span(&other,1),1280,720));
}
TEST(PoseAssociation, ReplaysThirtyActualCacheAndSourceObservations) {
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-cache-pose-association.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto rows=QJsonDocument::fromJson(file.readAll()).object()["frames"].toArray();
    ASSERT_EQ(rows.size(),30);
    const auto read=[](const QJsonArray& points,double time) {
        pfcore::PoseFrame frame;frame.timestampSeconds=time;
        for(const auto value:points) {const auto p=value.toArray();frame.keypoints.push_back({p[0].toDouble(),p[1].toDouble(),p[2].toDouble()});}
        return frame;
    };
    for(const auto value:rows) {
        const auto row=value.toObject();SCOPED_TRACE(row["time"].toDouble());
        const auto observed=read(row["cached"].toArray(),row["time"].toDouble());
        std::vector<pfcore::PoseFrame> candidates;
        for(const auto candidate:row["detections"].toArray())candidates.push_back(read(candidate.toArray(),row["time"].toDouble()));
        const auto result=pfcore::associateObservedPose(observed,candidates,1280,720);
        ASSERT_TRUE(result);EXPECT_EQ(result->candidate,static_cast<std::size_t>(row["expected"].toInt()));
        if(candidates.size()==1) {
            candidates.push_back(candidates.front());
            EXPECT_FALSE(pfcore::associateObservedPose(observed,candidates,1280,720));
        }
    }
}
}
