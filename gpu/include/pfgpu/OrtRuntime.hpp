#pragma once

#include <cstdint>
#include <string>

// pfgpu includes the ORT *headers* (types only) but never links the library.
// The runtime is resolved at run time through LoadLibraryW + OrtGetApiBase, so
// the same binary drives whatever onnxruntime.dll the user has (see
// docs/decisions.md: "Загрузка ORT и выбор провайдера").
#include <onnxruntime_c_api.h>

namespace pfgpu {

// How the runtime was resolved.
struct OrtRuntimeStatus {
    bool loaded = false;
    std::string version;          // e.g. "1.26.0"
    std::uint32_t apiVersion = 0; // OrtApi version actually obtained
    std::string error;            // human-readable reason when !loaded
};

// Lowest OrtApi version we can drive. OrtApi::SessionOptionsAppendExecutionProvider
// (configuring an execution provider by name, no per-EP header needed) is
// documented as "\since Version 1.12" in onnxruntime_c_api.h. Below that we
// refuse the runtime and stay CPU-less rather than guess struct offsets.
inline constexpr std::uint32_t kMinSupportedApiVersion = 12;

// OrtApi::GetEpDevices / EpDevice_EpName / SessionOptionsAppendExecutionProvider_V2
// are documented as "\since Version 1.22" in onnxruntime_c_api.h. Runtimes older
// than that are configured through the classic exported per-EP entry points
// instead (Provider.hpp: providerLegacyExportName).
inline constexpr std::uint32_t kEpDeviceApiVersion = 22;

// Memoized probe. Thread-safe; the library is loaded once and intentionally
// never unloaded (ORT keeps background thread pools alive).
const OrtRuntimeStatus& ortRuntimeStatus();

// nullptr unless ortRuntimeStatus().loaded.
const OrtApi* ortApi();

bool ortRuntimeAvailable();

// Version string of the loaded runtime, "" when unavailable.
std::string ortRuntimeVersion();

// Turns an OrtStatus into (ok, message) and releases it. The message text comes
// from the runtime itself, which is what we surface in the UI and in test
// failures.
bool checkStatus(const OrtApi& api, OrtStatus* status, std::string& error);

// Exported symbol from the already loaded onnxruntime.dll, or nullptr when the
// runtime is absent or the symbol does not exist. Used for the classic
// OrtSessionOptionsAppendExecutionProvider_<EP> entry points.
void* ortExportedSymbol(const char* name);

// Process-wide OrtEnv, created on first use. Intentionally never released: ORT
// keeps background threads alive, and tearing the environment down at shutdown
// races with them. Returns nullptr and fills `error` when the runtime is
// unavailable.
OrtEnv* sharedOrtEnv(std::string& error);

} // namespace pfgpu
