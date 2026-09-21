#include "pfgpu/OrtRuntime.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr const char* kProviderRootEnvVar = "PF_PROVIDER_ROOT";

struct RuntimeState {
    std::once_flag once;
    bool attempted = false;
    OrtRuntimeStatus status;
    const OrtApi* api = nullptr;
#if defined(_WIN32)
    HMODULE library = nullptr;
#endif
};

bool safeProviderName(const std::string& provider)
{
    return provider == "dml" || provider == "cuda"
        || provider == "tensorrt" || provider == "cpu";
}

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
    // Keep dependency resolution next to the selected runtime instead of the
    // current working directory/PATH.  This prevents a stray DLL from being
    // injected when the app is launched from a writable folder.
    HMODULE module = path.is_absolute()
        ? ::LoadLibraryExW(path.wstring().c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                           | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        : ::LoadLibraryW(path.wstring().c_str());
    if (module) {
        return module;
    }
    const DWORD code = ::GetLastError();
    error = "LoadLibraryW(\"" + path.string() + "\") failed, GetLastError="
        + std::to_string(code);
    return nullptr;
}

// Provider archives are allowed to contain a small directory wrapper (for
// example `onnxruntime-win-x64-gpu/onnxruntime.dll`).  Resolve that layout
// without requiring the user to edit PATH or move files by hand.  Keep the
// search bounded to the provider directory and never follow symlinks.
std::optional<std::filesystem::path> findRuntimeIn(const std::filesystem::path& root)
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) return std::nullopt;
    if (std::filesystem::is_symlink(root, error)) return std::nullopt;
    const auto direct = root / "onnxruntime.dll";
    if (!std::filesystem::is_symlink(direct, error)
        && std::filesystem::is_regular_file(direct, error)) return direct;

    std::filesystem::directory_options options =
        std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(root, options, error), end;
    std::size_t visited = 0;
    for (; it != end && !error && visited < 256; it.increment(error), ++visited) {
        if (it->is_symlink(error)) continue;
        if (!it->is_regular_file(error)) continue;
        if (it->path().filename() == "onnxruntime.dll") return it->path();
    }
    return std::nullopt;
}

void loadRuntime(RuntimeState& s)
{
    std::vector<std::filesystem::path> candidates;
    bool explicitPath = false;
    if (const char* override_path = std::getenv(kRuntimePathEnvVar);
        override_path && *override_path) {
        candidates.emplace_back(override_path);
        explicitPath = true;
    } else {
        if (const char* root = std::getenv(kProviderRootEnvVar); root && *root) {
            const auto overrideRoot = std::filesystem::u8path(root);
            if (const auto overrideRuntime = findRuntimeIn(overrideRoot))
                candidates.push_back(*overrideRuntime);
        }
        wchar_t modulePath[MAX_PATH] {};
        const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            const auto appDirectory = std::filesystem::path(modulePath).parent_path();
            const auto appRuntime = appDirectory / "onnxruntime.dll";
            // Provider bundles downloaded by the UI are installed side by
            // side under <app>/providers/<name>.  They are deliberately
            // considered only on the next process start: ONNX Runtime cannot
            // attach a new EP after its DLL has already been loaded.
            const auto providerRoot = appDirectory / "providers";
            std::error_code iterationError;
            if (std::filesystem::is_directory(providerRoot, iterationError)) {
                // Prefer the bundle explicitly selected by the user.  This
                // marker is written only after a verified archive is fully
                // extracted, so a partial download can never hijack startup.
                std::ifstream active(providerRoot / "active.txt");
                std::string activeProvider;
                std::getline(active, activeProvider);
                if (safeProviderName(activeProvider)) {
                    if (const auto activeRuntime = findRuntimeIn(providerRoot / activeProvider))
                        candidates.push_back(*activeRuntime);
                }
                for (const auto& entry : std::filesystem::directory_iterator(providerRoot, iterationError)) {
                    if (iterationError) break;
                    if (entry.is_symlink(iterationError)) continue;
                    if (!entry.is_directory(iterationError)) continue;
                    if (const auto bundledRuntime = findRuntimeIn(entry.path()))
                        candidates.push_back(*bundledRuntime);
                }
            }
            if (std::filesystem::is_regular_file(appRuntime)) candidates.push_back(appRuntime);
        }
        candidates.emplace_back(L"onnxruntime.dll");
    }

    HMODULE module = nullptr;
    std::string lastError;
    for (const auto& candidate : candidates) {
        module = loadLibrary(candidate, lastError);
        if (module) break;
    }
    if (!module) {
        s.status.error = lastError;
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
