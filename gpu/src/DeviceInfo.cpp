#include "pfgpu/DeviceInfo.hpp"

#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "pfgpu/ProbeModel.hpp"
#include "pfgpu/ProviderFactory.hpp"

#if defined(_WIN32)
#include <windows.h>
#if defined(PFGPU_HAS_DXGI)
#include <dxgi1_2.h>
#endif
#endif

namespace pfgpu {
namespace {

// ── Display adapters (DXGI) ─────────────────────────────────────────────────
// Used for human-readable device names in the badge/About. GPU *selection* is a
// provider option (device_id), not this list.
struct DisplayAdapter {
    std::string name;
    std::uint64_t dedicatedVideoMemoryBytes = 0;
    std::uint32_t vendorId = 0;
};

constexpr std::uint32_t kVendorNvidia = 0x10DE;

#if defined(_WIN32) && defined(PFGPU_HAS_DXGI)
template <typename T>
void releaseCom(T*& pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

std::string wideToUtf8(const wchar_t* text)
{
    if (!text || !*text) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                          nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}
#endif

std::vector<DisplayAdapter> displayAdapters()
{
    std::vector<DisplayAdapter> adapters;
#if defined(_WIN32) && defined(PFGPU_HAS_DXGI)
    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        return adapters;
    }
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) != S_OK || !adapter) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc {};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            DisplayAdapter info;
            info.name = wideToUtf8(desc.Description);
            info.dedicatedVideoMemoryBytes = desc.DedicatedVideoMemory;
            info.vendorId = desc.VendorId;
            adapters.push_back(std::move(info));
        }
        releaseCom(adapter);
    }
    releaseCom(factory);
#endif
    return adapters;
}

const DisplayAdapter* firstAdapterFor(Provider provider,
                                      const std::vector<DisplayAdapter>& adapters)
{
    if (adapters.empty()) {
        return nullptr;
    }
    if (provider == Provider::Cuda || provider == Provider::TensorRt) {
        for (const DisplayAdapter& adapter : adapters) {
            if (adapter.vendorId == kVendorNvidia) {
                return &adapter;
            }
        }
        return nullptr; // no NVIDIA GPU: the probe will fail anyway
    }
    // DirectML works against the default adapter of the machine.
    return &adapters.front();
}

std::string cpuDeviceName()
{
    const unsigned threads = std::thread::hardware_concurrency();
    if (threads == 0) {
        return "cpu";
    }
    return "cpu (" + std::to_string(threads) + " threads)";
}

// ── ORT session probe ───────────────────────────────────────────────────────
// A provider is "available" only if a real session can be created with it
// attached. OrtApi::GetAvailableProviders is not used for the decision because
// it documents that listed providers may still fail to initialize.
bool probeProvider(const OrtApi& api, const IProviderFactory& factory, std::string& error)
{
    OrtEnv* env = sharedOrtEnv(error);
    if (!env) {
        return false;
    }

    OrtSessionOptions* options = nullptr;
    if (!checkStatus(api, api.CreateSessionOptions(&options), error)) {
        return false;
    }
    struct OptionsGuard {
        const OrtApi* api;
        OrtSessionOptions* options;
        ~OptionsGuard()
        {
            if (options) {
                api->ReleaseSessionOptions(options);
            }
        }
    } guard { &api, options };

    // The probe must not be slowed down by optimisation passes or thread pools;
    // it only has to prove that the EP initializes and can run a graph.
    if (!checkStatus(api, api.SetSessionGraphOptimizationLevel(options, ORT_ENABLE_BASIC),
                     error)) {
        return false;
    }
    if (!checkStatus(api, api.SetIntraOpNumThreads(options, 1), error)) {
        return false;
    }
    if (!factory.configure(api, *options, factory.defaultOptions(), error)) {
        return false;
    }

    const std::string_view model = probeModelBytes();
    OrtSession* session = nullptr;
    if (!checkStatus(api,
                     api.CreateSessionFromArray(env, model.data(), model.size(), options,
                                                &session),
                     error)) {
        return false;
    }
    api.ReleaseSession(session);
    return true;
}

BackendProbe runProbe()
{
    BackendProbe probe;
    const OrtRuntimeStatus& runtime = ortRuntimeStatus();
    probe.ortLoaded = runtime.loaded;
    probe.ortVersion = runtime.version;
    probe.ortApiVersion = runtime.apiVersion;
    probe.ortError = runtime.error;

    const OrtApi* api = runtime.loaded ? ortApi() : nullptr;
    const std::vector<DisplayAdapter> adapters = displayAdapters();

    for (const Provider provider : kFallbackOrder) {
        BackendStatus status;
        status.provider = provider;

        if (provider == Provider::Cpu) {
            // CPU is part of the runtime itself: available exactly when the
            // runtime is.
            status.available = probe.ortLoaded;
            status.deviceName = cpuDeviceName();
            if (!status.available) {
                status.reason = probe.ortError;
            }
            probe.backends.push_back(std::move(status));
            continue;
        }

        if (!api) {
            status.reason = probe.ortError.empty() ? std::string("onnxruntime.dll not loaded")
                                                   : probe.ortError;
            probe.backends.push_back(std::move(status));
            continue;
        }

        const IProviderFactory* factory = findProviderFactory(provider);
        if (!factory) {
            status.reason = "no provider factory registered";
            probe.backends.push_back(std::move(status));
            continue;
        }

        if (const DisplayAdapter* adapter = firstAdapterFor(provider, adapters)) {
            status.deviceName = adapter->name;
        }

        std::string error;
        status.available = probeProvider(*api, *factory, error);
        if (!status.available) {
            status.reason = error;
            if (status.deviceName.empty()) {
                // Keep the message honest: the EP may be missing because the
                // provider DLL is absent, not necessarily because of hardware.
                status.deviceName = factory->epName();
            }
        } else if (status.deviceName.empty()) {
            status.deviceName = factory->epName();
        }
        probe.backends.push_back(std::move(status));
    }
    return probe;
}

struct ProbeState {
    std::mutex mutex;
    bool done = false;
    BackendProbe value;
};

ProbeState& probeState()
{
    static ProbeState state;
    return state;
}

} // namespace

const BackendProbe& probeBackends()
{
    ProbeState& state = probeState();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.done) {
        state.value = runProbe();
        state.done = true;
    }
    return state.value;
}

void resetBackendProbeForTesting()
{
    ProbeState& state = probeState();
    const std::lock_guard<std::mutex> lock(state.mutex);
    state.done = false;
    state.value = BackendProbe {};
}

Provider defaultProvider()
{
    const BackendProbe& probe = probeBackends();
    for (const BackendStatus& status : probe.backends) {
        if (status.available) {
            return status.provider;
        }
    }
    return Provider::Cpu;
}

Provider resolveProvider(Provider requested)
{
    if (requested != Provider::Auto) {
        return requested;
    }
    return defaultProvider();
}

bool isProviderAvailable(Provider provider)
{
    if (provider == Provider::Auto) {
        return probeBackends().ortLoaded;
    }
    const BackendStatus* status = findBackendStatus(provider);
    return status ? status->available : false;
}

const BackendStatus* findBackendStatus(Provider provider)
{
    const BackendProbe& probe = probeBackends();
    for (const BackendStatus& status : probe.backends) {
        if (status.provider == provider) {
            return &status;
        }
    }
    return nullptr;
}

std::vector<DeviceInfo> getDeviceInfo()
{
    const BackendProbe& probe = probeBackends();
    std::vector<DeviceInfo> devices;
    for (const BackendStatus& status : probe.backends) {
        if (!status.available) {
            continue;
        }
        DeviceInfo info;
        info.name = status.deviceName.empty() ? providerName(status.provider)
                                              : status.deviceName;
        info.backend = providerName(status.provider);
        devices.push_back(std::move(info));
    }
    return devices;
}

} // namespace pfgpu
