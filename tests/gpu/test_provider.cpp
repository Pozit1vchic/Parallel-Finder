#include <gtest/gtest.h>

#include <pfgpu/Provider.hpp>

#include <string>

namespace {

TEST(Provider, NamesAreStableStrings)
{
    // These strings live in settings.json and the CLI: changing them is a
    // breaking change for the user's saved configuration.
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Auto), "auto");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::TensorRt), "tensorrt");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Cuda), "cuda");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Dml), "dml");
    EXPECT_STREQ(pfgpu::providerName(pfgpu::Provider::Cpu), "cpu");
}

TEST(Provider, EpNamesMatchOnnxRuntimeSpelling)
{
    // Passed verbatim to OrtApi::SessionOptionsAppendExecutionProvider, so a
    // typo here would silently disable a provider instead of failing a build.
    EXPECT_STREQ(pfgpu::providerEpName(pfgpu::Provider::TensorRt), "TensorrtExecutionProvider");
    EXPECT_STREQ(pfgpu::providerEpName(pfgpu::Provider::Cuda), "CUDAExecutionProvider");
    EXPECT_STREQ(pfgpu::providerEpName(pfgpu::Provider::Dml), "DmlExecutionProvider");
    EXPECT_STREQ(pfgpu::providerEpName(pfgpu::Provider::Cpu), "CPUExecutionProvider");
    EXPECT_STREQ(pfgpu::providerEpName(pfgpu::Provider::Auto), "");
}

TEST(Provider, ParseIsCaseInsensitiveAndRejectsUnknown)
{
    EXPECT_EQ(pfgpu::parseProvider("CPU"), pfgpu::Provider::Cpu);
    EXPECT_EQ(pfgpu::parseProvider("Cuda"), pfgpu::Provider::Cuda);
    EXPECT_EQ(pfgpu::parseProvider("tensorrt"), pfgpu::Provider::TensorRt);
    EXPECT_EQ(pfgpu::parseProvider("DirectML"), pfgpu::Provider::Dml);
    EXPECT_EQ(pfgpu::parseProvider("auto"), pfgpu::Provider::Auto);
    // Aliases the UI may use in tooltips/CLI.
    EXPECT_EQ(pfgpu::parseProvider("trt"), pfgpu::Provider::TensorRt);
    EXPECT_EQ(pfgpu::parseProvider("dml"), pfgpu::Provider::Dml);

    EXPECT_FALSE(pfgpu::parseProvider("openvino").has_value());
    EXPECT_FALSE(pfgpu::parseProvider("").has_value());
}

TEST(Provider, FallbackOrderIsSpecFixed)
{
    // Spec section 1: TensorRT -> CUDA -> DirectML -> CPU.
    ASSERT_EQ(pfgpu::kFallbackOrder.size(), 4u);
    EXPECT_EQ(pfgpu::kFallbackOrder[0], pfgpu::Provider::TensorRt);
    EXPECT_EQ(pfgpu::kFallbackOrder[1], pfgpu::Provider::Cuda);
    EXPECT_EQ(pfgpu::kFallbackOrder[2], pfgpu::Provider::Dml);
    EXPECT_EQ(pfgpu::kFallbackOrder[3], pfgpu::Provider::Cpu);
}

TEST(Provider, ParseRoundTripsThroughNames)
{
    for (const pfgpu::Provider provider : pfgpu::kFallbackOrder) {
        const auto parsed = pfgpu::parseProvider(pfgpu::providerName(provider));
        ASSERT_TRUE(parsed.has_value()) << pfgpu::providerName(provider);
        EXPECT_EQ(*parsed, provider);
    }
    const auto auto_ = pfgpu::parseProvider(pfgpu::providerName(pfgpu::Provider::Auto));
    ASSERT_TRUE(auto_.has_value());
    EXPECT_EQ(*auto_, pfgpu::Provider::Auto);
}

} // namespace
