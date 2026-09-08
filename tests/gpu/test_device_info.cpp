#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>

#include <string>
#include <vector>

namespace {

TEST(Pfgpu, ProviderNamesAreStableStrings)
{
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Auto), "auto");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Cuda), "cuda");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Dml), "dml");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Cpu), "cpu");
}

TEST(Pfgpu, DefaultProviderIsCpuUntilStage1)
{
    EXPECT_EQ(pfgpu::defaultProvider(), pfgpu::Provider::Cpu);
}

TEST(Pfgpu, StubDeviceInfoListsCpu)
{
    const std::vector<pfgpu::DeviceInfo> devices = pfgpu::getDeviceInfo();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices.front().backend, "cpu");
}

// Version-agnostic ORT probe (LoadLibrary + OrtGetApiBase). Skipped when the
// runtime is not installed — the test never fails on a Base-only machine.
TEST(Pfgpu, OrtRuntimeProbeIsGraceful)
{
    if (!pfgpu::ortRuntimeAvailable()) {
        GTEST_SKIP() << "onnxruntime.dll not found on PATH (Base bundle)";
    }
    const std::string version = pfgpu::ortRuntimeVersion();
    ASSERT_FALSE(version.empty());
    // Semver-ish sanity: starts with a digit.
    EXPECT_GE(version.front(), '0');
    EXPECT_LE(version.front(), '9');
}

} // namespace
