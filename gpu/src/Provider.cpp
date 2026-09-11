#include "pfgpu/Provider.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace pfgpu {
namespace {

std::string toLower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

const char* providerName(Provider p) noexcept
{
    switch (p) {
    case Provider::Auto: return "auto";
    case Provider::TensorRt: return "tensorrt";
    case Provider::Cuda: return "cuda";
    case Provider::Dml: return "dml";
    case Provider::Cpu: return "cpu";
    }
    return "cpu";
}

const char* providerEpName(Provider p) noexcept
{
    switch (p) {
    case Provider::Auto: return "";
    case Provider::TensorRt: return "TensorrtExecutionProvider";
    case Provider::Cuda: return "CUDAExecutionProvider";
    case Provider::Dml: return "DmlExecutionProvider";
    // CPU has no shared provider library: it is built into the runtime and is
    // always the implicit EP, so it is never appended by name.
    case Provider::Cpu: return "CPUExecutionProvider";
    }
    return "";
}

const char* providerLegacyExportName(Provider p) noexcept
{
    switch (p) {
    case Provider::TensorRt: return "OrtSessionOptionsAppendExecutionProvider_TensorRT";
    case Provider::Cuda: return "OrtSessionOptionsAppendExecutionProvider_CUDA";
    case Provider::Dml: return "OrtSessionOptionsAppendExecutionProvider_DML";
    // CPU and Auto have no such entry point.
    case Provider::Auto:
    case Provider::Cpu: return nullptr;
    }
    return nullptr;
}

std::optional<Provider> parseProvider(std::string_view text) noexcept
{
    const std::string lowered = toLower(text);
    if (lowered == "auto") {
        return Provider::Auto;
    }
    if (lowered == "tensorrt" || lowered == "trt") {
        return Provider::TensorRt;
    }
    if (lowered == "cuda" || lowered == "gpu") {
        return Provider::Cuda;
    }
    if (lowered == "dml" || lowered == "directml") {
        return Provider::Dml;
    }
    if (lowered == "cpu") {
        return Provider::Cpu;
    }
    return std::nullopt;
}

} // namespace pfgpu
