#include <gtest/gtest.h>

#include <pfcore/MovementClassifier.hpp>
#include <pfcore/MotionRanker.hpp>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <functional>
#include <limits>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace {

pfcore::MotionWindow moving(const char* source, double dx, double dy)
{
    pfcore::MotionWindow window;
    window.sourceId = source;
    for (int frame = 0; frame < 6; ++frame) {
        const double t = static_cast<double>(frame) / 5.0;
        window.frames.push_back({t, {{0.0 + dx * t, 0.0 + dy * t},
                                     {10.0 + dx * t, 0.0 + dy * t},
                                     {5.0 + dx * t, 10.0 + dy * t},
                                     {3.0 + dx * t, 12.0 + dy * t},
                                     {7.0 + dx * t, 12.0 + dy * t},
                                     {2.0 + dx * t, 4.0 + dy * t},
                                     {8.0 + dx * t, 4.0 + dy * t},
                                     {1.0 + dx * t, 14.0 + dy * t},
                                     {9.0 + dx * t, 14.0 + dy * t},
                                     {0.0 + dx * t, 10.0 + dy * t},
                                     {10.0 + dx * t, 10.0 + dy * t}}});
    }
    return window;
}

TEST(MovementClassifier, LabelsHorizontalMotionDeterministically)
{
    const auto result = pfcore::MovementClassifier().classify(moving("a", 20.0, 0.0));
    EXPECT_EQ(result.direction, pfcore::MovementDirection::Right);
    EXPECT_STREQ(pfcore::MovementClassifier::directionName(result.direction), "right");
}

TEST(MovementClassifier, LabelsStaticMotion)
{
    const auto result = pfcore::MovementClassifier().classify(moving("a", 0.0, 0.0));
    EXPECT_EQ(result.direction, pfcore::MovementDirection::Static);
    EXPECT_EQ(result.gesture, pfcore::GestureClass::Static);
}

TEST(MovementClassifier, CachedCombinationPreservesAllClassificationEvidence)
{
    const pfcore::MovementClassifier classifier;
    const std::vector windows{moving("still",0,0), moving("right",20,0), moving("left",-20,0),
        moving("up",0,-15), moving("down",0,15), pfcore::MotionWindow{}};
    for (const auto& a : windows) for (const auto& b : windows) {
        const auto expected = classifier.classify(a,b);
        const auto actual = classifier.combineClassifications(classifier.classify(a),classifier.classify(b));
        EXPECT_EQ(actual.direction,expected.direction); EXPECT_EQ(actual.gesture,expected.gesture);
        EXPECT_DOUBLE_EQ(actual.directionConfidence,expected.directionConfidence);
        EXPECT_DOUBLE_EQ(actual.gestureConfidence,expected.gestureConfidence);
    }
}

TEST(MovementClassifier, DenseSelectionPerformance)
{
    if (!std::getenv("PF_BENCH_MOVEMENT")) GTEST_SKIP() << "Opt-in classification benchmark";
    std::vector<pfcore::MotionWindow> windows;
    for (int i=0;i<64;++i) windows.push_back(moving("benchmark",(i%3-1)*20,(i%5-2)*8));
    const pfcore::MovementClassifier classifier;
    constexpr std::size_t pairs=1000000;
    const auto run=[&](bool cached) {
        const auto start=std::chrono::steady_clock::now();
        std::vector<pfcore::MovementClassification> labels;
        if (cached) for (const auto& window:windows) labels.push_back(classifier.classify(window));
        double checksum=0;
        for(std::size_t i=0;i<pairs;++i) {
            const auto a=i%windows.size(),b=(i/windows.size())%windows.size();
            const auto value=cached ? classifier.combineClassifications(labels[a],labels[b]) : classifier.classify(windows[a],windows[b]);
            checksum+=static_cast<int>(value.direction)+static_cast<int>(value.gesture)+value.directionConfidence+value.gestureConfidence;
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return std::pair{checksum,ms};
    };
    const auto original=run(false),cached=run(true);
    EXPECT_DOUBLE_EQ(original.first,cached.first);
    std::printf("PF_CLASSIFICATION_BENCH pairs=%zu windows=%zu original_ms=%.3f cached_ms=%.3f speedup=%.2f\n",
        pairs,windows.size(),original.second,cached.second,original.second/cached.second);
}

TEST(MovementClassifier, ClassifiesOnlyTheMatchedTimeRange)
{
    auto window = moving("a", 20, 0);
    auto unrelated = window.frames.back();
    unrelated.timestampSeconds = 2;
    for (auto& point : unrelated.keypoints) point.x -= 100;
    window.frames.push_back(unrelated);
    pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classify(window).direction, pfcore::MovementDirection::Left);
    EXPECT_EQ(classifier.classify(window, 0, 1).direction, pfcore::MovementDirection::Right);
    EXPECT_EQ(classifier.classify(window, 3, 4).direction, pfcore::MovementDirection::Unknown);
}

TEST(MotionRanker, AddsLabelsAndStableScore)
{
    const auto left = moving("a", 20.0, 0.0);
    const auto right = moving("b", 20.0, 0.0);
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1; match.similarity = 0.9; match.durationSeconds = 1.0;
    std::vector<pfcore::MotionMatch> matches {match};
    pfcore::MotionRanker::rank(matches, {left, right});
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches.front().directionLabel, "right");
    EXPECT_GT(matches.front().rankScore, 0.0);
}

TEST(MotionRanker, RetainsClosedHandGestureButRejectsJitter)
{
    auto left = moving("a", 0.0, 0.0);
    // Raise and lower both hands: the endpoint pose is unchanged.
    const double lift[] = {0.0, 2.0, 4.0, 4.0, 2.0, 0.0};
    for (std::size_t i = 0; i < left.frames.size(); ++i) {
        left.frames[i].keypoints[9].y -= lift[i];
        left.frames[i].keypoints[10].y -= lift[i];
    }
    auto right = left;
    right.sourceId = "b";
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1;
    match.similarity = 0.9; match.durationSeconds = 1.0;
    std::vector<pfcore::MotionMatch> matches{match};
    pfcore::MotionRanker::rank(matches, {left, right});
    EXPECT_EQ(matches.size(), 1u);

    for (std::size_t i = 0; i < left.frames.size(); ++i) {
        left.frames[i].keypoints[9].y = 10.0 - lift[i] * 0.005;
        left.frames[i].keypoints[10].y = 10.0 - lift[i] * 0.005;
    }
    matches = {match};
    pfcore::MotionRanker::rank(matches, {left, left});
    EXPECT_TRUE(matches.empty());

    left = moving("a", 0.0, 0.0);
    left.frames[2].keypoints[9].y -= 4.0;
    left.frames[2].keypoints[10].y -= 4.0;
    matches = {match};
    pfcore::MotionRanker::rank(matches, {left, left});
    EXPECT_TRUE(matches.empty()) << "A single detection outlier is not a closed gesture";
}

pfcore::MotionWindow actionFixture(const std::function<void(pfcore::PoseFrame&,double)>& animate={})
{
    pfcore::MotionWindow w;
    for(int i=0;i<31;++i) {
        pfcore::PoseFrame f;f.timestampSeconds=i*.1;
        f.keypoints={{0,-2},{-.5,-2.5},{.5,-2.5},{-1,-2},{1,-2},
            {-2,0},{2,0},{-2,1.5},{2,1.5},{-2,3},{2,3},
            {-1,4},{1,4},{-1,6},{1,6},{-1,8},{1,8}};
        if(animate)animate(f,double(i)/30);
        w.frames.push_back(std::move(f));
    }
    return w;
}
pfcore::GestureClass group(const pfcore::MotionWindow& w) {
    return pfcore::MovementClassifier().classifyForGrouping(w,0,3).gesture;
}
TEST(MovementGrouping, SeparatesSingleArmRaisesLowersAndOppositeArmActions)
{
    const auto raise=actionFixture([](auto& f,double t){f.keypoints[9].y-=3*t;});
    EXPECT_EQ(group(raise),pfcore::GestureClass::Raise);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[10].y+=3*t;})),pfcore::GestureClass::Lower);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[9].y-=3*t;f.keypoints[10].y+=3*t;})),pfcore::GestureClass::Mixed);
}
TEST(MovementGrouping, DetectsClosedActionsAndSeparatesLateralWaves)
{
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[9].y-=3*std::sin(3.141592653589793*t);})),pfcore::GestureClass::RaiseLower);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[9].x+=2*std::sin(6*3.141592653589793*t);})),pfcore::GestureClass::Wave);
    // A lateral displacement without oscillation or elbow extension is not
    // sufficient evidence for a wave or a physical push.
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[9].x+=3*t;})),pfcore::GestureClass::Unknown);
}
TEST(MovementGrouping, RecognizesArmExtensionFlexionAndReturn)
{
    const auto reach=[](auto& f,double t) {
        f.keypoints[8]={2+1.5*t,1.5-.75*t,1};
        f.keypoints[10]={2+3*t,.5+t,1};
    };
    EXPECT_EQ(group(actionFixture(reach)),pfcore::GestureClass::Push);
    EXPECT_EQ(group(actionFixture([&](auto& f,double t){reach(f,1-t);})),pfcore::GestureClass::Pull);
    EXPECT_EQ(group(actionFixture([&](auto& f,double t){reach(f,std::sin(3.141592653589793*t));})),pfcore::GestureClass::ReachReturn);
}
TEST(MovementGrouping, SeparatesHeadTurnsTiltsAndNods)
{
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[0].x+=.7*t;})),pfcore::GestureClass::HeadTurn);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[0].y+=.7*t;})),pfcore::GestureClass::HeadTilt);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[0].y+=.7*std::sin(3.141592653589793*t);})),pfcore::GestureClass::HeadNod);
}
TEST(MovementGrouping, SeparatesCameraTransformsFromBodyActions)
{
    for(const bool movingArm:{false,true}) {
        auto w=actionFixture([&](auto& f,double t) {
            if(movingArm)f.keypoints[9].y-=3*t;
            const double angle=t*.5,zoom=1+t;
            for(auto& p:f.keypoints) {
                const auto x=p.x,y=p.y;
                p.x=zoom*(x*std::cos(angle)-y*std::sin(angle))+t*20;
                p.y=zoom*(x*std::sin(angle)+y*std::cos(angle))-t*15;
            }
        });
        EXPECT_EQ(group(w),movingArm ? pfcore::GestureClass::Raise:pfcore::GestureClass::Static);
    }
}
TEST(MovementGrouping, RequiresObservedLegArticulationForWalking)
{
    EXPECT_EQ(group(actionFixture([](auto& f,double t){for(auto& p:f.keypoints)p.x+=10*t;})),pfcore::GestureClass::Static);
    EXPECT_EQ(group(actionFixture([](auto& f,double t){
        const double phase=std::sin(6*3.141592653589793*t);
        f.keypoints[15].x+=2*phase;f.keypoints[16].x-=2*phase;
        f.keypoints[13].y+=.8*phase;f.keypoints[14].y-=.8*phase;
    })),pfcore::GestureClass::Step);
}
TEST(MovementGrouping, SeparatesTorsoRotationFromUniformZoom)
{
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[5].x+=1.4*t;f.keypoints[6].x-=1.4*t;})),pfcore::GestureClass::Turn);
}
TEST(MovementGrouping, RejectsOutliersMissingJointsAndInsufficientTemporalEvidence)
{
    auto w=actionFixture();w.frames[15].keypoints[9].y-=20;
    EXPECT_EQ(group(w),pfcore::GestureClass::Static);
    w=actionFixture([](auto& f,double t){f.keypoints[9].y-=3*t;f.keypoints[9].confidence=.1;});
    EXPECT_NE(group(w),pfcore::GestureClass::Raise);
    w=actionFixture([](auto& f,double){for(std::size_t i=7;i<17;++i)f.keypoints[i].confidence=.01;});
    EXPECT_EQ(group(w),pfcore::GestureClass::Unknown)<<"Hidden body motion cannot be declared static";
    w=actionFixture();for(auto& f:w.frames)for(auto& p:f.keypoints)p.confidence=0;
    EXPECT_EQ(group(w),pfcore::GestureClass::Unknown);
    w=actionFixture();w.frames.resize(4);EXPECT_EQ(group(w),pfcore::GestureClass::Unknown);
    w=actionFixture();w.frames[10].timestampSeconds=w.frames[9].timestampSeconds;
    EXPECT_EQ(group(w),pfcore::GestureClass::Unknown);
    w=actionFixture();for(auto& f:w.frames)f.keypoints[5].x=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(group(w),pfcore::GestureClass::Unknown);
}
TEST(MovementGrouping, RestrictsEvidenceToMatchedIntervalAndAbstainsOnPairDisagreement)
{
    auto w=actionFixture([](auto& f,double t){if(t>.6)f.keypoints[9].y-=9*(t-.6);});
    const pfcore::MovementClassifier c;
    EXPECT_EQ(c.classifyForGrouping(w,0,1.5).gesture,pfcore::GestureClass::Static);
    EXPECT_EQ(c.classifyForGrouping(w,1.8,3).gesture,pfcore::GestureClass::Raise);
    const auto known=c.classifyForGrouping(w,1.8,3);
    EXPECT_EQ(c.combineForGrouping(known,{}).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(c.classifyForGrouping(w,5,6).gesture,pfcore::GestureClass::Unknown);
}
TEST(MovementGrouping, RecordedCroppedHeadNeverBorrowsInvisibleHandEvidence)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/steve-head-motion-window.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    for(const auto value:QJsonDocument::fromJson(file.readAll()).array()) {
        pfcore::MotionWindow w;
        for(const auto frame:value.toObject()["frames"].toArray()) {
            const auto o=frame.toObject();pfcore::PoseFrame f;f.timestampSeconds=o["time"].toDouble();
            for(const auto p:o["points"].toArray()) {
                const auto a=p.toArray();f.keypoints.push_back({a[0].toDouble(),a[1].toDouble(),a[2].toDouble()});
            }
            w.frames.push_back(std::move(f));
        }
        ASSERT_FALSE(w.frames.empty());
        const auto result=pfcore::MovementClassifier().classifyForGrouping(w,w.frames.front().timestampSeconds,w.frames.back().timestampSeconds);
        EXPECT_NE(result.gesture,pfcore::GestureClass::Raise);EXPECT_NE(result.gesture,pfcore::GestureClass::Lower);
        EXPECT_NE(result.gesture,pfcore::GestureClass::Push);EXPECT_NE(result.gesture,pfcore::GestureClass::Pull);
        EXPECT_NE(result.gesture,pfcore::GestureClass::Wave);
    }
}
TEST(MovementGrouping, GroupsObservedPosesWithoutInventingHiddenBodyPosture)
{
    const pfcore::MovementClassifier c;
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture(),0,3,true),"head_front");
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture([](auto& f,double){f.keypoints[0].x=.7;}),0,3,true),"head_turned");
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture([](auto& f,double){f.keypoints[9].y=-2;}),0,3,false),"arms_raised");
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture([](auto& f,double){
        f.keypoints[7]={-2,1,1};f.keypoints[8]={2,1,1};
        f.keypoints[9]={2,1,1};f.keypoints[10]={-2,1,1};
    }),0,3,false),"arms_folded");
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture([](auto& f,double){
        f.keypoints[8]={3.5,.75,1};f.keypoints[10]={5,1.5,1};
    }),0,3,false),"arms_extended");
    auto legs=actionFixture([](auto& f,double){f.keypoints[7].confidence=f.keypoints[8].confidence=0;});
    EXPECT_EQ(c.classifyPoseForGrouping(legs,0,3,false),"legs_straight");
    for(auto& f:legs.frames) {f.keypoints[15]={-3,6,1};f.keypoints[16]={3,6,1};}
    EXPECT_EQ(c.classifyPoseForGrouping(legs,0,3,false),"legs_bent");
    auto cropped=actionFixture([](auto& f,double){for(std::size_t i=7;i<17;++i)f.keypoints[i].confidence=.01;});
    EXPECT_EQ(c.classifyPoseForGrouping(cropped,0,3,false),"body_pose");
    auto inconsistent=actionFixture([](auto& f,double t){if(t<.5)f.keypoints[9].y=-2;});
    EXPECT_EQ(c.classifyPoseForGrouping(inconsistent,0,3,false),"body_pose");
}
TEST(MovementGrouping, RemainsStableUnderNoiseMirroringAndPartialReferenceDropout)
{
    for(int variant=0;variant<24;++variant) {
        auto w=actionFixture([&](auto& f,double t) {
            f.keypoints[9].y-=3*t;
            const double scale=1+variant*.1;
            for(std::size_t i=0;i<f.keypoints.size();++i) {
                auto& p=f.keypoints[i];
                p.x=scale*(p.x+.015*std::sin(i+variant+t*20));
                p.y=scale*(p.y+.015*std::cos(i+variant+t*17));
                if(variant%2)p.x=-p.x;
            }
        });
        // Fewer than a quarter of frames lose hip observations; don't switch
        // coordinate systems mid-trajectory when detection confidence drops.
        for(std::size_t i=5;i<8;++i)w.frames[i].keypoints[11].confidence=.1;
        EXPECT_EQ(group(w),pfcore::GestureClass::Raise)<<variant;
    }
}
TEST(MovementGrouping, ReplaysManuallyReviewedElliotAndSoldierScenepacks)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/scenepack-movement-grouping.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto cases=QJsonDocument::fromJson(file.readAll()).array();
    ASSERT_EQ(cases.size(),11);
    const pfcore::MovementClassifier classifier;
    for(const auto value:cases) {
        const auto row=value.toObject();SCOPED_TRACE(row["name"].toString().toStdString());
        pfcore::MotionWindow window;
        for(const auto valueFrame:row["frames"].toArray()) {
            const auto frame=valueFrame.toObject();pfcore::PoseFrame f;
            f.timestampSeconds=frame["time"].toDouble();
            for(const auto p:frame["points"].toArray()) {
                const auto a=p.toArray();f.keypoints.push_back({a[0].toDouble(),a[1].toDouble(),a[2].toDouble()});
            }
            window.frames.push_back(std::move(f));
        }
        ASSERT_GE(window.frames.size(),5U);
        const auto start=row["start"].toDouble(),end=row["end"].toDouble();
        const bool head=row["headOnly"].toBool();
        for(int transform=0;transform<3;++transform) {
            auto transformed=window;
            if(transform)for(auto& frame:transformed.frames)for(auto& p:frame.keypoints) {
                p.x=(transform==1 ? -1.7:1.7)*p.x+100;
                p.y=1.7*p.y-50;
            }
            const auto actual=row["pose"].toBool()
                ? classifier.classifyPoseForGrouping(transformed,start,end,head)
                : pfcore::MovementClassifier::gestureName(classifier.classifyForGrouping(transformed,start,end,head).gesture);
            EXPECT_EQ(actual,row["expected"].toString().toStdString())<<transform;
        }
    }
}
TEST(MovementGrouping, RecordedHeadTurnRemainsRecognizableWhenAnEarBecomesOccluded)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-head-turn-occlusion.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto row=QJsonDocument::fromJson(file.readAll()).object();
    pfcore::MotionWindow window;
    for(const auto value:row["frames"].toArray()) {
        const auto source=value.toObject();pfcore::PoseFrame frame;
        frame.timestampSeconds=source["time"].toDouble();
        for(const auto point:source["points"].toArray()) {
            const auto p=point.toArray();frame.keypoints.push_back({p[0].toDouble(),p[1].toDouble(),p[2].toDouble()});
        }
        window.frames.push_back(std::move(frame));
    }
    ASSERT_EQ(window.frames.size(),14U);
    const pfcore::MovementClassifier classifier;
    for(int variant=0;variant<3;++variant) {
        auto transformed=window;
        for(auto& frame:transformed.frames)for(auto& p:frame.keypoints) {
            p.x=(variant==1 ? -1.7:1.7)*p.x+100;
            p.y=1.7*p.y-50;
        }
        for(const bool headOnly:{false,true})
            EXPECT_EQ(classifier.classifyForGrouping(transformed,row["start"].toDouble(),row["end"].toDouble(),headOnly).gesture,
                pfcore::GestureClass::HeadTurn)<<variant<<headOnly;
    }
    // Ear detector confidence alone cannot establish an action. Preserve the
    // same occlusion timeline but hold all facial positions at the first frame.
    auto confidenceDropOnly=window;
    for(auto& frame:confidenceDropOnly.frames)for(std::size_t j=0;j<5;++j) {
        frame.keypoints[j].x=window.frames.front().keypoints[j].x;
        frame.keypoints[j].y=window.frames.front().keypoints[j].y;
    }
    EXPECT_NE(classifier.classifyForGrouping(confidenceDropOnly,row["start"].toDouble(),row["end"].toDouble(),true).gesture,
        pfcore::GestureClass::HeadTurn);
}
TEST(MovementGrouping, UncertainHeadActionDoesNotBorrowTheFollowingShot)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-compound-head-action.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto row=QJsonDocument::fromJson(file.readAll()).object();
    pfcore::MotionWindow window;
    for(const auto value:row["frames"].toArray()) {
        const auto source=value.toObject();pfcore::PoseFrame frame;
        frame.timestampSeconds=source["time"].toDouble();
        for(const auto point:source["points"].toArray()) {
            const auto p=point.toArray();frame.keypoints.push_back({p[0].toDouble(),p[1].toDouble(),p[2].toDouble()});
        }
        window.frames.push_back(std::move(frame));
    }
    ASSERT_GE(window.frames.size(),5U);
    const pfcore::MovementClassifier classifier;
    for(const bool headOnly:{false,true})
        EXPECT_EQ(classifier.classifyForGrouping(window,row["start"].toDouble(),row["end"].toDouble(),headOnly).gesture,
            pfcore::GestureClass::Unknown);
}
TEST(MovementGrouping, CroppedYawDoesNotInventNodFromNarrowingShoulders)
{
    const pfcore::MovementClassifier classifier;
    for(const bool pitch:{false,true}) {
        auto window=actionFixture([&](auto& frame,double t) {
            frame.keypoints[0].x+=.7*t;
            if(pitch)frame.keypoints[0].y+=.5*t;
            frame.keypoints[4].confidence=t<.2 ? 1:.05;
            frame.keypoints[5].x+=1.4*t;
            frame.keypoints[6].x-=1.4*t;
        });
        for(const bool cropped:{false,true})for(int transform=0;transform<3;++transform) {
            auto observed=window;
            for(auto& frame:observed.frames) {
                if(cropped)for(std::size_t joint=7;joint<17;++joint)
                    frame.keypoints[joint].confidence=.01;
                const double t=frame.timestampSeconds/3;
                const double zoom=1+t,angle=transform==2 ? t*.4:0;
                for(auto& point:frame.keypoints) {
                    const double x=point.x,y=point.y;
                    point.x=zoom*(x*std::cos(angle)-y*std::sin(angle))+20*t;
                    point.y=zoom*(x*std::sin(angle)+y*std::cos(angle))-15*t;
                    if(transform==1)point.x=-point.x;
                }
            }
            const auto actual=classifier.classifyForGrouping(observed,0,3,true).gesture;
            EXPECT_EQ(actual,!cropped && pitch ? pfcore::GestureClass::Mixed:pfcore::GestureClass::HeadTurn)
                <<"cropped="<<cropped<<" pitch="<<pitch<<" transform="<<transform;
        }
    }
}
TEST(MovementGrouping, RejectsIntervalsCrossingKnownSceneBounds)
{
    auto window=actionFixture([](auto& f,double t){if(t>=.5)f.keypoints[9].y-=3*t;});
    window.sceneStartSeconds=0;
    window.sceneEndSeconds=1.5;
    const pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,3).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(classifier.classifyPoseForGrouping(window,0,3,false),"body_pose");
    EXPECT_EQ(classifier.classifyForGrouping(window,1.5,3,true).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(classifier.classifyPoseForGrouping(window,1.5,3,true),"head_pose");
    // The first frame of the next shot is excluded even when the requested
    // end equals the known boundary. It cannot supply the fifth observation.
    window=actionFixture();window.frames.resize(5);
    window.sceneStartSeconds=0;
    window.sceneEndSeconds=window.frames.back().timestampSeconds;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,window.sceneEndSeconds).gesture,pfcore::GestureClass::Unknown);
}
} // namespace
