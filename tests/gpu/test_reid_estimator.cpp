#include <gtest/gtest.h>
#include <pfgpu/ReIdEstimator.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <filesystem>
#include <chrono>
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
