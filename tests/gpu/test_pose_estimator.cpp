#include <gtest/gtest.h>

#include <pfgpu/PoseEstimator.hpp>
#include <pfgpu/Inference.hpp>

#include <filesystem>
#include <vector>

TEST(PoseEstimator, RunsPinnedBatchOneAssetWhenAvailable)
{
    const std::filesystem::path model = R"(D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx)";
    if (!std::filesystem::is_regular_file(model))
        GTEST_SKIP() << "external model bundle is not installed";

    std::vector<std::uint8_t> rgba(640U * 640U * 4U, 0U);
    for (std::size_t pixel = 3; pixel < rgba.size(); pixel += 4) rgba[pixel] = 255U;
    pfgpu::PoseEstimatorParams params;
    params.provider = pfgpu::Provider::Cpu;
    params.profile = "b1";
    pfgpu::PoseEstimator estimator(model.string(), params);
    const pfgpu::PoseImage image{640, 640, rgba.data()};
    EXPECT_NO_THROW({
        const auto detections = estimator.infer(image);
        for (const auto& detection : detections) {
            EXPECT_GE(detection.confidence, params.confidenceThreshold);
            EXPECT_EQ(detection.keypoints.size(), params.keypointCount * 3U);
        }
    });
}

TEST(PoseEstimator, DescribesPinnedStaticShapes)
{
    const std::filesystem::path model = R"(D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx)";
    if (!std::filesystem::is_regular_file(model))
        GTEST_SKIP() << "external model bundle is not installed";

    pfgpu::SessionCache cache;
    const auto session = cache.getOrCreate(pfgpu::ModelRef::fromPath(model.string()),
                                            {pfgpu::Provider::Cpu, 0, "b1"});
    ASSERT_TRUE(session.ok) << session.error;
    const auto description = pfgpu::describeSession(session.handle);
    ASSERT_TRUE(description.ok) << description.error;
    ASSERT_EQ(description.input.shape, (std::vector<std::int64_t>{1, 3, 640, 640}));
    ASSERT_EQ(description.outputs.size(), 1U);
    const auto& output = description.outputs.front().shape;
    const bool endToEnd = output == std::vector<std::int64_t>{1, 300, 57};
    const bool externalNms = output == std::vector<std::int64_t>{1, 56, 8400};
    EXPECT_TRUE(endToEnd || externalNms)
        << "unexpected pinned pose output shape";
}
