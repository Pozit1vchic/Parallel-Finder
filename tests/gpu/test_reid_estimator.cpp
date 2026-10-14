#include <gtest/gtest.h>
#include <pfgpu/ReIdEstimator.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <filesystem>
#include <chrono>
#include <array>
#include <limits>
#include "../helpers/onnx_identity.hpp"

TEST(ReIdEstimator, RefreshesDimensionsAndLayoutAfterModelReplacement)
{
    if (!pfgpu::ortApi()) GTEST_SKIP() << "ONNX Runtime unavailable";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("identity.onnx");
    auto timestamp = std::filesystem::file_time_type::clock::now();
    const auto replace = [&](std::initializer_list<std::uint64_t> dimensions) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const auto model = pftest::identityModel(dimensions);
        const bool written = file.write(model.data(), static_cast<qint64>(model.size())) == static_cast<qint64>(model.size());
        file.close();
        timestamp += std::chrono::seconds(1);
        std::filesystem::last_write_time(std::filesystem::path(path.toStdWString()), timestamp);
        return written;
    };
    ASSERT_TRUE(replace({1, 3, 4, 4}));
    pfgpu::ReIdEstimatorParams params;
    params.provider = pfgpu::Provider::Cpu;
    params.intraOpThreads = 1;
    pfgpu::ReIdEstimator estimator(path.toStdString(), params);
    std::vector<std::uint8_t> rgba(8 * 8 * 4, 127);
    const pfgpu::ReIdImage image{8, 8, rgba.data(), 0, 0, 8, 8};
    ASSERT_EQ(estimator.infer(image).size(), 48);
    ASSERT_TRUE(replace({1, 3, 8, 4}));
    ASSERT_EQ(estimator.infer(image).size(), 96);
    ASSERT_TRUE(replace({1, 4, 8, 3})); // NHWC, same element count
    ASSERT_EQ(estimator.infer(image).size(), 96);
    EXPECT_EQ(estimator.infer(image).size(), 96); // stable metadata cache hit
}

TEST(ReIdEstimator, InvalidPersonBoxesCannotBecomeAnEdgePixelIdentity)
{
    if (!pfgpu::ortApi()) GTEST_SKIP() << "ONNX Runtime unavailable";
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto path=directory.filePath("identity.onnx");QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const auto model=pftest::identityModel({1,3,4,4});
    ASSERT_EQ(file.write(model.data(),static_cast<qint64>(model.size())),static_cast<qint64>(model.size()));file.close();
    pfgpu::ReIdEstimatorParams params;params.provider=pfgpu::Provider::Cpu;params.intraOpThreads=1;
    pfgpu::ReIdEstimator estimator(path.toStdString(),params);
    std::vector<std::uint8_t> rgba(8*8*4,127);
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const auto offset=(y*8+x)*4;
        rgba[offset]=x*25;rgba[offset+1]=y*25;rgba[offset+2]=(x+y)*12;rgba[offset+3]=255;
    }
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(const auto& box:std::vector<std::array<float,4>>{
        {4,0,4,8},{6,0,2,8},{0,4,8,4},{0,6,8,2},
        {8,0,10,8},{0,8,8,10},{-4,0,0,8},{0,-4,8,0},
        {nan,0,8,8},{0,nan,8,8},{0,0,nan,8},{0,0,8,nan},
        {-inf,0,8,8},{0,0,inf,8}}) {
        const pfgpu::ReIdImage image{8,8,rgba.data(),box[0],box[1],box[2],box[3]};
        EXPECT_THROW(estimator.infer(image),std::invalid_argument);
    }
    // Legitimate crops partly outside the frame retain the same clipped input.
    const auto clipped=estimator.infer({8,8,rgba.data(),0,0,4,6});
    EXPECT_EQ(estimator.infer({8,8,rgba.data(),-4,-2,4,6}),clipped);
}
