#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <onnxruntime_c_api.h>

#include "pfgpu/Provider.hpp"

namespace pfgpu {

// The abstraction the spec asks for (section 2): a new GPU provider is added by
// registering one more factory — pfcore and the rest of pfgpu stay untouched.
class IProviderFactory {
public:
    using OptionList = std::vector<std::pair<std::string, std::string>>;

    virtual ~IProviderFactory() = default;

    virtual Provider provider() const noexcept = 0;

    // ORT execution provider name (Provider.hpp: providerEpName).
    virtual const char* epName() const noexcept = 0;

    // Provider options merged in before the caller's ones. Empty by default.
    virtual OptionList defaultOptions() const { return {}; }

    // Attaches this EP to the session options. Reports failure through `error`
    // instead of throwing: the caller turns that into the fallback-chain
    // decision and into the badge tooltip. Default implementation is
    // attachExecutionProvider(); CPU overrides it with a no-op.
    virtual bool configure(const OrtApi& api,
                           OrtSessionOptions& options,
                           const OptionList& optionsToApply,
                           std::string& error) const;
};

// Shared attach logic, used by every GPU factory (exposed so tests and future
// providers exercise exactly the same code path):
//
//  * CUDA/TensorRT — their public opaque V2 provider-option APIs are preferred
//    so startup/performance knobs and TensorRT disk caches are available.
//  * ORT >= 1.22 — EP-device API remains the generic provider path.
//  * older/unusual GPU packages — the classic exported entry point
//    OrtSessionOptionsAppendExecutionProvider_<EP>(options, device_id), resolved
//    with GetProcAddress, so still no per-EP headers and no link-time ORT.
//
// Legacy public provider structs are not used; only the opaque V2 option
// objects are used for CUDA/TensorRT, avoiding struct ABI coupling.
//
// The by-name OrtApi::SessionOptionsAppendExecutionProvider is also not used: it
// only knows a fixed list of providers (OPENVINO/SNPE/XNNPACK/QNN/WEBNN/AZURE)
// and rejects CUDA/DML/TensorRT by name.
bool attachExecutionProvider(const OrtApi& api,
                             OrtSessionOptions& options,
                             const IProviderFactory& factory,
                             const IProviderFactory::OptionList& optionsToApply,
                             std::string& error);

// Registry. Built-ins are installed on first access. Registering a factory for
// a provider that already has one replaces it (last wins) — that is how the GPU
// bundle or a test can override behaviour without touching call sites.
void registerProviderFactory(std::unique_ptr<IProviderFactory> factory);

const IProviderFactory* findProviderFactory(Provider provider);

// kFallbackOrder first (skipping providers without a factory), then any custom
// providers in registration order.
std::vector<const IProviderFactory*> providerFactories();

// TensorRT, CUDA, DirectML, CPU.
void installBuiltinProviderFactories();

// Test-only: drops every registration and reinstalls the built-ins, so a test
// that overrides a factory cannot leak that override into the next one.
void resetProviderFactoriesForTesting();

} // namespace pfgpu
