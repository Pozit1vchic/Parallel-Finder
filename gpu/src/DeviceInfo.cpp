#include "pfgpu/DeviceInfo.hpp"

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>

// Minimal read-only mirror of OrtApiBase (onnxruntime_c_api.h, exact order):
//   struct OrtApiBase {
//     const OrtApi* (ORT_API_CALL* GetApi)(uint32_t version);
//     const char* (ORT_API_CALL* GetVersionString)(void);
//   };
// We only need GetVersionString (second member), so a local struct keeps
// pfgpu fully free of ORT headers and works across ORT versions.
namespace {
struct OrtApiBaseView {
    void* (*GetApi)(std::uint32_t);
    const char* (*GetVersionString)(void);
};

struct OrtRuntime {
    HMODULE library = nullptr;
    OrtApiBaseView* apiBase = nullptr;
};

bool loadOrtRuntime(OrtRuntime& out)
{
    if (out.library && out.apiBase) {
        return true;
    }
    if (!out.library) {
        out.library = ::LoadLibraryW(L"onnxruntime.dll");
        if (!out.library) {
            return false;
        }
    }
    auto getApiBase = reinterpret_cast<OrtApiBaseView* (*)()>(
        reinterpret_cast<void*>(::GetProcAddress(out.library, "OrtGetApiBase")));
    if (!getApiBase) {
        ::FreeLibrary(out.library);
        out.library = nullptr;
        return false;
    }
    out.apiBase = getApiBase();
    if (!out.apiBase || !out.apiBase->GetVersionString) {
        ::FreeLibrary(out.library);
        out.library = nullptr;
        out.apiBase = nullptr;
        return false;
    }
    return true;
}
} // namespace

#endif // _WIN32

namespace pfgpu {

const char* providerName(Provider p)
{
    switch (p) {
    case Provider::Auto: return "auto";
    case Provider::Cuda: return "cuda";
    case Provider::Dml: return "dml";
    case Provider::Cpu: return "cpu";
    }
    return "cpu";
}

std::vector<DeviceInfo> getDeviceInfo()
{
    // TODO(1): enumerate real devices — ORT provider factory (CUDA/TensorRT/
    // DirectML availability) + display adapters. Fallback chain:
    // TensorRT -> CUDA -> DirectML -> CPU.
    return {DeviceInfo{"cpu", "cpu"}};
}

Provider defaultProvider()
{
    // TODO(1): probe ORT providers via OrtGetApiBase()->GetApi(...) and pick
    // the best available in the fallback order. Until then: CPU only.
    return Provider::Cpu;
}

bool ortRuntimeAvailable()
{
#if defined(_WIN32)
    OrtRuntime rt;
    return loadOrtRuntime(rt);
#else
    // Non-Windows hosts are not a target for v1; keep the API honest.
    return false;
#endif
}

std::string ortRuntimeVersion()
{
#if defined(_WIN32)
    OrtRuntime rt;
    if (!loadOrtRuntime(rt)) {
        return {};
    }
    const char* v = rt.apiBase->GetVersionString();
    return v ? std::string(v) : std::string();
#else
    return {};
#endif
}

} // namespace pfgpu


