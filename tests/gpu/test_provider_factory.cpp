#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <pfgpu/ProviderFactory.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

// A factory for a provider ORT does not know: registration must be the only
// step needed, which is the extensibility the spec demands (section 2).
class FakeProviderFactory final : public pfgpu::IProviderFactory {
public:
    explicit FakeProviderFactory(pfgpu::Provider provider, std::string epName)
        : provider_(provider)
        , epName_(std::move(epName))
    {
    }

    pfgpu::Provider provider() const noexcept override { return provider_; }
    const char* epName() const noexcept override { return epName_.c_str(); }

private:
    pfgpu::Provider provider_;
    std::string epName_;
};

class ProviderFactoryTest : public ::testing::Test {
protected:
    void TearDown() override { pfgpu::resetProviderFactoriesForTesting(); }
};

TEST_F(ProviderFactoryTest, BuiltinsCoverTheWholeFallbackChain)
{
    for (const pfgpu::Provider provider : pfgpu::kFallbackOrder) {
        const pfgpu::IProviderFactory* factory = pfgpu::findProviderFactory(provider);
        ASSERT_NE(factory, nullptr) << pfgpu::providerName(provider);
        EXPECT_STREQ(factory->epName(), pfgpu::providerEpName(provider));
    }
    EXPECT_EQ(pfgpu::findProviderFactory(pfgpu::Provider::Auto), nullptr);
}

TEST_F(ProviderFactoryTest, FactoriesAreOrderedByFallbackChain)
{
    const std::vector<const pfgpu::IProviderFactory*> factories = pfgpu::providerFactories();
    ASSERT_GE(factories.size(), pfgpu::kFallbackOrder.size());
    for (std::size_t i = 0; i < pfgpu::kFallbackOrder.size(); ++i) {
        EXPECT_EQ(factories[i]->provider(), pfgpu::kFallbackOrder[i]);
    }
}

TEST_F(ProviderFactoryTest, RegistrationReplacesAnExistingProvider)
{
    pfgpu::registerProviderFactory(
        std::make_unique<FakeProviderFactory>(pfgpu::Provider::Cuda, "FakeExecutionProvider"));

    const pfgpu::IProviderFactory* factory = pfgpu::findProviderFactory(pfgpu::Provider::Cuda);
    ASSERT_NE(factory, nullptr);
    EXPECT_STREQ(factory->epName(), "FakeExecutionProvider");

    // Replacing must not duplicate the entry or disturb the chain order.
    const std::vector<const pfgpu::IProviderFactory*> factories = pfgpu::providerFactories();
    std::size_t cudaCount = 0;
    for (const pfgpu::IProviderFactory* f : factories) {
        if (f->provider() == pfgpu::Provider::Cuda) {
            ++cudaCount;
        }
    }
    EXPECT_EQ(cudaCount, 1u);
}

TEST_F(ProviderFactoryTest, DefaultOptionsSelectDeviceZero)
{
    // GPU selection is an option, not a separate API call: that is what keeps
    // the generic by-name path (ORT >= 1.12) sufficient.
    for (const pfgpu::Provider provider :
         {pfgpu::Provider::TensorRt, pfgpu::Provider::Cuda, pfgpu::Provider::Dml}) {
        const pfgpu::IProviderFactory* factory = pfgpu::findProviderFactory(provider);
        ASSERT_NE(factory, nullptr);
        const auto options = factory->defaultOptions();
        ASSERT_EQ(options.size(), 1u) << pfgpu::providerName(provider);
        EXPECT_EQ(options.front().first, "device_id");
        EXPECT_EQ(options.front().second, "0");
    }

    const pfgpu::IProviderFactory* cpu = pfgpu::findProviderFactory(pfgpu::Provider::Cpu);
    ASSERT_NE(cpu, nullptr);
    EXPECT_TRUE(cpu->defaultOptions().empty());
}

TEST_F(ProviderFactoryTest, CpuConfigureIsANoOp)
{
    const OrtApi* api = pfgpu::ortApi();
    if (!api) {
        GTEST_SKIP() << "onnxruntime.dll not available";
    }
    OrtSessionOptions* options = nullptr;
    ASSERT_EQ(api->CreateSessionOptions(&options), nullptr);
    ASSERT_NE(options, nullptr);

    const pfgpu::IProviderFactory* cpu = pfgpu::findProviderFactory(pfgpu::Provider::Cpu);
    ASSERT_NE(cpu, nullptr);
    std::string error;
    // Appending "CPUExecutionProvider" by name is not an ORT-supported call;
    // CPU is the implicit EP, so the factory must succeed without calling it.
    EXPECT_TRUE(cpu->configure(*api, *options, cpu->defaultOptions(), error)) << error;
    EXPECT_TRUE(error.empty());

    api->ReleaseSessionOptions(options);
}

// A missing GPU runtime must produce a message that names the exact missing
// entry point: this text is what the badge tooltip shows when a user installed
// the Base bundle on a GPU machine.
TEST_F(ProviderFactoryTest, MissingGpuEntryPointIsReportedWithItsName)
{
    const OrtApi* api = pfgpu::ortApi();
    if (!api) {
        GTEST_SKIP() << "onnxruntime.dll not available";
    }
    // This assertion is about the classic path; runtimes that already speak the
    // EP-device API are covered by the backend probe tests.
    if (pfgpu::ortRuntimeStatus().apiVersion >= pfgpu::kEpDeviceApiVersion) {
        GTEST_SKIP() << "runtime has the EP-device API; nothing to assert here";
    }
    if (pfgpu::isProviderAvailable(pfgpu::Provider::Cuda)) {
        GTEST_SKIP() << "CUDA EP is available on this machine";
    }

    OrtSessionOptions* options = nullptr;
    ASSERT_EQ(api->CreateSessionOptions(&options), nullptr);
    ASSERT_NE(options, nullptr);

    const pfgpu::IProviderFactory* factory = pfgpu::findProviderFactory(pfgpu::Provider::Cuda);
    ASSERT_NE(factory, nullptr);
    std::string error;
    EXPECT_FALSE(factory->configure(*api, *options, factory->defaultOptions(), error));
    EXPECT_NE(error.find(pfgpu::providerLegacyExportName(pfgpu::Provider::Cuda)),
              std::string::npos)
        << error;
    // And it must say what to do about it, not just what is missing.
    EXPECT_NE(error.find("1.22+"), std::string::npos) << error;

    api->ReleaseSessionOptions(options);
}

} // namespace
