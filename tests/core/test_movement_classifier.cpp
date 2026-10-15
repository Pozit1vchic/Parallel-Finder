#include <gtest/gtest.h>

#include <pfcore/MovementClassifier.hpp>
#include <pfcore/HeadOrientation.hpp>
#include <pfcore/MotionRanker.hpp>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
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
double exclusiveFixtureEnd(const pfcore::MotionWindow& window,double inclusiveEnd)
{
    if(window.frames.size()<2 || std::abs(inclusiveEnd-window.frames.back().timestampSeconds)>1e-5)
        return inclusiveEnd;
    // These recordings store pose observations sampled at 6 FPS, with the
    // final included PTS as `end`. Use the next observation boundary at the
    // recorded cadence. No new pose is interpolated, and known shot bounds
    // remain authoritative if a future fixture provides them.
    const double period=window.frames.back().timestampSeconds
        -window.frames[window.frames.size()-2].timestampSeconds;
    const double next=inclusiveEnd+period;
    return window.sceneEndSeconds>=0 ? std::min(next,window.sceneEndSeconds):next;
}
TEST(MovementGrouping, IsolatedBoundaryPoseDoesNotScatterHeldScenesIntoMotionGroups)
{
    using G=pfcore::GestureClass;
    const pfcore::MovementClassifier classifier;
    for(const int index:{0,29}) {
        auto window=actionFixture();
        window.frames[index].keypoints[0].x+=.7;
        window.frames[index].keypoints[9].y-=2.;
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3).gesture,G::Static);
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,G::Static);
        // Missing lower-body observations still cannot establish full-body
        // stillness, even after removal of a single facial detection outlier.
        for(auto& frame:window.frames)
            for(int joint=7;joint<17;++joint)frame.keypoints[joint].confidence=.01;
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3).gesture,G::Unknown);
    }
}
TEST(MovementGrouping, ExclusiveClipEndCannotSupplyPoseOrMotionEvidence)
{
    const pfcore::MovementClassifier classifier;
    // Exactly three observed frames are sufficient for a stable held pose;
    // the exported half-open range below contains only two of them.
    auto window=actionFixture();
    window.frames.resize(3);
    for(auto& frame:window.frames)frame.keypoints[9].confidence=frame.keypoints[10].confidence=.01;
    EXPECT_EQ(classifier.classifyPoseForGrouping(window,0,.2,false),"body_pose");
    EXPECT_EQ(classifier.classifyPoseForGrouping(window,0,.21,false),"legs_straight");
    window=actionFixture();window.frames.resize(5);
    // A head/body action requires five measured temporal samples. The one
    // at the exclusive end cannot establish coverage missing from the clip.
    EXPECT_EQ(classifier.classifyForGrouping(window,0,.4).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(classifier.classifyForGrouping(window,0,.41).gesture,pfcore::GestureClass::Static);
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
TEST(MovementGrouping, WalkingEvidenceSurvivesHiddenFaceAndIndependentActions)
{
    const pfcore::MovementClassifier classifier;
    for(const bool hiddenFace:{false,true}) {
        auto window=actionFixture([&](auto& frame,double t){
            const double phase=std::sin(6*3.141592653589793*t);
            frame.keypoints[15].x+=2*phase;frame.keypoints[16].x-=2*phase;
            frame.keypoints[13].y+=.8*phase;frame.keypoints[14].y-=.8*phase;
            frame.keypoints[9].y-=3*t;
            if(hiddenFace)for(int i=0;i<5;++i)frame.keypoints[i].confidence=0;
            else frame.keypoints[0].x+=1.2*t;
        });
        const auto result=classifier.classifyForGrouping(window,0,3,false);
        EXPECT_EQ(result.gesture,pfcore::GestureClass::Mixed);
        auto expected=std::vector<pfcore::GestureClass>{pfcore::GestureClass::Raise,pfcore::GestureClass::Step};
        if(!hiddenFace)expected.push_back(pfcore::GestureClass::HeadTurn);
        EXPECT_EQ(result.actions,expected);
        for(auto& frame:window.frames)for(auto& joint:frame.keypoints){joint.x+=frame.timestampSeconds*20;joint.y-=frame.timestampSeconds*10;}
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).actions,expected);
        for(auto& frame:window.frames)for(int i:{13,14,15,16})frame.keypoints[i].confidence=0;
        const auto incomplete=classifier.classifyForGrouping(window,0,3,false);
        EXPECT_EQ(std::count(incomplete.actions.begin(),incomplete.actions.end(),pfcore::GestureClass::Step),0);
    }
}

TEST(MovementGrouping, RealWalkingTowardAwayAndIntoShopSurvivesHiddenFace)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-real-walking.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    auto cases=QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();
    ASSERT_EQ(cases.size(),3);
    QFile backPhase(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-back-walking-phase.json");
    ASSERT_TRUE(backPhase.open(QIODevice::ReadOnly));
    const auto backCases=QJsonDocument::fromJson(backPhase.readAll()).object()["cases"].toArray();
    ASSERT_EQ(backCases.size(),1);cases.append(backCases[0]);
    const pfcore::MovementClassifier classifier;
    for(const auto value:cases)for(const bool hideFace:{false,true})for(int transform=0;transform<4;++transform) {
        const auto example=value.toObject();pfcore::MotionWindow window;
        for(const auto f:example["frames"].toArray()) {
            const auto frame=f.toObject();pfcore::PoseFrame pose;pose.timestampSeconds=frame["time"].toDouble();
            for(const auto p:frame["points"].toArray()){const auto point=p.toArray();pose.keypoints.push_back({point[0].toDouble(),point[1].toDouble(),point[2].toDouble()});}
            if(hideFace)for(int joint=0;joint<5;++joint)pose.keypoints[joint].confidence=0;
            window.frames.push_back(pose);
        }
        ASSERT_GE(window.frames.size(),5);
        for(std::size_t frame=0;frame<window.frames.size();++frame) {
            const double t=double(frame)/window.frames.size();
            for(std::size_t joint=0;joint<window.frames[frame].keypoints.size();++joint) {
                auto& point=window.frames[frame].keypoints[joint];
                if(transform==1)point.x=-point.x;
                if(transform==2) {
                    const double angle=.18*t,scale=1+.3*t,x=point.x,y=point.y;
                    point.x=scale*(x*std::cos(angle)-y*std::sin(angle))+80*t;
                    point.y=scale*(x*std::sin(angle)+y*std::cos(angle))-40*t;
                }
                if(transform==3 && joint>=11)point.confidence=.1;
            }
        }
        // The recording stores observed PTS, including its last labelled
        // frame. Convert that inclusive observation range to the classifier's
        // half-open clip range without discarding a reviewed gait phase.
        const auto result=classifier.classifyForGrouping(window,window.frames.front().timestampSeconds,
            exclusiveFixtureEnd(window,window.frames.back().timestampSeconds),false);
        if(transform==3) {
            EXPECT_NE(result.gesture,pfcore::GestureClass::Step);
            EXPECT_EQ(std::find(result.actions.begin(),result.actions.end(),pfcore::GestureClass::Step),result.actions.end());
            continue;
        }
        EXPECT_TRUE(result.gesture==pfcore::GestureClass::Step ||
            std::find(result.actions.begin(),result.actions.end(),pfcore::GestureClass::Step)!=result.actions.end())
            << example["label"].toString().toStdString() << " hidden face=" << hideFace << " transform=" << transform;
        if(example["label"].toString().startsWith("turning")) {
            EXPECT_NE(std::find(result.actions.begin(),result.actions.end(),pfcore::GestureClass::Turn),result.actions.end());
        }
    }
}

TEST(MovementGrouping, PlantedFeetDuringRealShovelingAndThrowingAreNotWalking)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/batya-planted-feet-not-walking.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto cases=QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();
    ASSERT_EQ(cases.size(),2);
    const pfcore::MovementClassifier classifier;
    for(const auto value:cases)for(const bool hideFace:{false,true})for(int transform=0;transform<3;++transform) {
        const auto example=value.toObject();pfcore::MotionWindow window;
        for(const auto f:example["frames"].toArray()) {
            const auto frame=f.toObject();pfcore::PoseFrame pose;pose.timestampSeconds=frame["time"].toDouble();
            for(const auto p:frame["points"].toArray()){const auto point=p.toArray();pose.keypoints.push_back({point[0].toDouble(),point[1].toDouble(),point[2].toDouble()});}
            window.frames.push_back(pose);
        }
        ASSERT_GE(window.frames.size(),5);
        for(std::size_t index=0;index<window.frames.size();++index) {
            const double t=double(index)/window.frames.size();
            for(std::size_t joint=0;joint<window.frames[index].keypoints.size();++joint) {
                auto& point=window.frames[index].keypoints[joint];
                if(hideFace && joint<5)point.confidence=0;
                if(transform==1)point.x=-point.x;
                if(transform==2) {
                    const double angle=.18*t,scale=1+.3*t,x=point.x,y=point.y;
                    point.x=scale*(x*std::cos(angle)-y*std::sin(angle))+80*t;
                    point.y=scale*(x*std::sin(angle)+y*std::cos(angle))-40*t;
                }
            }
        }
        const auto result=classifier.classifyForGrouping(window,window.frames.front().timestampSeconds,window.frames.back().timestampSeconds,false);
        EXPECT_NE(result.gesture,pfcore::GestureClass::Step) << example["label"].toString().toStdString();
        EXPECT_NE(result.gesture,pfcore::GestureClass::HeadTurn) << example["label"].toString().toStdString();
        EXPECT_EQ(std::count(result.actions.begin(),result.actions.end(),pfcore::GestureClass::Step),0);
    }
}

TEST(MovementGrouping, SeparatesTorsoRotationFromUniformZoom)
{
    EXPECT_EQ(group(actionFixture([](auto& f,double t){f.keypoints[5].x+=1.4*t;f.keypoints[6].x-=1.4*t;})),pfcore::GestureClass::Turn);
}
TEST(MovementGrouping, SuddenLeftRightPoseSwapIsNotAContinuousBodyTurn)
{
    const auto window=actionFixture([](auto& frame,double t){
        if(t>=.5)for(const auto& pair:{std::pair{5,6},std::pair{7,8},std::pair{9,10},
            std::pair{11,12},std::pair{13,14},std::pair{15,16}})
            std::swap(frame.keypoints[pair.first],frame.keypoints[pair.second]);
    });
    const pfcore::MovementClassifier classifier;
    const auto result=classifier.classifyForGrouping(window,0,3,false);
    EXPECT_NE(result.gesture,pfcore::GestureClass::Turn);
    EXPECT_EQ(std::count(result.actions.begin(),result.actions.end(),pfcore::GestureClass::Turn),0);
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
    EXPECT_EQ(c.classifyPoseForGrouping(actionFixture(),0,3,false),"legs_straight");
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
TEST(MovementGrouping, RestingStraightArmsDoNotMasqueradeAsReaching)
{
    const pfcore::MovementClassifier classifier;
    for(const bool mirrored:{false,true}) {
        auto resting=actionFixture([&](auto& frame,double) {
            if(mirrored)for(auto& point:frame.keypoints)point.x=-point.x;
        });
        EXPECT_EQ(classifier.classifyPoseForGrouping(resting,0,3,false),"legs_straight");
        // A waist-up crop exposes resting arms but provides no evidence for
        // standing versus sitting. Keep that uncertainty rather than making
        // the resting straight elbow an editorial reaching category.
        for(auto& frame:resting.frames)
            for(int joint=11;joint<17;++joint)frame.keypoints[joint].confidence=.01;
        EXPECT_EQ(classifier.classifyPoseForGrouping(resting,0,3,false),"body_pose");
    }
}

TEST(MovementGrouping, ProfilePoseUsesAgreedOcclusionWithoutGuessingFromMissingEar)
{
    const pfcore::MovementClassifier classifier;
    auto profile=actionFixture([](auto& f,double) {
        f.keypoints[0].x=.7;f.keypoints[4].confidence=.05;
    });
    EXPECT_EQ(classifier.classifyPoseForGrouping(profile,0,3,true),"head_turned");
    auto mirrored=profile;
    for(auto& f:mirrored.frames) {
        for(auto& p:f.keypoints)p.x=-p.x;
        std::swap(f.keypoints[1],f.keypoints[2]);std::swap(f.keypoints[3],f.keypoints[4]);
        std::swap(f.keypoints[5],f.keypoints[6]);
    }
    EXPECT_EQ(classifier.classifyPoseForGrouping(mirrored,0,3,true),"head_turned");
    auto frontal=profile;for(auto& f:frontal.frames)f.keypoints[0].x=0;
    EXPECT_EQ(classifier.classifyPoseForGrouping(frontal,0,3,true),"head_pose");
    auto uncertain=profile;for(auto& f:uncertain.frames)f.keypoints[0].confidence=.4;
    EXPECT_EQ(classifier.classifyPoseForGrouping(uncertain,0,3,true),"head_pose");
    auto inconsistent=profile;for(auto& f:inconsistent.frames) {
        f.keypoints[4].confidence=1;f.keypoints[3].confidence=.05;
    }
    EXPECT_EQ(classifier.classifyPoseForGrouping(inconsistent,0,3,true),"head_pose");
    auto repeated=profile;for(auto& f:repeated.frames)f.timestampSeconds=0;
    EXPECT_EQ(classifier.classifyPoseForGrouping(repeated,0,3,true),"head_pose");
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
        const auto start=row["start"].toDouble();
        // Historical fixture endpoints name the last included sampled frame;
        // they are not the exclusive video boundary used by export/grouping.
        // Keep every labelled observation and every original expected class.
        const auto end=exclusiveFixtureEnd(window,row["end"].toDouble());
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
            const auto classification=classifier.classifyForGrouping(observed,0,3,true);
            const auto actual=classification.gesture;
            EXPECT_EQ(actual,!cropped && pitch ? pfcore::GestureClass::Mixed:pfcore::GestureClass::HeadTurn)
                <<"cropped="<<cropped<<" pitch="<<pitch<<" transform="<<transform;
            if(!cropped && pitch)
                EXPECT_EQ(classification.actions,(std::vector<pfcore::GestureClass>{pfcore::GestureClass::HeadTurn,pfcore::GestureClass::HeadTilt}));
            else EXPECT_TRUE(classification.actions.empty());
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
TEST(MovementGrouping, IndependentSimultaneousActionsDoNotHideBehindOneClass)
{
    const pfcore::MovementClassifier classifier;
    const auto source=actionFixture([](auto& frame,double t) {
        frame.keypoints[9].y-=3*t;
        frame.keypoints[0].x+=1.2*t;
    });
    for(int transform=0;transform<3;++transform) {
        auto window=source;
        for(auto& frame:window.frames) {
            const double t=frame.timestampSeconds/3;
            for(auto& p:frame.keypoints) {
                p.x=p.x*(1+t)+20*t;p.y=p.y*(1+t)-15*t;
                if(transform==1)p.x=-p.x;
                if(transform==2) {const double x=p.x;p.x=-p.y;p.y=x;}
            }
        }
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,pfcore::GestureClass::Mixed);
        const auto compound=classifier.classifyForGrouping(window,0,3,false);
        EXPECT_EQ(compound.actions,(std::vector<pfcore::GestureClass>{pfcore::GestureClass::Raise,pfcore::GestureClass::HeadTurn}));
        EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(compound,compound).actions,compound.actions);
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,pfcore::GestureClass::HeadTurn);
    }
    auto armOnly=actionFixture([](auto& frame,double t){frame.keypoints[9].y-=3*t;});
    EXPECT_EQ(classifier.classifyForGrouping(armOnly,0,3,false).gesture,pfcore::GestureClass::Raise);
    const auto compound=classifier.classifyForGrouping(source,0,3,false);
    const auto single=classifier.classifyForGrouping(armOnly,0,3,false);
    EXPECT_TRUE(pfcore::MovementClassifier::combineForGrouping(compound,single).actions.empty());
}
TEST(MovementGrouping, UnclassifiedVisibleHandActionCannotBecomeIsolatedHeadTurn)
{
    const pfcore::MovementClassifier classifier;
    auto window=actionFixture([](auto& frame,double t) {
        frame.keypoints[9].x+=3*t;
        frame.keypoints[9].y-=3*t;
        frame.keypoints[0].x+=1.2*t;
    });
    EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,pfcore::GestureClass::HeadTurn);
    for(auto& frame:window.frames)frame.keypoints[9].confidence=0;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,pfcore::GestureClass::HeadTurn);
}
TEST(MovementGrouping, PairFacetsIgnoreEnumerationOrderButRequireFiniteEvidence)
{
    using G=pfcore::GestureClass;
    pfcore::MovementClassification left,right;
    left.gesture=right.gesture=G::Mixed;
    left.gestureConfidence=right.gestureConfidence=.9;
    left.actions={G::Raise,G::HeadTurn};
    right.actions={G::HeadTurn,G::Raise,G::HeadTurn};
    const auto combined=pfcore::MovementClassifier::combineForGrouping(left,right);
    EXPECT_EQ(combined.actions,left.actions);
    EXPECT_EQ(combined.gesture,G::Mixed);
    right.actions={G::Lower,G::HeadTurn};
    EXPECT_TRUE(pfcore::MovementClassifier::combineForGrouping(left,right).actions.empty());
    for(double invalid:{std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity()}) {
        right=left;right.gestureConfidence=invalid;
        EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(left,right).gesture,G::Unknown);
        EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(right,left).gesture,G::Unknown);
    }
}
TEST(MovementGrouping, LearnedHeadEvidenceControlsHeadAndSeparatesIndependentArmMotion)
{
    const pfcore::MovementClassifier classifier;
    for(const bool turn:{false,true}) {
        auto window=actionFixture([](auto& frame,double t){frame.keypoints[9].y-=3*t;frame.keypoints[0].x+=t;});
        for(const auto& frame:window.frames) {
            const double angle=turn ? frame.timestampSeconds/3.*.8:0,c=std::cos(angle),s=std::sin(angle);
            window.headOrientations.push_back({frame.timestampSeconds,{c,0,s,0,1,0,-s,0,c},true});
        }
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,
            turn ? pfcore::GestureClass::HeadTurn:pfcore::GestureClass::Static);
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,
            turn ? pfcore::GestureClass::Mixed:pfcore::GestureClass::Raise);
        for(auto& sample:window.headOrientations)sample.observed=false;
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,pfcore::GestureClass::Unknown);
        // A missing face does not erase an independently measured raised arm.
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,pfcore::GestureClass::Raise);
    }
}
TEST(MovementGrouping, LearnedHeadMotionSubtractsObservedCameraRoll)
{
    const pfcore::MovementClassifier classifier;
    for(const bool nod:{false,true}) {
        auto window=actionFixture();
        for(auto& frame:window.frames) {
            const double t=frame.timestampSeconds/3,angle=t*.7,c=std::cos(angle),s=std::sin(angle);
            for(auto& p:frame.keypoints) {const double x=p.x,y=p.y;p.x=c*x-s*y;p.y=s*x+c*y;}
            const double pitch=nod ? .5*std::sin(std::numbers::pi*t):0,cp=std::cos(pitch),sp=std::sin(pitch);
            window.headOrientations.push_back({frame.timestampSeconds,{c,-s*cp,s*sp,s,c*cp,-c*sp,0,sp,cp},true});
        }
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,true).gesture,
            nod ? pfcore::GestureClass::HeadNod:pfcore::GestureClass::Static);
    }
}
TEST(MovementGrouping, IndependentArmMotionRetainsBothMeasuredHeadActions)
{
    using G=pfcore::GestureClass;
    auto window=actionFixture([](auto& frame,double t){frame.keypoints[9].y-=3*t;});
    for(const auto& frame:window.frames) {
        const double t=frame.timestampSeconds/3,yaw=.7*t,pitch=.5*t;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
        window.headOrientations.push_back({frame.timestampSeconds,
            {cy,sy*sp,sy*cp,0,cp,-sp,-sy,cy*sp,cy*cp},true});
    }
    const pfcore::MovementClassifier classifier;
    const auto head=classifier.classifyForGrouping(window,0,3,true);
    EXPECT_EQ(head.actions,(std::vector<G>{G::HeadTurn,G::HeadTilt}));
    const auto body=classifier.classifyForGrouping(window,0,3,false);
    EXPECT_EQ(body.gesture,G::Mixed);
    EXPECT_EQ(body.actions,(std::vector<G>{G::Raise,G::HeadTurn,G::HeadTilt}));
    EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(body,body).actions,body.actions);
    EXPECT_TRUE(pfcore::MovementClassifier::combineForGrouping(head,body).actions.empty());
}
TEST(MovementGrouping, LearnedOrientationDoesNotInheritContradictoryGeometricHeadFacets)
{
    using G=pfcore::GestureClass;
    const pfcore::MovementClassifier classifier;
    for(bool tilt:{false,true}) {
        auto window=actionFixture([](auto& frame,double t) {
            frame.keypoints[0].x+=.7*t;frame.keypoints[0].y+=.5*t;
            frame.keypoints[4].confidence=t<.2 ? 1:.05;
        });
        for(const auto& frame:window.frames) {
            const double angle=tilt ? frame.timestampSeconds/3*.5:0;
            const double c=std::cos(angle),s=std::sin(angle);
            window.headOrientations.push_back({frame.timestampSeconds,{1,0,0,0,c,-s,0,s,c},true});
        }
        const auto result=classifier.classifyForGrouping(window,0,3,false);
        EXPECT_EQ(result.gesture,tilt ? G::HeadTilt:G::Static);
        EXPECT_TRUE(result.actions.empty() || result.actions==(std::vector<G>{G::HeadTilt}));
        for(auto& sample:window.headOrientations)sample.observed=false;
        EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,G::Unknown);
    }
}
TEST(MovementGrouping, CroppedBodyRetainsMeasuredHeadMotionWithoutInventingStaticScene)
{
    using G=pfcore::GestureClass;
    const pfcore::MovementClassifier classifier;
    for(bool moving:{false,true}) {
        auto window=actionFixture();
        for(auto& frame:window.frames) {
            for(int joint=7;joint<17;++joint)frame.keypoints[joint].confidence=.01;
            const double angle=moving ? frame.timestampSeconds/3*.5:0,c=std::cos(angle),s=std::sin(angle);
            window.headOrientations.push_back({frame.timestampSeconds,{1,0,0,0,c,-s,0,s,c},true});
        }
        const auto result=classifier.classifyForGrouping(window,0,3,false);
        EXPECT_EQ(result.gesture,moving ? G::HeadTilt:G::Unknown);
        EXPECT_EQ(result.headOnlyEvidence,moving);
        EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(result,result).headOnlyEvidence,moving);
        if(moving) {
            // A visible, unclassified arm action cannot become a head-only label.
            for(auto& frame:window.frames)for(int joint:{7,9}) {
                frame.keypoints[joint].confidence=1;
                frame.keypoints[joint].x+=frame.timestampSeconds;
                frame.keypoints[joint].y+=frame.timestampSeconds;
            }
            EXPECT_EQ(classifier.classifyForGrouping(window,0,3,false).gesture,G::Unknown);
            EXPECT_FALSE(classifier.classifyForGrouping(window,0,3,false).headOnlyEvidence);
        }
    }
}
TEST(MovementGrouping, TrimUsesOnlyItsOwnMeasuredShoulderReference)
{
    auto window=actionFixture();
    for(auto& frame:window.frames) {
        const double angle=frame.timestampSeconds*.45,c=std::cos(angle),s=std::sin(angle);
        for(auto& p:frame.keypoints) {const double x=p.x,y=p.y;p.x=c*x-s*y;p.y=s*x+c*y;}
        if(frame.timestampSeconds>1.0)frame.keypoints[5].confidence=frame.keypoints[6].confidence=.01;
        window.headOrientations.push_back({frame.timestampSeconds,{c,-s,0,s,c,0,0,0,1},true});
    }
    const pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,1.0,true).gesture,pfcore::GestureClass::Static);
}
TEST(MovementGrouping, LearnedHeadCannotBorrowTheFirstFrameOfTheFollowingShot)
{
    auto window=actionFixture();window.frames.resize(5);
    for(const auto& f:window.frames)window.headOrientations.push_back({f.timestampSeconds,{1,0,0,0,1,0,0,0,1},true});
    window.sceneStartSeconds=0;window.sceneEndSeconds=.4;
    const pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,.4,true).gesture,pfcore::GestureClass::Unknown);
    window.sceneEndSeconds=.2;
    EXPECT_EQ(classifier.classifyPoseForGrouping(window,0,.2,true),"head_pose");
}
} // namespace
