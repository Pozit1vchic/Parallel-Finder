#include "pfgpu/SessionCache.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <atomic>
#include <cstdlib>
#include <cstdio>

#include "pfgpu/DeviceInfo.hpp"
#include "pfgpu/OrtRuntime.hpp"
#include "pfgpu/ProviderFactory.hpp"

namespace pfgpu {
namespace {

std::string normalizePath(const std::string& path)
{
    // Only slash normalization: the cache key must be stable across the way the
    // path was spelled, but resolving symlinks/relative paths is the caller's
    // job (ModelStore resolves to an absolute path before loading).
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::string modelIdentity(const ModelRef& model)
{
    if (model.isPath()) {
        const std::filesystem::path path(model.path);
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(path, sizeError);
        std::error_code timeError;
        const auto writeTime = std::filesystem::last_write_time(path, timeError);
        // Include cheap file metadata so replacing an ONNX asset at the same
        // path cannot silently reuse an old ORT session.
        return "path:" + normalizePath(model.path) + "|size="
            + std::to_string(sizeError ? 0ULL : static_cast<unsigned long long>(size))
            + "|mtime=" + std::to_string(timeError ? 0LL
                : static_cast<long long>(writeTime.time_since_epoch().count()));
    }
    const std::size_t hash = std::hash<std::string> {}(model.bytes);
    return "mem:" + model.tag + ":" + std::to_string(hash) + ":"
        + std::to_string(model.bytes.size());
}

std::string buildCacheKey(const ModelRef& model, const SessionKey& key, Provider resolved)
{
    return modelIdentity(model) + "|" + providerName(resolved) + "|device"
        + std::to_string(key.deviceId) + "|" + key.profile + "|threads"
        + std::to_string(key.intraOpThreads);
}

std::size_t effectiveThreadCount(const SessionKey& key, Provider resolved)
{
    // Auto must not create an all-core CPU pool for every NVIDIA model. GPU work
    // is submitted by the calling thread; unused pools steal decode resources.
    // Explicit user counts and the CPU provider retain their existing policy.
    const bool nvidia = resolved == Provider::Cuda || resolved == Provider::TensorRt;
    return key.intraOpThreads == 0 && nvidia ? 1 : key.intraOpThreads;
}

} // namespace

ModelRef ModelRef::fromPath(std::string path)
{
    ModelRef ref;
    ref.path = std::move(path);
    ref.tag = ref.path;
    return ref;
}

ModelRef ModelRef::fromBytes(std::string bytes, std::string tag)
{
    ModelRef ref;
    ref.bytes = std::move(bytes);
    ref.tag = tag.empty() ? std::string("memory") : std::move(tag);
    return ref;
}

struct SessionCache::Entry {
    Entry(const OrtApi* runtime, OrtSession* value, Provider ep, const std::string& key)
        : api(runtime), session(value), provider(ep), cacheKey(key) {}
    ~Entry() { if (session) api->ReleaseSession(session); }
    const OrtApi* api;
    OrtSession* session = nullptr;
    Provider provider = Provider::Cpu;
    std::string cacheKey;
    std::shared_ptr<std::mutex> runMutex = std::make_shared<std::mutex>();
    mutable std::mutex profileMutex;
    mutable std::size_t profileRuns = 0;
    mutable std::atomic_bool profiling{false};
};

struct SessionCache::Impl {
    struct Slot {
        std::shared_ptr<Entry> entry;
        std::uint64_t stamp = 0;
    };

    mutable std::mutex mutex;
    std::unordered_map<std::string, Slot> sessions;
    std::uint64_t clock = 0;
    std::size_t maxEntries = SessionCache::kDefaultMaxEntries;
    Stats stats;
};

SessionCache::SessionCache(std::size_t maxEntries)
    : impl_(std::make_unique<Impl>())
{
    setMaxEntries(maxEntries);
}

SessionCache::~SessionCache()
{
    // Release through clear() while the api pointer is still valid; the runtime
    // is never unloaded, so plain destruction would be safe too.
    clear();
}

SessionCache::Result SessionCache::getOrCreate(const ModelRef& model, const SessionKey& key)
{
    Result result;

    const OrtApi* api = ortApi();
    if (!api) {
        result.error = ortRuntimeStatus().error.empty()
            ? std::string("onnxruntime.dll not loaded")
            : ortRuntimeStatus().error;
        return result;
    }

    const Provider resolved = resolveProvider(key.provider);
    if (!isProviderAvailable(resolved)) {
        // Manual selection must fail loudly instead of silently degrading: the
        // UI shows this message (spec section 1: ручной выбор auto/cuda/dml/cpu).
        const BackendStatus* status = findBackendStatus(resolved);
        result.error = std::string("provider '") + providerName(resolved)
            + "' is not available on this machine";
        if (status && !status->reason.empty()) {
            result.error += ": " + status->reason;
        }
        return result;
    }

    const std::string cacheKey = buildCacheKey(model, key, resolved);

    {
        Impl& impl = *impl_;
        const std::lock_guard<std::mutex> lock(impl.mutex);
        if (const auto it = impl.sessions.find(cacheKey); it != impl.sessions.end()) {
            impl.clock += 1;
            it->second.stamp = impl.clock;
            impl.stats.hits += 1;

            result.ok = true;
            result.handle.session = it->second.entry->session;
            result.handle.owner = it->second.entry;
            result.handle.runMutex = it->second.entry->runMutex;
            result.handle.provider = it->second.entry->provider;
            result.handle.cacheKey = cacheKey;
            result.handle.createdNow = false;
            result.handle.profiling = it->second.entry->profiling;
            return result;
        }
        impl.stats.misses += 1;
    }

    const IProviderFactory* factory = findProviderFactory(resolved);
    if (!factory) {
        result.error = std::string("no provider factory registered for '")
            + providerName(resolved) + "'";
        return result;
    }

    std::string error;
    OrtEnv* env = sharedOrtEnv(error);
    if (!env) {
        result.error = "OrtEnv: " + error;
        return result;
    }

    OrtSessionOptions* options = nullptr;
    if (!checkStatus(*api, api->CreateSessionOptions(&options), error)) {
        result.error = "OrtSessionOptions: " + error;
        return result;
    }

    struct Cleanup {
        const OrtApi* api;
        OrtSessionOptions* options;
        ~Cleanup()
        {
            if (options) {
                api->ReleaseSessionOptions(options);
            }
        }
    } cleanup { api, options };

    if (!checkStatus(*api, api->SetIntraOpNumThreads(options,
                                                     static_cast<int>(effectiveThreadCount(key, resolved))), error)
        || !checkStatus(*api, api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL),
                        error)
        || !factory->configure(*api, *options, factory->defaultOptions(), error)) {
        result.error = "session options: " + error;
        return result;
    }

    if ((resolved == Provider::Cuda || resolved == Provider::TensorRt)
        && (!checkStatus(*api, api->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"), error)
            || !checkStatus(*api, api->AddSessionConfigEntry(options, "session.inter_op.allow_spinning", "0"), error))) {
        result.error = "session thread policy: " + error;
        return result;
    }

    // TensorRT rarely owns every node in a real model. ORT's recommended
    // ordering is TensorRT first, CUDA second, CPU last. Without CUDA here,
    // unsupported TRT subgraphs silently fall all the way back to CPU, which
    // can erase the performance advantage of TensorRT. The TensorRT runtime
    // bundle ships the CUDA EP as the secondary provider.
    if (resolved == Provider::TensorRt) {
        if (const IProviderFactory* cuda = findProviderFactory(Provider::Cuda)) {
            std::string cudaFallbackError;
            // TensorRT itself is already valid; a missing optional CUDA
            // fallback must not make the whole session unusable.
            (void)cuda->configure(*api, *options, cuda->defaultOptions(), cudaFallbackError);
        }
    }

    const auto* profileRoot = std::getenv("PF_ORT_PROFILE_ROOT");
    const bool profiling = profileRoot && *profileRoot;
    if (profiling) {
        static std::atomic_uint64_t sequence{0};
        const std::filesystem::path directory(std::u8string(profileRoot, profileRoot + std::char_traits<char>::length(profileRoot)));
        std::error_code ioError;
        std::filesystem::create_directories(directory, ioError);
        if (ioError) { result.error = "profiling directory: " + ioError.message(); return result; }
        const auto prefix = directory / (std::string(providerName(resolved)) + "-" + std::to_string(sequence.fetch_add(1)));
        if (!checkStatus(*api, api->EnableProfiling(options, prefix.c_str()), error)) {
            result.error = "EnableProfiling: " + error; return result;
        }
    }

    OrtSession* session = nullptr;
    struct SessionCleanup {
        const OrtApi* api; OrtSession*& session;
        ~SessionCleanup() { if (session) api->ReleaseSession(session); }
    } sessionCleanup{api, session};
    if (model.isPath()) {
#if defined(_WIN32)
        const std::wstring widePath = std::filesystem::path(model.path).wstring();
        const ORTCHAR_T* modelPath = widePath.c_str();
#else
        const ORTCHAR_T* modelPath = model.path.c_str();
#endif
        if (!checkStatus(*api, api->CreateSession(env, modelPath, options, &session), error)) {
            result.error = "CreateSession(" + model.path + "): " + error;
            return result;
        }
    } else {
        if (!checkStatus(*api,
                         api->CreateSessionFromArray(env, model.bytes.data(),
                                                     model.bytes.size(), options, &session),
                         error)) {
            result.error = "CreateSessionFromArray: " + error;
            return result;
        }
    }

    auto entry = std::make_shared<Entry>(api, session, resolved, cacheKey);
    session = nullptr; // Entry now owns the session; allocation failures were guarded above.
    entry->profiling = profiling;

    {
        Impl& impl = *impl_;
        const std::lock_guard<std::mutex> lock(impl.mutex);
        impl.clock += 1;
        impl.stats.created += 1;
        impl.sessions[cacheKey] = Impl::Slot { entry, impl.clock };

        // LRU eviction by insertion/use stamp. Evicting only drops the map's
        // reference: a session still held by an in-flight handle stays alive
        // until that handle is released.
        while (impl.sessions.size() > impl.maxEntries) {
            auto victim = impl.sessions.end();
            for (auto it = impl.sessions.begin(); it != impl.sessions.end(); ++it) {
                if (it->first == cacheKey) {
                    continue; // never evict what we just created
                }
                if (victim == impl.sessions.end() || it->second.stamp < victim->second.stamp) {
                    victim = it;
                }
            }
            if (victim == impl.sessions.end()) {
                break;
            }
            impl.sessions.erase(victim);
            impl.stats.evictions += 1;
        }
    }

    result.ok = true;
    result.handle.session = entry->session;
    result.handle.owner = entry;
    result.handle.runMutex = entry->runMutex;
    result.handle.provider = resolved;
    result.handle.cacheKey = cacheKey;
    result.handle.createdNow = true;
    result.handle.profiling = profiling;
    return result;
}

void SessionCache::recordProfilingRun(const SessionHandle& handle)
{
    if (!handle.profiling || !handle.owner) return;
    const auto entry = std::static_pointer_cast<const Entry>(handle.owner);
    const std::lock_guard lock(entry->profileMutex);
    if (!entry->profiling || ++entry->profileRuns < 16) return;
    entry->profiling = false;
    const auto* api = ortApi();
    OrtAllocator* allocator = nullptr;
    std::string error;
    if (!checkStatus(*api, api->GetAllocatorWithDefaultOptions(&allocator), error)) return;
    char* path = nullptr;
    if (checkStatus(*api, api->SessionEndProfiling(entry->session, allocator, &path), error)) {
        std::fprintf(stderr, "PF_DEBUG_ORT_PROFILE runs=%zu key=%s path=%s\n", entry->profileRuns, entry->cacheKey.c_str(), path ? path : "");
    } else std::fprintf(stderr, "PF_DEBUG_ORT_PROFILE error=%s\n", error.c_str());
    if (path) {
        if (auto* status = api->AllocatorFree(allocator, path)) api->ReleaseStatus(status);
    }
}

SessionCache::Stats SessionCache::stats()
{
    Impl& impl = *impl_;
    const std::lock_guard<std::mutex> lock(impl.mutex);
    Stats copy = impl.stats;
    copy.live = impl.sessions.size();
    return copy;
}

void SessionCache::clear()
{
    Impl& impl = *impl_;
    std::unordered_map<std::string, Impl::Slot> dropped;
    {
        const std::lock_guard<std::mutex> lock(impl.mutex);
        dropped.swap(impl.sessions);
    }
    // Destruction of the entries (and therefore ReleaseSession) happens outside
    // the lock: releasing a session may block on EP teardown.
}

SessionCache& processSessionCache()
{
    // One active configuration is pose + ReID + two face sessions. Keeping
    // sixteen GPU arenas can consume many GiB after switching configurations.
    // Active handles stay alive independently of LRU eviction.
    static SessionCache cache(4);
    return cache;
}

std::size_t SessionCache::releaseUnused()
{
    std::vector<std::shared_ptr<Entry>> dropped;
    {
        const std::lock_guard lock(impl_->mutex);
        for (auto it = impl_->sessions.begin(); it != impl_->sessions.end();) {
            if (it->second.entry.use_count() == 1) {
                dropped.push_back(std::move(it->second.entry));
                it = impl_->sessions.erase(it);
            } else ++it;
        }
    }
    return dropped.size();
}

std::size_t SessionCache::maxEntries() const
{
    Impl& impl = *impl_;
    const std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.maxEntries;
}

void SessionCache::setMaxEntries(std::size_t maxEntries)
{
    if (maxEntries == 0) {
        throw std::invalid_argument("SessionCache: maxEntries must be >= 1");
    }
    Impl& impl = *impl_;
    const std::lock_guard<std::mutex> lock(impl.mutex);
    impl.maxEntries = maxEntries;
    while (impl.sessions.size() > impl.maxEntries) {
        auto victim = impl.sessions.end();
        for (auto it = impl.sessions.begin(); it != impl.sessions.end(); ++it) {
            if (victim == impl.sessions.end() || it->second.stamp < victim->second.stamp) {
                victim = it;
            }
        }
        if (victim == impl.sessions.end()) break;
        impl.sessions.erase(victim);
        impl.stats.evictions += 1;
    }
}

} // namespace pfgpu
