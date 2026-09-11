#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pfgpu/OrtRuntime.hpp"
#include "pfgpu/Provider.hpp"

namespace pfgpu {

struct DeviceInfo {
    std::string name;    // "NVIDIA GeForce RTX 4070", "cpu (24 threads)"
    std::string backend; // "tensorrt" | "cuda" | "dml" | "cpu"
};

// One link of the fallback chain, after probing this machine.
struct BackendStatus {
    Provider provider = Provider::Cpu;
    bool available = false;
    std::string reason;     // why it is unusable ("" when available)
    std::string deviceName; // human-readable device, when known
};

struct BackendProbe {
    bool ortLoaded = false;
    std::string ortVersion;
    std::uint32_t ortApiVersion = 0;
    std::string ortError;                // runtime-level failure, when any
    std::vector<BackendStatus> backends; // kFallbackOrder, CPU last
};

// Probes the machine once per process and memoizes the result. The probe
// creates throwaway sessions (see ProbeModel.hpp), which is cheap but not free,
// so it is never repeated on the hot path.
const BackendProbe& probeBackends();

// Test-only: forgets the memoized probe so a test can observe a fresh run.
void resetBackendProbeForTesting();

// First usable provider in kFallbackOrder. CPU is the last resort and is
// reported even when ONNX Runtime itself could not be loaded (the badge then
// shows the runtime error).
Provider defaultProvider();

// Auto resolves through the probe, concrete providers pass through unchanged.
Provider resolveProvider(Provider requested);

bool isProviderAvailable(Provider provider);

// nullptr when the provider is not part of the fallback chain.
const BackendStatus* findBackendStatus(Provider provider);

// Available backends as display entries, used by the UI badge and About.
std::vector<DeviceInfo> getDeviceInfo();

} // namespace pfgpu
