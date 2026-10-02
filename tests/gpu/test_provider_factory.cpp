#include <gtest/gtest.h>

#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <pfgpu/ProviderFactory.hpp>

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

struct OptionSpy {
    std::map<std::string, std::string> options;
    int created = 0, released = 0, appended = 0;
    std::string fail;
};
thread_local OptionSpy optionSpy;

class ScopedCachePath {
public:
    explicit ScopedCachePath(const QByteArray& path)
        : previous_(qgetenv("PF_TRT_CACHE_PATH")), wasSet_(qEnvironmentVariableIsSet("PF_TRT_CACHE_PATH"))
    { qputenv("PF_TRT_CACHE_PATH", path); }
    ~ScopedCachePath()
    {
        if (wasSet_) qputenv("PF_TRT_CACHE_PATH", previous_);
        else qunsetenv("PF_TRT_CACHE_PATH");
    }
private:
    QByteArray previous_;
    bool wasSet_;
};

OrtStatus* optionFailure(const char* stage)
{
    return optionSpy.fail == stage ? pfgpu::ortApi()->CreateStatus(ORT_FAIL, stage) : nullptr;
}
template<class Options> OrtStatus* ORT_API_CALL createOptions(Options** out) noexcept
{
    *out = reinterpret_cast<Options*>(new int(1));
    ++optionSpy.created;
    return optionFailure("create");
}
template<class Options> OrtStatus* ORT_API_CALL updateOptions(Options*, const char* const* keys,
                                                           const char* const* values, std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i) optionSpy.options[keys[i]] = values[i];
    return optionFailure("update");
}
template<class Options> OrtStatus* ORT_API_CALL appendOptions(OrtSessionOptions*, const Options*) noexcept
{
    ++optionSpy.appended;
    return optionFailure("append");
}
template<class Options> void ORT_API_CALL releaseOptions(Options* value)
{
    delete reinterpret_cast<int*>(value);
    ++optionSpy.released;
}

OrtApi optionSpyApi()
{
    // Do not copy the entire newer header's OrtApi from an older DLL table.
    OrtApi api{};
    api.GetErrorMessage = pfgpu::ortApi()->GetErrorMessage;
    api.ReleaseStatus = pfgpu::ortApi()->ReleaseStatus;
    api.CreateCUDAProviderOptions = createOptions<OrtCUDAProviderOptionsV2>;
    api.UpdateCUDAProviderOptions = updateOptions<OrtCUDAProviderOptionsV2>;
    api.SessionOptionsAppendExecutionProvider_CUDA_V2 = appendOptions<OrtCUDAProviderOptionsV2>;
    api.ReleaseCUDAProviderOptions = releaseOptions<OrtCUDAProviderOptionsV2>;
    api.CreateTensorRTProviderOptions = createOptions<OrtTensorRTProviderOptionsV2>;
    api.UpdateTensorRTProviderOptions = updateOptions<OrtTensorRTProviderOptionsV2>;
    api.SessionOptionsAppendExecutionProvider_TensorRT_V2 = appendOptions<OrtTensorRTProviderOptionsV2>;
    api.ReleaseTensorRTProviderOptions = releaseOptions<OrtTensorRTProviderOptionsV2>;
    return api;
}

TEST_F(ProviderFactoryTest, OpaqueCudaOptionsAreAppliedAndReleased)
{
    if (!pfgpu::ortApi() || pfgpu::ortRuntimeStatus().apiVersion < 11) GTEST_SKIP() << "requires ORT API >= 11";
    optionSpy = {};
    const auto api = optionSpyApi();
    std::string error;
    const FakeProviderFactory factory(pfgpu::Provider::Cuda, "CUDAExecutionProvider");
    // The fake append never dereferences the opaque session-options pointer.
    int dummy = 0;
    EXPECT_TRUE(pfgpu::attachExecutionProvider(api, *reinterpret_cast<OrtSessionOptions*>(&dummy), factory,
        {{"device_id", "2"}, {"cudnn_conv_algo_search", "HEURISTIC"}, {"do_copy_in_default_stream", "1"}}, error)) << error;
    EXPECT_EQ(optionSpy.options["device_id"], "2");
    EXPECT_EQ(optionSpy.options["cudnn_conv_algo_search"], "HEURISTIC");
    EXPECT_EQ(optionSpy.options["do_copy_in_default_stream"], "1");
    EXPECT_EQ(optionSpy.created, 1);
    EXPECT_EQ(optionSpy.appended, 1);
    EXPECT_EQ(optionSpy.released, 1);
}

TEST_F(ProviderFactoryTest, TensorRtRuntimeCacheOptionsReachOpaqueApi)
{
    if (!pfgpu::ortApi() || pfgpu::ortRuntimeStatus().apiVersion < 11) GTEST_SKIP() << "requires ORT API >= 11";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto cachePath = directory.filePath(QStringLiteral("кэш движков"));
    const ScopedCachePath overridePath(cachePath.toUtf8());
    optionSpy = {};
    const auto api = optionSpyApi();
    const auto* factory = pfgpu::findProviderFactory(pfgpu::Provider::TensorRt);
    int dummy = 0;
    std::string error;
    ASSERT_TRUE(factory->configure(api, *reinterpret_cast<OrtSessionOptions*>(&dummy), factory->defaultOptions(), error)) << error;
    EXPECT_EQ(optionSpy.options["trt_engine_cache_enable"], "1");
    EXPECT_EQ(optionSpy.options["trt_timing_cache_enable"], "1");
    EXPECT_EQ(optionSpy.options["trt_fp16_enable"], "1");
    EXPECT_EQ(optionSpy.options["trt_engine_cache_path"], cachePath.toUtf8().toStdString());
    EXPECT_EQ(optionSpy.options["trt_engine_cache_path"], optionSpy.options["trt_timing_cache_path"]);
    EXPECT_TRUE(QFileInfo(cachePath).isDir());
    EXPECT_EQ(optionSpy.released, 1);
}

TEST_F(ProviderFactoryTest, UnavailableTensorRtCacheIsReportedBeforeAppendingProvider)
{
    if (!pfgpu::ortApi() || pfgpu::ortRuntimeStatus().apiVersion < 11) GTEST_SKIP() << "requires ORT API >= 11";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QFile blocker(directory.filePath(QStringLiteral("not-a-directory")));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const ScopedCachePath overridePath((blocker.fileName() + QStringLiteral("/cache")).toUtf8());
    optionSpy = {};
    const auto api = optionSpyApi();
    const auto* factory = pfgpu::findProviderFactory(pfgpu::Provider::TensorRt);
    int dummy = 0;
    std::string error;
    EXPECT_FALSE(factory->configure(api, *reinterpret_cast<OrtSessionOptions*>(&dummy), factory->defaultOptions(), error));
    EXPECT_NE(error.find("TensorRT cache directory"), std::string::npos);
    EXPECT_EQ(optionSpy.created, 0);
    EXPECT_EQ(optionSpy.appended, 0);
}

TEST_F(ProviderFactoryTest, OpaqueProviderErrorsReleaseOptionsAndNeverDropSettings)
{
    if (!pfgpu::ortApi() || pfgpu::ortRuntimeStatus().apiVersion < 11) GTEST_SKIP() << "requires ORT API >= 11";
    const auto api = optionSpyApi();
    int dummy = 0;
    for (const auto provider : {pfgpu::Provider::Cuda, pfgpu::Provider::TensorRt})
        for (const std::string stage : {"create", "update", "append"}) {
            optionSpy = {};
            optionSpy.fail = stage;
            const FakeProviderFactory factory(provider, pfgpu::providerEpName(provider));
            std::string error;
            EXPECT_FALSE(pfgpu::attachExecutionProvider(api, *reinterpret_cast<OrtSessionOptions*>(&dummy), factory,
                {{"device_id", "0"}, {"test-option", "value"}}, error));
            EXPECT_NE(error.find(stage), std::string::npos);
            EXPECT_EQ(optionSpy.created, 1);
            EXPECT_EQ(optionSpy.released, 1);
            EXPECT_EQ(optionSpy.appended, stage == "append" ? 1 : 0);
        }
}

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
TEST_F(ProviderFactoryTest, MissingGpuProviderIsReportedWithItsNameAndRuntimeAdvice)
{
    const OrtApi* api = pfgpu::ortApi();
    if (!api) {
        GTEST_SKIP() << "onnxruntime.dll not available";
    }
    // Opaque V2 reports the EP itself; classic-only packages report the
    // missing exported symbol. Neither failure should look like CPU success.
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
    EXPECT_NE(error.find(pfgpu::ortRuntimeStatus().apiVersion >= 11
        ? pfgpu::providerEpName(pfgpu::Provider::Cuda)
        : pfgpu::providerLegacyExportName(pfgpu::Provider::Cuda)),
              std::string::npos)
        << error;
    // And it must say what to do about it, not just what is missing.
    EXPECT_NE(error.find("install the matching GPU runtime"), std::string::npos) << error;

    api->ReleaseSessionOptions(options);
}

} // namespace
