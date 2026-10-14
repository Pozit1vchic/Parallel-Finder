#include <pfcore/PoseAssociation.hpp>
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
