#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/Inference.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <pfgpu/ProbeModel.hpp>
#include <pfgpu/SessionCache.hpp>

#include <cstdint>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <future>
#include "../../gpu/src/CudaGraphRunner.hpp"

namespace {

TEST(SessionCachePolicy, ProcessCacheHoldsOneModelConfiguration)
{
    EXPECT_EQ(pfgpu::processSessionCache().maxEntries(), 4u);
}

// Runs the probe model (Identity on float32 [1,2]) and returns the output.
// Everything goes through the dynamically loaded api pointer, so the test has
// no link-time dependency on ONNX Runtime either.
bool runIdentity(const OrtApi& api, OrtSession* session, std::vector<float>& data,
                 std::string& error)
{
    const std::int64_t shape[2] = {1, static_cast<std::int64_t>(data.size())};

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

TEST_F(SessionCacheTest, IdleCleanupReleasesOnlyUnownedSessions)
{
    pfgpu::SessionCache cache(4);
    auto held = cache.getOrCreate(probeModel());
    ASSERT_TRUE(held.ok) << held.error;
    std::weak_ptr<const void> lifetime = held.handle.owner;
    EXPECT_EQ(cache.releaseUnused(), 0u);
    EXPECT_EQ(cache.stats().live, 1u);
    std::vector<float> data{3.5f, -1.25f};
    std::string error;
    ASSERT_TRUE(runIdentity(*api_, held.handle.session, data, error)) << error;
    held.handle = {};
    EXPECT_EQ(cache.releaseUnused(), 1u);
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(cache.stats().live, 0u);
    EXPECT_EQ(cache.releaseUnused(), 0u);
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

TEST_F(SessionCacheTest, MultipleFloatInputsPreserveBindingOrderAndModelShapes)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR)+"/two-input-subtract.onnx");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto bytes=file.readAll();
    pfgpu::SessionCache cache;
    const auto loaded=cache.getOrCreate(pfgpu::ModelRef::fromBytes(
        std::string(bytes.constData(),bytes.size()),"two-input-subtract"),{pfgpu::Provider::Cpu,0,"multi-input-test",1});
    ASSERT_TRUE(loaded.ok)<<loaded.error;
    const auto spec=pfgpu::describeSession(loaded.handle);
    ASSERT_TRUE(spec.ok)<<spec.error;ASSERT_EQ(spec.inputs.size(),2U);
    EXPECT_EQ(spec.inputs[0].shape,(std::vector<std::int64_t>{1,2}));
    EXPECT_EQ(spec.inputs[1].shape,spec.inputs[0].shape);
    const std::array<pfgpu::FloatTensor,2> inputs{{{{1,2},{4.25F,-2.5F}},{{1,2},{1.5F,3.F}}}};
    const auto output=pfgpu::runFloats(loaded.handle,inputs);
    ASSERT_TRUE(output.ok)<<output.error;ASSERT_EQ(output.outputs.size(),1U);
    EXPECT_EQ(output.outputNames,(std::vector<std::string>{"difference"}));
    EXPECT_EQ(output.outputs[0].values,(std::vector<float>{2.75F,-5.5F}));
    const auto incomplete=pfgpu::runFloat(loaded.handle,inputs[0]);
    EXPECT_FALSE(incomplete.ok);EXPECT_EQ(incomplete.error,"input count does not match model");
}

TEST_F(SessionCacheTest, DiagnosticProfilingIsBoundedAndPreservesInference)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto previous = qgetenv("PF_ORT_PROFILE_ROOT");
    const bool wasSet = qEnvironmentVariableIsSet("PF_ORT_PROFILE_ROOT");
    struct Restore {
        QByteArray value; bool existed;
        ~Restore() { if (existed) qputenv("PF_ORT_PROFILE_ROOT", value); else qunsetenv("PF_ORT_PROFILE_ROOT"); }
    } restore{previous, wasSet};
    qputenv("PF_ORT_PROFILE_ROOT", directory.path().toUtf8());
    pfgpu::SessionCache cache;
    const auto session = cache.getOrCreate(probeModel(), {pfgpu::Provider::Cpu, 0, "bounded-profile", 1});
    ASSERT_TRUE(session.ok) << session.error;
    ASSERT_TRUE(session.handle.profiling);
    for (int i = 0; i < 20; ++i) {
        const auto output = pfgpu::runFloat(session.handle, {{1,2}, {4.25F,-2.5F}});
        ASSERT_TRUE(output.ok) << output.error;
        EXPECT_EQ(output.outputs[0].values, (std::vector<float>{4.25F,-2.5F}));
    }
    const auto reused = cache.getOrCreate(probeModel(), {pfgpu::Provider::Cpu, 0, "bounded-profile", 1});
    ASSERT_TRUE(reused.ok);
    EXPECT_FALSE(reused.handle.profiling);
    const auto files = QDir(directory.path()).entryList({QStringLiteral("*.json")}, QDir::Files);
    ASSERT_EQ(files.size(), 1);
    QFile file(directory.filePath(files[0]));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto profile = QJsonDocument::fromJson(file.readAll());
    EXPECT_TRUE(profile.isArray());
    EXPECT_FALSE(profile.isEmpty());
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

TEST_F(SessionCacheTest, AutomaticAndExplicitThreadsKeepSameInferenceContract)
{
    // Auto GPU uses the bounded submission policy; CPU auto and explicit
    // counts still work. Different requests must retain separate cache keys.
    for (const auto provider : {pfgpu::Provider::Cpu, pfgpu::Provider::Cuda, pfgpu::Provider::Dml}) {
        if (!pfgpu::isProviderAvailable(provider)) continue;
        pfgpu::SessionCache cache;
        const auto automatic = cache.getOrCreate(probeModel(), {provider, 0, "test", 0});
        const auto explicitThreads = cache.getOrCreate(probeModel(), {provider, 0, "test", 2});
        ASSERT_TRUE(automatic.ok) << automatic.error;
        ASSERT_TRUE(explicitThreads.ok) << explicitThreads.error;
        EXPECT_NE(automatic.handle.cacheKey, explicitThreads.handle.cacheKey);
        const pfgpu::FloatTensor input{{1, 2}, {4.25F, -2.5F}};
        const auto a = pfgpu::runFloat(automatic.handle, input);
        const auto b = pfgpu::runFloat(explicitThreads.handle, input);
        ASSERT_TRUE(a.ok) << a.error;
        ASSERT_TRUE(b.ok) << b.error;
        ASSERT_EQ(a.outputs.size(), 1u);
        ASSERT_EQ(b.outputs.size(), 1u);
        EXPECT_EQ(a.outputs[0].values, input.values);
        EXPECT_EQ(a.outputs[0].values, b.outputs[0].values);
    }
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

TEST_F(SessionCacheTest, ShrinkingCapacityEvictsImmediately)
{
    pfgpu::SessionCache cache(3);
    ASSERT_TRUE(cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "one"}).ok);
    ASSERT_TRUE(cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "two"}).ok);
    ASSERT_TRUE(cache.getOrCreate(probeModel(), {pfgpu::Provider::Auto, 0, "three"}).ok);
    cache.setMaxEntries(1);
    EXPECT_EQ(cache.stats().live, 1U);
}

TEST_F(SessionCacheTest, GraphSetupFailureKeepsOrdinaryInferenceUsable)
{
    pfgpu::SessionCache cache;
    const auto session=cache.getOrCreate(probeModel(),{pfgpu::Provider::Cpu,0,"fallback",1});
    ASSERT_TRUE(session.ok)<<session.error;
    const auto* api=pfgpu::ortApi();ASSERT_NE(api,nullptr);
    // The CPU identity fixture cannot supply graph device allocations. Force
    // the real runner through setup failure, then use its uncaptured fallback.
    pfgpu::detail::CudaGraphRunner runner(*api,session.handle.session,0);
    for(const auto& values:{std::vector<float>{1.f,2.f},std::vector<float>{-.5f,9.f}}) {
        pfgpu::FloatTensor input{{1,2},values};
        const auto result=runner.run(input);
        ASSERT_TRUE(result.ok)<<result.error;
        ASSERT_EQ(result.outputs.size(),1U);
        EXPECT_EQ(result.outputs.front().shape,input.shape);
        EXPECT_EQ(result.outputs.front().values,values);
    }
    auto invalid=pfgpu::FloatTensor{{1,2},{1.f}};
    EXPECT_FALSE(runner.run(invalid).ok);
    const auto recovered=runner.run(pfgpu::FloatTensor{{1,2},{3.f,4.f}});
    ASSERT_TRUE(recovered.ok)<<recovered.error;
    EXPECT_EQ(recovered.outputs.front().values,(std::vector<float>{3.f,4.f}));
}

TEST(SessionCacheCudaGraph, RealFloat32ModelsPreserveOutputsAcrossClientsAndCacheClear)
{
    const auto root=qEnvironmentVariable("PF_TEST_CUDA_GRAPH_MODEL_ROOT");
    if(root.isEmpty())GTEST_SKIP()<<"set PF_TEST_CUDA_GRAPH_MODEL_ROOT for the actual CUDA models";
    if(!pfgpu::isProviderAvailable(pfgpu::Provider::Cuda))GTEST_SKIP()<<"CUDA unavailable";
    const auto previous=qgetenv("PF_AUDIT_CUDA_GRAPH");
    struct Restore {QByteArray value;~Restore(){if(value.isNull())qunsetenv("PF_AUDIT_CUDA_GRAPH");else qputenv("PF_AUDIT_CUDA_GRAPH",value);}} restore{previous};
    for(const auto& model:{"yolo26m-pose.onnx","person-reid-osnet.onnx",
        "face_detection_yunet_2023mar.onnx","face_recognition_sface_2021dec.onnx"}) {
        SCOPED_TRACE(model);
        const auto path=QDir(root).filePath(model);
        ASSERT_TRUE(QFile::exists(path));
        pfgpu::SessionCache normalCache,graphCache;
        const auto source=pfgpu::ModelRef::fromPath(path.toStdString());
        qputenv("PF_AUDIT_CUDA_GRAPH","0");
        const auto normal=normalCache.getOrCreate(source,{pfgpu::Provider::Cuda,0,{},0});
        ASSERT_TRUE(normal.ok)<<normal.error;
        ASSERT_FALSE(normal.handle.cudaGraph);
        qputenv("PF_AUDIT_CUDA_GRAPH","1");
        const auto captured=graphCache.getOrCreate(source,{pfgpu::Provider::Cuda,0,{},0});
        ASSERT_TRUE(captured.ok)<<captured.error;
        ASSERT_TRUE(captured.handle.cudaGraph);
        const auto description=pfgpu::describeSession(normal.handle);
        ASSERT_TRUE(description.ok)<<description.error;
        pfgpu::FloatTensor input;input.shape=description.input.shape;
        size_t elements=1;
        for(const auto d:input.shape){ASSERT_GT(d,0);elements*=static_cast<size_t>(d);}
        ASSERT_LE(elements,4*1024*1024U);
        input.values.resize(elements);
        for(size_t i=0;i<elements;++i)input.values[i]=static_cast<float>((i*37)%251)/250.f;
        std::vector<pfgpu::FloatTensor> inputs;
        std::vector<pfgpu::InferenceResult> reference;
        for(int i=0;i<12;++i) {
            input.values[static_cast<size_t>(i)%elements]=static_cast<float>(i)/12.f;
            inputs.push_back(input);reference.push_back(pfgpu::runFloat(normal.handle,input));
            ASSERT_TRUE(reference.back().ok)<<reference.back().error;
        }
        auto invalid=inputs.front();invalid.values.pop_back();
        EXPECT_FALSE(pfgpu::runFloat(captured.handle,invalid).ok);
        const auto first=pfgpu::runFloat(captured.handle,inputs.front());
        ASSERT_TRUE(first.ok)<<first.error;
        auto reused=graphCache.getOrCreate(source,{pfgpu::Provider::Cuda,0,{},0});
        ASSERT_TRUE(reused.ok);
        EXPECT_EQ(reused.handle.cudaGraph,captured.handle.cudaGraph);
        graphCache.clear();normalCache.clear();
        std::vector<std::future<pfgpu::InferenceResult>> futures;
        for(const auto& tensor:inputs)futures.push_back(std::async(std::launch::async,
            [handle=captured.handle,tensor]{return pfgpu::runFloat(handle,tensor);}));
        for(size_t i=0;i<futures.size();++i) {
            const auto actual=futures[i].get();ASSERT_TRUE(actual.ok)<<actual.error;
            ASSERT_EQ(actual.outputs.size(),reference[i].outputs.size());
            EXPECT_EQ(actual.outputNames,reference[i].outputNames);
            for(size_t j=0;j<actual.outputs.size();++j) {
                EXPECT_EQ(actual.outputs[j].shape,reference[i].outputs[j].shape);
                EXPECT_EQ(actual.outputs[j].values,reference[i].outputs[j].values)<<i<<":"<<j;
            }
        }
        qunsetenv("PF_AUDIT_CUDA_GRAPH");
        const auto automatic=graphCache.getOrCreate(source,{pfgpu::Provider::Cuda,0,"b1",0});
        ASSERT_TRUE(automatic.ok)<<automatic.error;
        EXPECT_EQ(bool(automatic.handle.cudaGraph),pfgpu::ortRuntimeVersion()=="1.24.4");
        const auto automaticResult=pfgpu::runFloat(automatic.handle,inputs.front());
        ASSERT_TRUE(automaticResult.ok)<<automaticResult.error;
        ASSERT_EQ(automaticResult.outputs.size(),reference.front().outputs.size());
        for(size_t i=0;i<automaticResult.outputs.size();++i)
            EXPECT_EQ(automaticResult.outputs[i].values,reference.front().outputs[i].values);
    }
}
} // namespace
