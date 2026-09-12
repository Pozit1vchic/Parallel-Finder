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
        return "path:" + normalizePath(model.path);
    }
    const std::size_t hash = std::hash<std::string> {}(model.bytes);
    return "mem:" + model.tag + ":" + std::to_string(hash) + ":"
        + std::to_string(model.bytes.size());
}

std::string buildCacheKey(const ModelRef& model, const SessionKey& key, Provider resolved)
{
    return modelIdentity(model) + "|" + providerName(resolved) + "|device"
        + std::to_string(key.deviceId) + "|" + key.profile;
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
    OrtSession* session = nullptr;
    Provider provider = Provider::Cpu;
    std::string cacheKey;
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
            result.handle.provider = it->second.entry->provider;
            result.handle.cacheKey = cacheKey;
            result.handle.createdNow = false;
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

    if (!checkStatus(*api, api->SetIntraOpNumThreads(options, 0), error)
        || !checkStatus(*api, api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL),
                        error)
        || !factory->configure(*api, *options, factory->defaultOptions(), error)) {
        result.error = "session options: " + error;
        return result;
    }

    OrtSession* session = nullptr;
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

    auto entry = std::shared_ptr<Entry>(new Entry { session, resolved, cacheKey },
                                        [api](Entry* raw) {
                                            if (raw->session) {
                                                api->ReleaseSession(raw->session);
                                            }
                                            delete raw;
                                        });

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
    result.handle.session = session;
    result.handle.owner = entry;
    result.handle.provider = resolved;
    result.handle.cacheKey = cacheKey;
    result.handle.createdNow = true;
    return result;
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
