#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/OrtRuntime.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// The probe is memoized per process, so most tests share one result. Tests that
// need a fresh run call resetBackendProbeForTesting().
class BackendProbeTest : public ::testing::Test {
protected:
    void TearDown() override { pfgpu::resetBackendProbeForTesting(); }
};

TEST_F(BackendProbeTest, CoversTheWholeFallbackChainInOrder)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    ASSERT_EQ(probe.backends.size(), pfgpu::kFallbackOrder.size());
    for (std::size_t i = 0; i < pfgpu::kFallbackOrder.size(); ++i) {
        EXPECT_EQ(probe.backends[i].provider, pfgpu::kFallbackOrder[i]);
    }
    // CPU is the last resort and must be the last entry (spec section 1).
    EXPECT_EQ(probe.backends.back().provider, pfgpu::Provider::Cpu);
}

TEST_F(BackendProbeTest, ReportsWhySomethingIsUnavailable)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    for (const pfgpu::BackendStatus& status : probe.backends) {
        if (status.available) {
            EXPECT_TRUE(status.reason.empty())
                << pfgpu::providerName(status.provider) << ": " << status.reason;
            EXPECT_FALSE(status.deviceName.empty())
                << pfgpu::providerName(status.provider);
        } else {
            // An unavailable link of the chain without a reason is useless in
            // the UI: the badge tooltip shows exactly this text.
            EXPECT_FALSE(status.reason.empty()) << pfgpu::providerName(status.provider);
        }
    }
}

TEST_F(BackendProbeTest, CpuAvailabilityTracksTheRuntime)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    EXPECT_EQ(probe.ortLoaded, pfgpu::ortRuntimeAvailable());
    const pfgpu::BackendStatus* cpu = pfgpu::findBackendStatus(pfgpu::Provider::Cpu);
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->available, probe.ortLoaded);
    if (!probe.ortLoaded) {
        EXPECT_FALSE(probe.ortError.empty());
    }
}

TEST_F(BackendProbeTest, RuntimeVersionMatchesTheLoadedLibrary)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    EXPECT_EQ(probe.ortVersion, pfgpu::ortRuntimeVersion());
    if (probe.ortLoaded) {
        EXPECT_FALSE(probe.ortVersion.empty());
        EXPECT_GE(probe.ortApiVersion, pfgpu::kMinSupportedApiVersion);
        EXPECT_LE(probe.ortApiVersion, ORT_API_VERSION);
    }
}

TEST_F(BackendProbeTest, DefaultProviderIsTheFirstAvailableLink)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    const pfgpu::Provider expected = [&probe] {
        for (const pfgpu::BackendStatus& status : probe.backends) {
            if (status.available) {
                return status.provider;
            }
        }
        return pfgpu::Provider::Cpu;
    }();

    EXPECT_EQ(pfgpu::defaultProvider(), expected);
    EXPECT_TRUE(pfgpu::isProviderAvailable(expected) || expected == pfgpu::Provider::Cpu);
}

TEST_F(BackendProbeTest, ResolveProviderMapsAutoToTheDefault)
{
    EXPECT_EQ(pfgpu::resolveProvider(pfgpu::Provider::Auto), pfgpu::defaultProvider());
    EXPECT_EQ(pfgpu::resolveProvider(pfgpu::Provider::Cuda), pfgpu::Provider::Cuda);
    EXPECT_EQ(pfgpu::resolveProvider(pfgpu::Provider::Dml), pfgpu::Provider::Dml);
}

TEST_F(BackendProbeTest, AvailabilityQueriesAgreeWithTheProbe)
{
    for (const pfgpu::Provider provider : pfgpu::kFallbackOrder) {
        const pfgpu::BackendStatus* status = pfgpu::findBackendStatus(provider);
        ASSERT_NE(status, nullptr) << pfgpu::providerName(provider);
        EXPECT_EQ(pfgpu::isProviderAvailable(provider), status->available);
    }
    // Auto is not a provider that can be "available" as such; it reports the
    // runtime state, which is what the settings UI needs.
    EXPECT_EQ(pfgpu::isProviderAvailable(pfgpu::Provider::Auto),
              pfgpu::probeBackends().ortLoaded);
}

TEST_F(BackendProbeTest, DeviceInfoListsOnlyAvailableBackends)
{
    const std::vector<pfgpu::DeviceInfo> devices = pfgpu::getDeviceInfo();
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();

    const std::size_t available = static_cast<std::size_t>(
        std::count_if(probe.backends.begin(), probe.backends.end(),
                      [](const pfgpu::BackendStatus& s) { return s.available; }));
    EXPECT_EQ(devices.size(), available);

    for (const pfgpu::DeviceInfo& device : devices) {
        EXPECT_FALSE(device.name.empty());
        EXPECT_FALSE(device.backend.empty());
        const auto parsed = pfgpu::parseProvider(device.backend);
        ASSERT_TRUE(parsed.has_value()) << device.backend;
        EXPECT_TRUE(pfgpu::isProviderAvailable(*parsed));
    }
}

// Guard against the failure mode where our own probe model is invalid: then
// every GPU provider is reported unavailable with a model-loading error, which
// looks like "no GPU" in the UI and hides the real bug.
TEST_F(BackendProbeTest, GpuProbeFailuresAreNotModelErrors)
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    if (!probe.ortLoaded) {
        GTEST_SKIP() << "onnxruntime.dll not installed: " << probe.ortError;
    }
    for (const pfgpu::BackendStatus& status : probe.backends) {
        if (status.provider == pfgpu::Provider::Cpu || status.available) {
            continue;
        }
        EXPECT_EQ(status.reason.find("Failed to load model"), std::string::npos)
            << pfgpu::providerName(status.provider) << ": " << status.reason;
        EXPECT_EQ(status.reason.find("Missing opset"), std::string::npos)
            << pfgpu::providerName(status.provider) << ": " << status.reason;
    }
}

TEST_F(BackendProbeTest, ProbeIsMemoizedAndStableAcrossRuns)
{
    const pfgpu::Provider first = pfgpu::defaultProvider();
    const std::size_t backends = pfgpu::probeBackends().backends.size();

    pfgpu::resetBackendProbeForTesting();
    EXPECT_EQ(pfgpu::defaultProvider(), first);
    EXPECT_EQ(pfgpu::probeBackends().backends.size(), backends);
}

// Version-agnostic ORT probe (LoadLibrary + OrtGetApiBase + GetApi descending).
// Never fails on a Base-only machine.
TEST(PfgpuOrtRuntime, ProbeIsGracefulWhenTheRuntimeIsMissing)
{
    const pfgpu::OrtRuntimeStatus& status = pfgpu::ortRuntimeStatus();
    if (!status.loaded) {
        EXPECT_FALSE(status.error.empty());
        EXPECT_EQ(pfgpu::ortApi(), nullptr);
        EXPECT_FALSE(pfgpu::ortRuntimeAvailable());
        EXPECT_TRUE(pfgpu::ortRuntimeVersion().empty());
        GTEST_SKIP() << "onnxruntime.dll not installed: " << status.error;
    }

    const std::string version = pfgpu::ortRuntimeVersion();
    ASSERT_FALSE(version.empty());
    EXPECT_GE(version.front(), '0');
    EXPECT_LE(version.front(), '9');
    EXPECT_NE(pfgpu::ortApi(), nullptr);
    EXPECT_GE(status.apiVersion, pfgpu::kMinSupportedApiVersion);
}

} // namespace
