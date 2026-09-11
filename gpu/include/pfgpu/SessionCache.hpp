#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <onnxruntime_c_api.h>

#include "pfgpu/Provider.hpp"

namespace pfgpu {

// What to load. Exactly one of `path` / `bytes` is used; in-memory models exist
// so tests (and the probe) never touch the filesystem.
struct ModelRef {
    std::string path;
    std::string bytes;
    std::string tag; // diagnostics + identity for in-memory models

    static ModelRef fromPath(std::string path);
    static ModelRef fromBytes(std::string bytes, std::string tag = {});

    bool isPath() const noexcept { return !path.empty(); }
};

// Session identity: model + provider + device + profile. Two different model
// profiles ("b1" / "b8", see docs/decisions.md D1) must never share a session.
struct SessionKey {
    Provider provider = Provider::Auto;
    int deviceId = 0;
    std::string profile;

    friend bool operator==(const SessionKey&, const SessionKey&) = default;
};

// Borrowed session. The cache owns it; `owner` keeps the entry alive even if
// the cache evicts or clears the entry meanwhile.
struct SessionHandle {
    OrtSession* session = nullptr;
    std::shared_ptr<const void> owner;
    Provider provider = Provider::Cpu; // effective provider (Auto resolved)
    std::string cacheKey;             // diagnostics for logs and tests
    bool createdNow = false;

    explicit operator bool() const noexcept { return session != nullptr; }
};

// Session cache keyed by SessionKey (spec section 2: "ORT-сессии (модель|провайдер)").
// Thread-safe: JobManager (stage 3c) serializes by model path, but the UI thread
// may query the cache concurrently, so every operation takes the lock.
class SessionCache {
public:
    static constexpr std::size_t kDefaultMaxEntries = 4;

    struct Result {
        bool ok = false;
        std::string error;
        SessionHandle handle;
    };

    struct Stats {
        std::size_t live = 0;
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t evictions = 0;
        std::size_t created = 0;
    };

    explicit SessionCache(std::size_t maxEntries = kDefaultMaxEntries);
    ~SessionCache();

    SessionCache(const SessionCache&) = delete;
    SessionCache& operator=(const SessionCache&) = delete;
    SessionCache(SessionCache&&) = delete;
    SessionCache& operator=(SessionCache&&) = delete;

    // Creates the session or returns the cached one. Never throws: provider and
    // runtime failures come back as Result::error (the message is ORT's own).
    Result getOrCreate(const ModelRef& model, const SessionKey& key = {});

    Stats stats();
    void clear();

    std::size_t maxEntries() const;
    void setMaxEntries(std::size_t maxEntries); // throws std::invalid_argument on 0

private:
    struct Entry;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pfgpu
