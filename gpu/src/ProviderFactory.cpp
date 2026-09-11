#include "pfgpu/ProviderFactory.hpp"

#include <cstring>
#include <mutex>
#include <unordered_map>

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
    if (apiVersion >= kEpDeviceApiVersion) {
        return attachViaEpDeviceApi(api, options, factory, optionsToApply, error);
    }

    const char* symbol = providerLegacyExportName(factory.provider());
    auto append = symbol ? reinterpret_cast<AppendProviderFn>(ortExportedSymbol(symbol))
                         : nullptr;
    if (!append) {
        error = "onnxruntime " + ortRuntimeStatus().version + " (OrtApi v"
            + std::to_string(apiVersion) + ") exposes no "
            + (symbol ? symbol : "provider entry point")
            + " and has no EP-device API (needs ORT 1.22+): install the GPU build of the runtime";
        return false;
    }

    // The classic entry point takes only a device id; richer options (TRT
    // engine cache, cuDNN tuning) require the OrtApi struct variants, which we
    // deliberately do not depend on — see ProviderFactory.hpp. Our built-in
    // defaults are device_id only, so nothing is silently dropped in practice.
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
