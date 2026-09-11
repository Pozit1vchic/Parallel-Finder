#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace pfgpu {

// Inference providers, in fallback order: TensorRT -> CUDA -> DirectML -> CPU
// (spec section 1). Auto = probe the machine, pick the best usable one.
enum class Provider {
    Auto,
    TensorRt,
    Cuda,
    Dml,
    Cpu,
};

// "auto" | "tensorrt" | "cuda" | "dml" | "cpu". These identifiers live in
// settings.json and in the CLI, they are never localized.
const char* providerName(Provider p) noexcept;

// Execution provider name as ONNX Runtime spells it, "" for Auto. Passed to
// OrtApi::SessionOptionsAppendExecutionProvider, so the exact spelling matters
// (it is part of the ORT public API, not ours).
const char* providerEpName(Provider p) noexcept;

// Classic per-EP entry point exported by ORT < 1.22 GPU packages, e.g.
// "OrtSessionOptionsAppendExecutionProvider_CUDA". Resolved with GetProcAddress
// at run time, so pfgpu still needs no per-EP headers. Returns nullptr for Auto
// and CPU.
const char* providerLegacyExportName(Provider p) noexcept;

// Case-insensitive parse for settings/CLI values. std::nullopt for unknown text
// — the caller decides whether that is fatal.
std::optional<Provider> parseProvider(std::string_view text) noexcept;

// Fallback order from the spec. CPU is the guaranteed last resort.
inline constexpr std::array<Provider, 4> kFallbackOrder {
    Provider::TensorRt,
    Provider::Cuda,
    Provider::Dml,
    Provider::Cpu,
};

} // namespace pfgpu
