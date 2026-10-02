#include "pfgpu/ProviderFactory.hpp"

#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <cstdlib>

#include "pfgpu/OrtRuntime.hpp"

namespace pfgpu {
namespace {

// Classic entry point exported by ORT GPU packages older than 1.22.
using AppendProviderFn = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, int);

int deviceIdFrom(const IProviderFactory::OptionList& options)
{
    for (const auto& [key, value] : options) {
        if (key == "device_id") {
            try {
                return std::stoi(value);
            } catch (const std::exception&) {
                return 0;
            }
        }
    }
    return 0;
}

bool tensorRtRuntimeOptions(const IProviderFactory::OptionList& defaults,
                           IProviderFactory::OptionList& options, std::string& message)
{
    options = defaults;
    std::error_code error;
    std::filesystem::path cacheRoot;
    // The GUI sets this to its persistent cache root; probes/benchmarks can
    // supply an isolated directory. Qt's environment value is UTF-8 on Windows.
    if (const auto* overridePath = std::getenv("PF_TRT_CACHE_PATH"); overridePath && *overridePath)
        cacheRoot = std::filesystem::path(std::u8string(overridePath, overridePath + std::strlen(overridePath)));
    else cacheRoot = std::filesystem::temp_directory_path(error) / "ParallelFinder" / "trt-cache";
    if (!error) std::filesystem::create_directories(cacheRoot, error);
    if (error) {
        // Do not enable a cache without its path: ORT would silently write to
        // the working directory rather than the user-selected cache location.
        message = "TensorRT cache directory is unavailable: " + error.message();
        return false;
    }
    const auto utf8Path = cacheRoot.u8string();
    const std::string cachePath(utf8Path.begin(), utf8Path.end());
    options.push_back({"trt_fp16_enable", "1"});
    options.push_back({"trt_engine_cache_enable", "1"});
    options.push_back({"trt_timing_cache_enable", "1"});
    options.push_back({"trt_engine_cache_path", cachePath});
    options.push_back({"trt_timing_cache_path", cachePath});
    return true;
}

IProviderFactory::OptionList cudaRuntimeOptions(const IProviderFactory::OptionList& defaults)
{
    auto options = defaults;
    // Diagnostic only: compare tuning policies in separate benchmark processes
    // before changing the production default (ORT's EXHAUSTIVE search).
    if (const auto* search = std::getenv("PF_CUDA_CONV_SEARCH"); search
        && (std::strcmp(search, "EXHAUSTIVE") == 0 || std::strcmp(search, "HEURISTIC") == 0
            || std::strcmp(search, "DEFAULT") == 0))
        options.emplace_back("cudnn_conv_algo_search", search);
    return options;
}

// Public opaque option objects are ABI-stable and part of OrtApi since 1.11
// (TensorRT V2 predates that). Unlike the classic exported device-id function,
// these calls actually apply cache/tuning options. No CUDA/TRT SDK link needed.
template<class Options, class Create, class Update, class Append, class Release>
bool attachOpaqueOptions(const OrtApi& api, OrtSessionOptions& sessionOptions,
                         const IProviderFactory::OptionList& options,
                         Create create, Update update, Append append, Release release,
                         std::string& error)
{
    Options* raw = nullptr;
    OrtStatus* status = create(&raw);
    const std::unique_ptr<Options, Release> owner(raw, release);
    if (!checkStatus(api, status, error)) return false;
    if (!raw) { error = "provider returned null options"; return false; }
    std::vector<const char*> keys, values;
    keys.reserve(options.size()); values.reserve(options.size());
    for (const auto& [key, value] : options) {
        keys.push_back(key.c_str()); values.push_back(value.c_str());
    }
    return checkStatus(api, update(raw, keys.data(), values.data(), keys.size()), error)
        && checkStatus(api, append(&sessionOptions, raw), error);
}

// Every provider we ship except CPU goes through attachExecutionProvider.
class BuiltinProviderFactory final : public IProviderFactory {
public:
    BuiltinProviderFactory(Provider provider, OptionList defaults)
        : provider_(provider)
        , defaults_(std::move(defaults))
    {
    }

    Provider provider() const noexcept override { return provider_; }

    const char* epName() const noexcept override { return providerEpName(provider_); }

    OptionList defaultOptions() const override { return defaults_; }

    bool configure(const OrtApi& api,
                   OrtSessionOptions& options,
                   const OptionList& optionsToApply,
                   std::string& error) const override
    {
        // CPU is built into the runtime and is always the implicit EP: there is
        // no API call that appends it, so this is a deliberate no-op rather than
        // a call that would fail and mark CPU "unavailable".
        if (provider_ == Provider::Cpu) {
            return true;
        }
        // Keep defaultOptions() deliberately minimal (device_id only). Several
        // callers and tests rely on that stable factory contract. TensorRT's
        // performance options are runtime configuration, not provider identity.
        if (provider_ == Provider::TensorRt) {
            OptionList effectiveOptions;
            if (!tensorRtRuntimeOptions(optionsToApply, effectiveOptions, error)) return false;
            return IProviderFactory::configure(api, options, effectiveOptions, error);
        }
        if (provider_ == Provider::Cuda) {
            const auto effectiveOptions = cudaRuntimeOptions(optionsToApply);
            return IProviderFactory::configure(api, options, effectiveOptions, error);
        }
        return IProviderFactory::configure(api, options, optionsToApply, error);
    }

private:
    Provider provider_;
    OptionList defaults_;
};

struct Registry {
    std::mutex mutex;
    bool builtinsInstalled = false;
    std::vector<std::unique_ptr<IProviderFactory>> factories;
    std::unordered_map<int, std::size_t> byProvider; // Provider -> index in factories
};

Registry& registry()
{
    static Registry r;
    return r;
}

void installBuiltinsLocked(Registry& r)
{
    if (r.builtinsInstalled) {
        return;
    }
    r.builtinsInstalled = true;

    struct Builtin {
        Provider provider;
        IProviderFactory::OptionList defaults;
    };
    const Builtin builtins[] = {
        // Device selection is an option (EP-device API: which OrtEpDevice we
        // pass; classic path: the device_id argument).
        {Provider::TensorRt, {{"device_id", "0"}}},
        {Provider::Cuda, {{"device_id", "0"}}},
        {Provider::Dml, {{"device_id", "0"}}},
        {Provider::Cpu, {}},
    };

    for (const Builtin& builtin : builtins) {
        auto factory = std::make_unique<BuiltinProviderFactory>(builtin.provider, builtin.defaults);
        r.byProvider[static_cast<int>(builtin.provider)] = r.factories.size();
        r.factories.push_back(std::move(factory));
    }
}

int providerIndexInFallback(Provider p)
{
    for (std::size_t i = 0; i < kFallbackOrder.size(); ++i) {
        if (kFallbackOrder[i] == p) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ORT >= 1.22: pick the OrtEpDevice for this EP and attach it.
bool attachViaEpDeviceApi(const OrtApi& api,
                          OrtSessionOptions& options,
                          const IProviderFactory& factory,
                          const IProviderFactory::OptionList& optionsToApply,
                          std::string& error)
{
    OrtEnv* env = sharedOrtEnv(error);
    if (!env) {
        return false;
    }

    const OrtEpDevice* const* devices = nullptr;
    std::size_t deviceCount = 0;
    if (!checkStatus(api, api.GetEpDevices(env, &devices, &deviceCount), error)) {
        return false;
    }

    const OrtEpDevice* match = nullptr;
    for (std::size_t i = 0; i < deviceCount; ++i) {
        const char* name = api.EpDevice_EpName(devices[i]);
        if (name && std::strcmp(name, factory.epName()) == 0) {
            match = devices[i];
            break;
        }
    }
    if (!match) {
        error = "runtime exposes no device for '" + std::string(factory.epName())
            + "' (found " + std::to_string(deviceCount) + " EP device(s))";
        return false;
    }

    std::vector<const char*> keys;
    std::vector<const char*> values;
    keys.reserve(optionsToApply.size());
    values.reserve(optionsToApply.size());
    for (const auto& [key, value] : optionsToApply) {
        keys.push_back(key.c_str());
        values.push_back(value.c_str());
    }

    return checkStatus(api,
                       api.SessionOptionsAppendExecutionProvider_V2(&options, env, &match, 1,
                                                                    keys.data(), values.data(),
                                                                    keys.size()),
                       error);
}

} // namespace

bool attachExecutionProvider(const OrtApi& api,
                             OrtSessionOptions& options,
                             const IProviderFactory& factory,
                             const IProviderFactory::OptionList& optionsToApply,
                             std::string& error)
{
    const std::uint32_t apiVersion = ortRuntimeStatus().apiVersion;
    const auto opaqueResult = [&](bool ok) {
        if (!ok) error = std::string(factory.epName()) + ": " + error
            + "; install the matching GPU runtime and its dependencies";
        return ok;
    };
    // OrtApi is append-only; do not read these fields on an older API table.
    if (apiVersion >= 11) {
        if (factory.provider() == Provider::Cuda && api.CreateCUDAProviderOptions
            && api.UpdateCUDAProviderOptions && api.SessionOptionsAppendExecutionProvider_CUDA_V2
            && api.ReleaseCUDAProviderOptions)
            return opaqueResult(attachOpaqueOptions<OrtCUDAProviderOptionsV2>(api, options, optionsToApply,
                api.CreateCUDAProviderOptions, api.UpdateCUDAProviderOptions,
                api.SessionOptionsAppendExecutionProvider_CUDA_V2, api.ReleaseCUDAProviderOptions, error));
        if (factory.provider() == Provider::TensorRt && api.CreateTensorRTProviderOptions
            && api.UpdateTensorRTProviderOptions && api.SessionOptionsAppendExecutionProvider_TensorRT_V2
            && api.ReleaseTensorRTProviderOptions)
            return opaqueResult(attachOpaqueOptions<OrtTensorRTProviderOptionsV2>(api, options, optionsToApply,
                api.CreateTensorRTProviderOptions, api.UpdateTensorRTProviderOptions,
                api.SessionOptionsAppendExecutionProvider_TensorRT_V2, api.ReleaseTensorRTProviderOptions, error));
    }
    if (apiVersion >= kEpDeviceApiVersion) {
        if (attachViaEpDeviceApi(api, options, factory, optionsToApply, error)) return true;
        // Official GPU builds can expose built-in EPs only via classic factories.
    }

    const char* symbol = providerLegacyExportName(factory.provider());
    auto append = symbol ? reinterpret_cast<AppendProviderFn>(ortExportedSymbol(symbol))
                         : nullptr;
    if (!append) {
        error = "onnxruntime " + ortRuntimeStatus().version + " (OrtApi v"
            + std::to_string(apiVersion) + ") exposes no "
            + (symbol ? symbol : "provider entry point")
            + "; no compatible EP device or classic factory was found: install the matching GPU runtime";
        return false;
    }

    // Older runtimes can still select a device, but must not pretend to apply
    // richer cache/tuning options that this classic ABI cannot represent.
    for (const auto& [key, value] : optionsToApply) {
        (void)value;
        if (key != "device_id") {
            error = "runtime has no compatible provider-options API for '" + key
                + "'; install a newer matching GPU runtime";
            return false;
        }
    }
    if (factory.provider() == Provider::Dml) {
        if (!checkStatus(api, api.DisableMemPattern(&options), error)
            || !checkStatus(api, api.SetSessionExecutionMode(&options, ORT_SEQUENTIAL), error)) return false;
    }
    error.clear();
    return checkStatus(api, append(&options, deviceIdFrom(optionsToApply)), error);
}

bool IProviderFactory::configure(const OrtApi& api,
                                 OrtSessionOptions& options,
                                 const OptionList& optionsToApply,
                                 std::string& error) const
{
    return attachExecutionProvider(api, options, *this, optionsToApply, error);
}

void registerProviderFactory(std::unique_ptr<IProviderFactory> factory)
{
    if (!factory) {
        return;
    }
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    installBuiltinsLocked(r);

    const int key = static_cast<int>(factory->provider());
    if (const auto it = r.byProvider.find(key); it != r.byProvider.end()) {
        r.factories[it->second] = std::move(factory);
        return;
    }
    r.byProvider[key] = r.factories.size();
    r.factories.push_back(std::move(factory));
}

const IProviderFactory* findProviderFactory(Provider provider)
{
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    installBuiltinsLocked(r);

    const auto it = r.byProvider.find(static_cast<int>(provider));
    if (it == r.byProvider.end()) {
        return nullptr;
    }
    return r.factories[it->second].get();
}

std::vector<const IProviderFactory*> providerFactories()
{
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    installBuiltinsLocked(r);

    std::vector<const IProviderFactory*> ordered;
    ordered.reserve(r.factories.size());

    // Fallback order first — that is the order the prober and the UI rely on.
    for (const Provider p : kFallbackOrder) {
        const auto it = r.byProvider.find(static_cast<int>(p));
        if (it != r.byProvider.end()) {
            ordered.push_back(r.factories[it->second].get());
        }
    }
    // Then anything custom, in registration order, so an extension can never
    // jump the queue by accident.
    for (const auto& factory : r.factories) {
        if (providerIndexInFallback(factory->provider()) < 0) {
            ordered.push_back(factory.get());
        }
    }
    return ordered;
}

void installBuiltinProviderFactories()
{
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    installBuiltinsLocked(r);
}

void resetProviderFactoriesForTesting()
{
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    r.factories.clear();
    r.byProvider.clear();
    r.builtinsInstalled = false;
    installBuiltinsLocked(r);
}

} // namespace pfgpu
