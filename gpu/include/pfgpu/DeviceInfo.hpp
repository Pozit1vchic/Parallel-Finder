#pragma once

#include <string>
#include <vector>

namespace pfgpu {

// Inference providers, in fallback order: TensorRT -> CUDA -> DirectML -> CPU.
// Auto = probe the machine, pick the best available.
enum class Provider {
    Auto,
    Cuda,
    Dml,
    Cpu,
};

struct DeviceInfo {
    std::string name;    // e.g. "NVIDIA GeForce RTX 4070", "cpu"
    std::string backend; // "tensorrt" | "cuda" | "dml" | "cpu"
};

const char* providerName(Provider p);

// Stage 0 stub: real enumeration (ORT provider factory + DXGI) lands in stage 1.
std::vector<DeviceInfo> getDeviceInfo();

// Stage 0 stub: with no ORT probing yet, CPU is the only safe default.
Provider defaultProvider();

// Version-agnostic ORT runtime probe: LoadLibraryW("onnxruntime.dll") +
// GetProcAddress("OrtGetApiBase"). No link-time ORT dependency at all.
bool ortRuntimeAvailable();

// ORT version string from OrtGetApiBase()->GetVersionString(), or "" if the
// runtime is not loadable.
std::string ortRuntimeVersion();

} // namespace pfgpu
