#include "pfgpu/OrtRuntime.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pfgpu {
namespace {

// OrtApi is append-only: newer versions add fields at the end, so a struct
// obtained for version N is safe to use for every entry point that exists in N.
// The runtime's minor version is also its maximum API version.  Using that
// hint avoids asking GetApi() for an API newer than the loaded DLL (which
// prints a noisy warning before returning null on ORT 1.17.x).
std::uint32_t runtimeApiHint(std::string_view version)
{
    const auto dot = version.find('.');
    if (dot == std::string_view::npos) return ORT_API_VERSION;
    const auto begin = dot + 1;
    const auto end = version.find('.', begin);
    const auto minorText = version.substr(begin, end == std::string_view::npos
        ? std::string_view::npos : end - begin);
    if (minorText.empty()) return ORT_API_VERSION;
    std::uint32_t minor = 0;
    for (const char digit : minorText) {
        if (digit < '0' || digit > '9') return ORT_API_VERSION;
        minor = minor * 10u + static_cast<std::uint32_t>(digit - '0');
    }
    return minor > 0 ? std::min<std::uint32_t>(minor, ORT_API_VERSION) : ORT_API_VERSION;
}

std::pair<const OrtApi*, std::uint32_t> selectApiVersion(const OrtApiBase& apiBase,
                                                         std::uint32_t maxVersion)
{
    const OrtApi* selected = nullptr;
    std::uint32_t selectedVersion = 0;
    const auto upperBound = std::max(kMinSupportedApiVersion,
                                     std::min(maxVersion, static_cast<std::uint32_t>(ORT_API_VERSION)));
    for (std::uint32_t version = kMinSupportedApiVersion; version <= upperBound;
         ++version) {
        const OrtApi* candidate = apiBase.GetApi(version);
        if (!candidate) {
            break; // nothing newer is supported either
        }
        selected = candidate;
        selectedVersion = version;
    }
    return { selected, selectedVersion };
}

// Environment override for the GPU bundle: lets a developer point at
// D:\PF_CUDA\onnxruntime.dll without touching PATH. Empty = default search
// (application directory first, then PATH), which is what the portable release
// relies on.
constexpr const char* kRuntimePathEnvVar = "PF_ORT_DLL";

struct RuntimeState {
    std::once_flag once;
    bool attempted = false;
    OrtRuntimeStatus status;
    const OrtApi* api = nullptr;
#if defined(_WIN32)
    HMODULE library = nullptr;
#endif
};

RuntimeState& state()
{
    static RuntimeState s;
    return s;
}

#if defined(_WIN32)
// Returns the loaded module and fills `error` on failure. Never throws.
HMODULE loadLibrary(const std::filesystem::path& path, std::string& error)
{
    ::SetLastError(ERROR_SUCCESS);
    HMODULE module = ::LoadLibraryW(path.wstring().c_str());
    if (module) {
        return module;
    }
    const DWORD code = ::GetLastError();
    error = "LoadLibraryW(\"" + path.string() + "\") failed, GetLastError="
        + std::to_string(code);
    return nullptr;
}

void loadRuntime(RuntimeState& s)
{
    std::filesystem::path candidate = L"onnxruntime.dll";
    bool explicitPath = false;
    if (const char* override_path = std::getenv(kRuntimePathEnvVar);
        override_path && *override_path) {
        candidate = std::filesystem::path(override_path);
        explicitPath = true;
    }

    HMODULE module = loadLibrary(candidate, s.status.error);
    if (!module) {
        if (explicitPath) {
            s.status.error += " (from " + std::string(kRuntimePathEnvVar) + ")";
        } else {
            s.status.error += "; set PF_ORT_DLL to point at the runtime";
        }
        return;
    }

    using GetApiBaseFn = OrtApiBase* (*)();
    auto getApiBase = reinterpret_cast<GetApiBaseFn>(
        reinterpret_cast<void*>(::GetProcAddress(module, "OrtGetApiBase")));
    if (!getApiBase) {
        s.status.error = "onnxruntime.dll has no OrtGetApiBase export";
        ::FreeLibrary(module);
        return;
    }

    const OrtApiBase* apiBase = getApiBase();
    if (!apiBase || !apiBase->GetApi || !apiBase->GetVersionString) {
        s.status.error = "OrtApiBase from onnxruntime.dll is malformed";
        ::FreeLibrary(module);
        return;
    }

    s.status.version = apiBase->GetVersionString() ? apiBase->GetVersionString() : "";

    const auto [api, apiVersion] = selectApiVersion(*apiBase, runtimeApiHint(s.status.version));
    if (api) {
        s.api = api;
        s.status.apiVersion = apiVersion;
        s.status.loaded = true;
        s.library = module; // kept loaded on purpose
        return;
    }

    s.status.error = "onnxruntime.dll " + s.status.version
        + " exposes no OrtApi >= " + std::to_string(kMinSupportedApiVersion)
        + " (configured EP by name needs ORT 1.12+)";
    ::FreeLibrary(module);
}
#else
void loadRuntime(RuntimeState& s)
{
    s.status.error = "only Windows hosts are supported in v1";
}
#endif

} // namespace

const OrtRuntimeStatus& ortRuntimeStatus()
{
    RuntimeState& s = state();
    std::call_once(s.once, [&s] {
        s.attempted = true;
        loadRuntime(s);
    });
    return s.status;
}

const OrtApi* ortApi()
{
    (void)ortRuntimeStatus();
    return state().api;
}

bool ortRuntimeAvailable()
{
    return ortRuntimeStatus().loaded;
}

std::string ortRuntimeVersion()
{
    return ortRuntimeStatus().version;
}

void* ortExportedSymbol(const char* name)
{
#if defined(_WIN32)
    RuntimeState& s = state();
    (void)ortRuntimeStatus(); // ensure the library is loaded
    if (!s.library || !name) {
        return nullptr;
    }
    return reinterpret_cast<void*>(::GetProcAddress(s.library, name));
#else
    (void)name;
    return nullptr;
#endif
}

OrtEnv* sharedOrtEnv(std::string& error)
{
    struct Holder {
        std::mutex mutex;
        OrtEnv* env = nullptr;
    };
    static Holder holder;

    const OrtApi* api = ortApi();
    if (!api) {
        error = ortRuntimeStatus().error.empty() ? std::string("onnxruntime.dll not loaded")
                                                 : ortRuntimeStatus().error;
        return nullptr;
    }

    const std::lock_guard<std::mutex> lock(holder.mutex);
    if (!holder.env) {
        if (!checkStatus(*api,
                         api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "parallel-finder",
                                        &holder.env),
                         error)) {
            holder.env = nullptr;
            return nullptr;
        }
    }
    return holder.env;
}

bool checkStatus(const OrtApi& api, OrtStatus* status, std::string& error)
{
    if (!status) {
        return true;
    }
    const char* message = api.GetErrorMessage(status);
    error = message ? message : "unknown ONNX Runtime error";
    api.ReleaseStatus(status);
    return false;
}

} // namespace pfgpu
