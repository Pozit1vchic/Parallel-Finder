#include <gtest/gtest.h>
#include <pfgpu/BodyOrientationEstimator.hpp>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>
#include <cmath>

TEST(BodyOrientation, BilinearRgbLetterboxMatchesReviewedOpenCvPixelsAndMirror) {
    std::vector<std::uint8_t> rgba(4*3*4,255);
    for(int y=0;y<3;++y)for(int x=0;x<4;++x)for(int c=0;c<3;++c)rgba[(y*4+x)*4+c]=(x*43+y*17+c*50)%256;
    const pfgpu::PoseImage image{4,3,rgba.data()};
    const auto input=pfgpu::prepareBodyOrientationInput(image),flipped=pfgpu::prepareBodyOrientationInput(image,true);
    EXPECT_EQ(input.tensor.shape,(std::vector<std::int64_t>{1,3,1024,1024}));EXPECT_EQ(input.padLeft,0);EXPECT_EQ(input.padTop,128);
    EXPECT_DOUBLE_EQ(input.scale,256);
    // OpenCV 4.x INTER_LINEAR reference; fixed-point implementations can differ
    // by one byte at a half-step. This is not an exact floating pixel claim.
    struct Pixel {int x,y,r,g,b;};
    for(const auto& p:std::vector<Pixel>{{0,0,0,50,100},{127,127,0,50,100},{255,255,30,80,130},
        {511,383,81,131,181},{512,384,81,132,181},{777,600,140,190,124},{1023,767,163,213,7}}) {
        const int expected[]={p.r,p.g,p.b};
        for(int c=0;c<3;++c) {
            const auto value=input.tensor.values[c*1024*1024+(p.y+128)*1024+p.x]*255;
            EXPECT_NEAR(value,expected[c],1.01);
            EXPECT_NEAR(value,flipped.tensor.values[c*1024*1024+(p.y+128)*1024+1023-p.x]*255,1.01);
        }
    }
    EXPECT_FLOAT_EQ(input.tensor.values[0],114.F/255.F);
    EXPECT_THROW(pfgpu::prepareBodyOrientationInput({0,3,rgba.data()}),std::invalid_argument);
    EXPECT_THROW(pfgpu::prepareBodyOrientationInput({4,3,nullptr}),std::invalid_argument);
}

TEST(BodyOrientation, DecoderUsesObjectnessNmsUnletterboxAndSignedMirrorYaw) {
    pfgpu::BodyOrientationInput input;input.sourceWidth=100;input.sourceHeight=50;input.scale=10;input.padTop=256;
    pfgpu::FloatTensor raw;raw.shape={1,4,7};raw.values={
        300,506,200,300,.9F,.9F,.25F, 300,506,200,300,.8F,.9F,.75F,
        800,506,200,300,.9F,.9F,.5F, 800,506,200,300,.9F,.1F,0};
    const auto detected=pfgpu::decodeBodyOrientationOutput(raw,input);ASSERT_EQ(detected.size(),2);
    EXPECT_NEAR(detected[0].detectionScore,.81,1e-6);EXPECT_DOUBLE_EQ(detected[0].left,20);EXPECT_DOUBLE_EQ(detected[0].right,40);
    EXPECT_DOUBLE_EQ(detected[0].top,10);EXPECT_DOUBLE_EQ(detected[0].bottom,40);EXPECT_DOUBLE_EQ(detected[0].yawDegrees,90);
    input.mirrored=true;const auto flipped=pfgpu::decodeBodyOrientationOutput(raw,input);
    EXPECT_DOUBLE_EQ(flipped[0].left,60);EXPECT_DOUBLE_EQ(flipped[0].right,80);EXPECT_DOUBLE_EQ(flipped[0].yawDegrees,-90);
    raw.values[6]=std::numeric_limits<float>::quiet_NaN();EXPECT_EQ(pfgpu::decodeBodyOrientationOutput(raw,input).size(),2);
    raw.shape[2]=8;EXPECT_THROW(pfgpu::decodeBodyOrientationOutput(raw,input),std::invalid_argument);
}

TEST(BodyOrientation, MutualAssociationMirrorAgreementAndCircularMeanRetainUnknowns) {
    const pfgpu::BodyOrientationRegion a{0,0,10,20},b{20,0,30,20};
    const pfgpu::BodyOrientationDetection x{0,0,10,20,.9,179},y{20,0,30,20,.9,-90};
    auto mirror=x;mirror.yawDegrees=-179;
    auto found=pfgpu::associateBodyOrientations({a,b},{x,y},{mirror,y});ASSERT_EQ(found.size(),2);
    ASSERT_TRUE(found[0].yawDegrees);EXPECT_NEAR(std::abs(*found[0].yawDegrees),180,1e-9);EXPECT_DOUBLE_EQ(found[0].mirrorDifferenceDegrees,2);
    ASSERT_TRUE(found[1].yawDegrees);EXPECT_DOUBLE_EQ(*found[1].yawDegrees,-90);
    found=pfgpu::associateBodyOrientations({a,a},{x},{mirror});
    for(const auto& value:found) {EXPECT_FALSE(value.yawDegrees);EXPECT_EQ(value.status,pfgpu::BodyYawStatus::AmbiguousAssociation);}
    mirror.yawDegrees=0;found=pfgpu::associateBodyOrientations({a},{x},{mirror});
    EXPECT_FALSE(found[0].yawDegrees);EXPECT_EQ(found[0].status,pfgpu::BodyYawStatus::MirrorDisagreement);
    found=pfgpu::associateBodyOrientations({a},{y},{y});EXPECT_FALSE(found[0].yawDegrees);
    found=pfgpu::associateBodyOrientations({a},{x,x},{x});EXPECT_FALSE(found[0].yawDegrees);
    auto invalid=a;invalid.right=std::numeric_limits<double>::quiet_NaN();
    found=pfgpu::associateBodyOrientations({invalid},{x},{x});EXPECT_EQ(found[0].status,pfgpu::BodyYawStatus::InvalidSubject);
}

TEST(BodyOrientation, FrozenRealBodyViewsReplayAssociationWithoutHidingUnresolvedCases) {
    QFile file(QStringLiteral(PF_TEST_FIXTURE_DIR "/body-yaw-observations.json"));ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto fixture=QJsonDocument::fromJson(file.readAll()).object();
    EXPECT_EQ(fixture.value("schema").toInt(),1);
    EXPECT_EQ(fixture.value("modelSha256").toString(),"3bdfefba3f0bbcd23454ceefaa3088c2e95a7b9d120502899f65249ae98e1c93");
    const auto rows=fixture.value("controls").toArray();ASSERT_EQ(rows.size(),11);
    int observed=0,unresolved=0;
    for(const auto row:rows) {
        const auto control=row.toObject();SCOPED_TRACE(control.value("id").toString().toStdString());
        const auto box=control.value("expectedBodyBox").toArray();ASSERT_EQ(box.size(),4);
        const pfgpu::BodyOrientationRegion subject{box[0].toDouble(),box[1].toDouble(),box[2].toDouble(),box[3].toDouble()};
        std::vector<std::vector<pfgpu::BodyOrientationDetection>> views;
        for(const auto list:control.value("detections").toArray()) {
            std::vector<pfgpu::BodyOrientationDetection> detections;
            for(const auto entry:list.toArray()) {
                const auto d=entry.toObject();const auto r=d.value("box").toArray();ASSERT_EQ(r.size(),4);
                detections.push_back({r[0].toDouble(),r[1].toDouble(),r[2].toDouble(),r[3].toDouble(),
                    d.value("detectionScore").toDouble(),d.value("yawDegrees").toDouble()});
            }
            views.push_back(std::move(detections));
        }
        ASSERT_EQ(views.size(),2);const auto result=pfgpu::associateBodyOrientations({subject},views[0],views[1]).front();
        EXPECT_EQ(result.yawDegrees.has_value(),control.value("expectedSupported").toBool());
        if(result.yawDegrees) {
            ++observed;const auto range=control.value("acceptableYawDegrees").toArray();ASSERT_EQ(range.size(),2);
            double yaw=*result.yawDegrees;if(yaw<range[0].toDouble())yaw+=360;
            EXPECT_GE(yaw,range[0].toDouble());EXPECT_LE(yaw,range[1].toDouble());
            EXPECT_GE(result.associationIou,.5);EXPECT_GE(result.associationMargin,.15);EXPECT_LE(result.mirrorDifferenceDegrees,25);
        } else ++unresolved;
    }
    // Unknowns remain misses of available body views, not relabelled correct
    // negatives or a claim that all eleven frames were measured successfully.
    EXPECT_EQ(observed,9);EXPECT_EQ(unresolved,2);
}
