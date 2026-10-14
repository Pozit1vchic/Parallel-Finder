#include <pfgpu/FaceEstimator.hpp>
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <QTemporaryDir>

TEST(FaceInput, InvalidGeometryCannotLoadModelsOrRetainPreviousIdentity)
{
    // These paths deliberately do not exist: invalid observations must be
    // refused before loading a model, not interpreted as another face.
    pfgpu::FaceEstimator estimator("invalid-input-detector.onnx","invalid-input-recognizer.onnx",pfgpu::Provider::Cpu);
    std::vector<std::uint8_t> rgba(64*64*4,127);
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(const auto& box:std::vector<std::array<float,4>>{
        {nan,0,64,64},{0,nan,64,64},{0,0,nan,64},{0,0,64,nan},
        {-inf,0,64,64},{0,0,inf,64},{32,0,32,64},{48,0,16,64},
        {0,48,64,16},{64,0,96,64},{-64,0,0,64}}) {
        const pfgpu::ReIdImage image{64,64,rgba.data(),box[0],box[1],box[2],box[3]};
        pfgpu::FaceRecognitionDiagnostics diagnostic;diagnostic.accepted=diagnostic.faceDetected=true;
        diagnostic.detectorScore=1;
        EXPECT_TRUE(estimator.infer(image,&diagnostic).empty());
        EXPECT_FALSE(diagnostic.accepted);EXPECT_FALSE(diagnostic.faceDetected);
        EXPECT_EQ(diagnostic.detectorScore,0);
        EXPECT_FALSE(estimator.locateFace(image,{32,32}));
    }
}

TEST(FaceInput, InvalidConfidenceAndNoseHintsCannotBypassDetectorChecks)
{
    pfgpu::FaceEstimator estimator("invalid-input-detector.onnx","invalid-input-recognizer.onnx",pfgpu::Provider::Cpu);
    std::vector<std::uint8_t> rgba(64*64*4,127);
    const pfgpu::ReIdImage image{64,64,rgba.data(),0,0,64,64};
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    for(double confidence:{nan,inf,-.1,1.1})
        EXPECT_TRUE(estimator.infer(image,nullptr,confidence).empty());
    for(const auto& hint:std::vector<std::array<double,2>>{{nan,32},{32,nan},{inf,32},{32,-inf}}) {
        EXPECT_TRUE(estimator.infer(image,nullptr,.85,&hint).empty());
        EXPECT_FALSE(estimator.locateFace(image,hint));
    }
}

TEST(FaceInput, ValidInputStillReachesTheDetector)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfgpu::FaceEstimator estimator(directory.filePath("missing-detector.onnx").toStdString(),
        directory.filePath("missing-recognizer.onnx").toStdString(),pfgpu::Provider::Cpu);
    std::vector<std::uint8_t> rgba(64*64*4,127);
    const pfgpu::ReIdImage image{64,64,rgba.data(),0,0,64,64};
    EXPECT_THROW(estimator.infer(image),std::runtime_error);
    EXPECT_THROW(estimator.locateFace(image,{32,32}),std::runtime_error);
}
