#pragma once
#include <pfservices/ProviderStore.hpp>
#include <pfservices/ModelStore.hpp>
#include <QThread>
#include <atomic>
#include <memory>
#include <numeric>
#include <algorithm>
#include <cstdio>

namespace pfservices::detail {
using PackageDownload = std::function<bool(const ModelAsset&,const std::filesystem::path&,DownloadProgress,std::string&)>;
using PackageRetry = std::function<void(std::size_t)>;

inline bool downloadWithRetry(const ModelAsset& asset,const std::filesystem::path& path,
    DownloadProgress progress,std::string& error,const PackageDownload& download,const PackageRetry& retry={}) {
    for(std::size_t attempt=0;attempt<3;++attempt) {
        if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_DOWNLOAD_CANCELLED";return false;}
        error.clear();
        if(download(asset,path,progress,error))return true;
        if(QThread::currentThread()->isInterruptionRequested())return false;
        if(attempt==2)return false;
        // Metadata/path errors cannot be fixed by retrying a network transfer.
        const bool transient=error.starts_with("download model:") || error.starts_with("model download: HTTP 5")
            || error.starts_with("model download: HTTP 429") || error.starts_with("model SHA-256 mismatch")
            || error == "model download: partial exceeds expected size";
        if(!transient)return false;
        if(retry)retry(attempt+1);
        for(std::size_t tick=0;tick<(attempt+1)*10;++tick) {
            if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_DOWNLOAD_CANCELLED";return false;}
            QThread::msleep(50);
        }
    }
    return false;
}

// Manifest parts carry authoritative hashes. Fill free slots without waiting
// for unrelated slow chunks. A retry halves the limit for NEW work; healthy
// in-flight replies drain normally. Two clean completions per current slot
// increase the limit by one, up to the caller's bound (never above four).
// Progress and concurrency notifications run only on the parent thread.
using PackageConcurrency = std::function<void(std::size_t)>;
inline bool downloadProviderParts(const ProviderAsset& asset,const std::filesystem::path& root,
    ProviderDownloadProgress progress,std::string& error,const PackageDownload& download,
    const PackageRetry& retry={},std::size_t maximumConnections=4,const PackageConcurrency& concurrency={}) {
    std::vector<std::atomic_uint64_t> received(asset.parts.size());
    std::vector<std::atomic_int> outcomes(asset.parts.size());
    std::vector<std::atomic_bool> retried(asset.parts.size());
    std::vector<std::string> errors(asset.parts.size());
    std::atomic_size_t retrySignals=0;
    maximumConnections=std::clamp<std::size_t>(maximumConnections,1,4);
    std::size_t limit=maximumConnections,next=0,seenRetries=0,cleanCompletions=0;
    struct Worker {std::size_t index;std::unique_ptr<QThread> thread;};
    struct Workers {
        std::vector<Worker> active;
        ~Workers() {for(auto& worker:active)worker.thread->requestInterruption();for(auto& worker:active)worker.thread->wait();}
    } workers;
    const auto setLimit=[&](std::size_t value,const char* reason) {
        limit=value;cleanCompletions=0;
        if(qEnvironmentVariableIsSet("PF_DEBUG_DOWNLOAD"))
            std::fprintf(stderr,"PF_PROVIDER_CONCURRENCY limit=%zu maximum=%zu reason=%s\n",limit,maximumConnections,reason);
        if(concurrency)concurrency(limit);
    };
    setLimit(limit,"initial");
    while(next<asset.parts.size()||!workers.active.empty()) {
        if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_DOWNLOAD_CANCELLED";return false;}
        const auto retryCount=retrySignals.load(std::memory_order_acquire);
        if(retryCount!=seenRetries) {setLimit(std::max<std::size_t>(1,limit/2),"retry");seenRetries=retryCount;}
        for(auto it=workers.active.begin();it!=workers.active.end();) {
            const auto index=it->index;
            const auto outcome=outcomes[index].load(std::memory_order_acquire);
            if(outcome<0) {error=errors[index];return false;}
            if(outcome==0||!it->thread->wait(0)) {++it;continue;}
            if(retried[index].load(std::memory_order_relaxed))cleanCompletions=0;
            else ++cleanCompletions;
            it=workers.active.erase(it);
            if(limit<maximumConnections&&cleanCompletions>=limit*2)setLimit(limit+1,"recovery");
        }
        while(next<asset.parts.size()&&workers.active.size()<limit) {
            const auto index=next++;
            auto thread=std::unique_ptr<QThread>(QThread::create([&,index] {
                try {
                    const auto& part=asset.parts[index];
                    ModelAsset chunk{part.archive,part.sha256,part.sizeBytes,part.downloadUrl};
                    const bool ok=downloadWithRetry(chunk,root/part.archive,[&,index](auto count,auto) {
                        received[index].store(std::min(count,asset.parts[index].sizeBytes),std::memory_order_relaxed);
                    },errors[index],download,[&,index](std::size_t attempt) {
                        retried[index].store(true,std::memory_order_relaxed);
                        retrySignals.fetch_add(1,std::memory_order_release);
                        if(qEnvironmentVariableIsSet("PF_DEBUG_DOWNLOAD"))
                            std::fprintf(stderr,"PF_PROVIDER_PART_RETRY part=%zu attempt=%zu\n",index,attempt);
                        if(retry)retry(attempt);
                    });
                    if(ok)received[index].store(part.sizeBytes,std::memory_order_relaxed);
                    outcomes[index].store(ok?1:-1,std::memory_order_release);
                } catch(const std::exception& exception) {
                    errors[index]=exception.what();outcomes[index].store(-1,std::memory_order_release);
                } catch(...) {errors[index]="PROVIDER_DOWNLOAD_WORKER_FAILED";outcomes[index].store(-1,std::memory_order_release);}
            }));
            workers.active.push_back({index,std::move(thread)});
            workers.active.back().thread->start();
        }
        std::uint64_t total=0;
        for(const auto& count:received)total+=count.load(std::memory_order_relaxed);
        if(progress)progress(total,asset.sizeBytes);
        // An event wait wakes immediately on completion. Sleep-based polling
        // adds a full Windows timer tick to every short/cached chunk wave.
        if(!workers.active.empty())workers.active.front().thread->wait(10);
    }
    return true;
}
}
