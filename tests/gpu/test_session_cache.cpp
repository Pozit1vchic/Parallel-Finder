#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/Inference.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <pfgpu/ProbeModel.hpp>
#include <pfgpu/SessionCache.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Runs the probe model (Identity on float32 [1,2]) and returns the output.
// Everything goes through the dynamically loaded api pointer, so the test has
// no link-time dependency on ONNX Runtime either.
bool runIdentity(const OrtApi& api, OrtSession* session, std::vector<float>& data,
                 std::string& error)
{
    const std::int64_t shape[2] = {1, data.size()};

    OrtMemoryInfo* memoryInfo = nullptr;
    if (!pfgpu::checkStatus(api,
                            api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                                    &memoryInfo),
                            error)) {
        return false;
    }

    OrtValue* input = nullptr;
    const bool created = pfgpu::checkStatus(
        api,
        api.CreateTensorWithDataAsOrtValue(memoryInfo, data.data(),
                                           data.size() * sizeof(float), shape, 2,
                                           ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input),
        error);
    api.ReleaseMemoryInfo(memoryInfo);
    if (!created) {
        return false;
    }

    const char* inputNames[1] = {pfgpu::kProbeModelInputName.data()};
    const char* outputNames[1] = {pfgpu::kProbeModelOutputName.data()};
    const OrtValue* inputs[1] = {input};
    OrtValue* output = nullptr;
    const bool ran = pfgpu::checkStatus(
        api, api.Run(session, nullptr, inputNames, inputs, 1, outputNames, 1, &output),
        error);
    api.ReleaseValue(input);
    if (!ran) {
        return false;
    }

    float* raw = nullptr;
    const bool read = pfgpu::checkStatus(api, api.GetTensorMutableData(output,
                                                                      reinterpret_cast<void**>(&raw)),
                                         error);
    if (read) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = raw[i];
        }
    }
    api.ReleaseValue(output);
    return read;
}

// Every test here needs a working runtime; on a Base-only machine they report
// as skipped rather than failed.
class SessionCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!pfgpu::ortRuntimeAvailable()) {
            GTEST_SKIP() << "onnxruntime.dll not available: " << pfgpu::ortRuntimeStatus().error;
        }
        api_ = pfgpu::ortApi();
        ASSERT_NE(api_, nullptr);
    }

    pfgpu::ModelRef probeModel() const
    {
        return pfgpu::ModelRef::fromBytes(std::string(pfgpu::probeModelBytes()), "probe");
    }

    const OrtApi* api_ = nullptr;
};

TEST_F(SessionCacheTest, CreatesASessionFromAnInMemoryModel)
{
    pfgpu::SessionCache cache;
    const pfgpu::SessionCache::Result result = cache.getOrCreate(probeModel());

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_NE(result.handle.session, nullptr);
    EXPECT_TRUE(result.handle.createdNow);
    EXPECT_FALSE(result.handle.cacheKey.empty());
    EXPECT_EQ(result.handle.provider, pfgpu::defaultProvider());

    const pfgpu::SessionCache::Stats stats = cache.stats();
    EXPECT_EQ(stats.created, 1u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.hits, 0u);
    EXPECT_EQ(stats.live, 1u);
}

TEST_F(SessionCacheTest, CachedSessionActuallyRunsTheGraph)
{
    pfgpu::SessionCache cache;
    const pfgpu::SessionCache::Result result = cache.getOrCreate(probeModel());
    ASSERT_TRUE(result.ok) << result.error;

    std::vector<float> data {3.5f, -1.25f};
    std::string error;
    ASSERT_TRUE(runIdentity(*api_, result.handle.session, data, error)) << error;
    EXPECT_FLOAT_EQ(data[0], 3.5f);
    EXPECT_FLOAT_EQ(data[1], -1.25f);
}

TEST_F(SessionCacheTest, FloatInferenceCopiesOrtOutputIntoOwnedTensor)
{
    pfgpu::SessionCache cache;
    const auto result = cache.getOrCreate(probeModel());
    ASSERT_TRUE(result.ok) << result.error;

    pfgpu::FloatTensor input;
    input.shape = {1, 2};
    input.values = {4.25F, -2.5F};
    const auto output = pfgpu::runFloat(result.handle, input);
    ASSERT_TRUE(output.ok) << output.error;
    ASSERT_EQ(output.outputs.size(), 1U);
    ASSERT_EQ(output.outputs.front().values.size(), 2U);
    EXPECT_FLOAT_EQ(output.outputs.front().values[0], 4.25F);
    EXPECT_FLOAT_EQ(output.outputs.front().values[1], -2.5F);
}

TEST_F(SessionCacheTest, SecondRequestIsAHit)
{
    pfgpu::SessionCache cache;
    const auto first = cache.getOrCreate(probeModel());
    ASSERT_TRUE(first.ok) << first.error;

    const auto second = cache.getOrCreate(probeModel());
    ASSERT_TRUE(second.ok) << second.error;
    EXPECT_FALSE(second.handle.createdNow);
    EXPECT_EQ(second.handle.session, first.handle.session);

    const pfgpu::SessionCache::Stats stats = cache.stats();
    EXPECT_EQ(stats.created, 1u);
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_EQ(stats.misses, 1u);
}

TEST_F(SessionCacheTest, DifferentProfilesGetDifferentSessions)
{
    // D1 in docs/decisions.md: b1/b8 profiles must never share a session.
    pfgpu::SessionCache cache;
    const auto small = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "b1"});
    const auto large = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "b8"});
    ASSERT_TRUE(small.ok) << small.error;
    ASSERT_TRUE(large.ok) << large.error;

    EXPECT_NE(small.handle.session, large.handle.session);
    EXPECT_NE(small.handle.cacheKey, large.handle.cacheKey);
    EXPECT_EQ(cache.stats().live, 2u);
}

TEST_F(SessionCacheTest, EvictsLeastRecentlyUsedBeyondCapacity)
{
    pfgpu::SessionCache cache(2);
    const auto first = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "p1"});
    ASSERT_TRUE(first.ok) << first.error;
    // Touch p1 so p2 becomes the least recently used entry.
    ASSERT_TRUE(cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "p1"}).ok);
    const auto second = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "p2"});
    ASSERT_TRUE(second.ok) << second.error;
    const auto third = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "p3"});
    ASSERT_TRUE(third.ok) << third.error;

    const pfgpu::SessionCache::Stats stats = cache.stats();
    EXPECT_EQ(stats.live, 2u);
    EXPECT_EQ(stats.evictions, 1u);
}

TEST_F(SessionCacheTest, HandleSurvivesEvictionAndClear)
{
    // Eviction/clear must not pull a session out from under an in-flight user
    // (JobManager holds handles across long analyses).
    pfgpu::SessionCache cache(1);
    const auto first = cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "keep"});
    ASSERT_TRUE(first.ok) << first.error;

    ASSERT_TRUE(cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "other"}).ok);
    cache.clear();
    EXPECT_EQ(cache.stats().live, 0u);

    std::vector<float> data {7.0f, 8.0f};
    std::string error;
    ASSERT_TRUE(runIdentity(*api_, first.handle.session, data, error)) << error;
    EXPECT_FLOAT_EQ(data[0], 7.0f);
}

TEST_F(SessionCacheTest, RejectsInvalidModelBytes)
{
    pfgpu::SessionCache cache;
    const auto result = cache.getOrCreate(pfgpu::ModelRef::fromBytes("not an onnx model", "bad"));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(result.handle.session, nullptr);
    EXPECT_EQ(cache.stats().live, 0u);
}

TEST_F(SessionCacheTest, ManualProviderSelectionFailsLoudlyWhenUnavailable)
{
    pfgpu::Provider unavailable = pfgpu::Provider::Auto;
    for (const pfgpu::Provider provider : pfgpu::kFallbackOrder) {
        if (provider != pfgpu::Provider::Cpu
            && !pfgpu::isProviderAvailable(provider)) {
            unavailable = provider;
            break;
        }
    }
    if (unavailable == pfgpu::Provider::Auto) {
        GTEST_SKIP() << "every GPU provider is available on this machine";
    }

    pfgpu::SessionCache cache;
    const auto result = cache.getOrCreate(probeModel(), {unavailable, 0, {}});
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find(pfgpu::providerName(unavailable)), std::string::npos)
        << result.error;
}

TEST(SessionCacheStandalone, RejectsZeroCapacity)
{
    pfgpu::SessionCache cache;
    EXPECT_THROW(cache.setMaxEntries(0), std::invalid_argument);
    EXPECT_EQ(cache.maxEntries(), pfgpu::SessionCache::kDefaultMaxEntries);
    cache.setMaxEntries(8);
    EXPECT_EQ(cache.maxEntries(), 8u);
}

} // namespace
