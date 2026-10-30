#pragma once
#include <pfservices/ProviderStore.hpp>
#include <pfservices/ModelStore.hpp>
#include <QThread>
#include <atomic>
#include <memory>
#include <numeric>
#include <algorithm>

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
            || error.starts_with("model download: HTTP 429") || error.starts_with("model SHA-256 mismatch");
        if(!transient)return false;
        if(retry)retry(attempt+1);
        for(std::size_t tick=0;tick<(attempt+1)*10;++tick) {
            if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_DOWNLOAD_CANCELLED";return false;}
            QThread::msleep(50);
        }
    }
    return false;
}

// Manifest parts carry authoritative hashes. At most four independent replies
// run at once; only the parent reports aggregate progress. Worker destruction
// always joins, including when a callback throws or another chunk fails.
inline bool downloadProviderParts(const ProviderAsset& asset,const std::filesystem::path& root,
    ProviderDownloadProgress progress,std::string& error,const PackageDownload& download,
    const PackageRetry& retry={},std::size_t maximumConnections=4) {
    std::vector<std::atomic_uint64_t> received(asset.parts.size());
    std::vector<std::atomic_int> outcomes(asset.parts.size());
    std::vector<std::string> errors(asset.parts.size());
    maximumConnections=std::clamp<std::size_t>(maximumConnections,1,4);
    for(std::size_t first=0;first<asset.parts.size();first+=maximumConnections) {
        struct Workers {
            std::vector<std::unique_ptr<QThread>> threads;
            ~Workers() {for(auto& thread:threads)thread->requestInterruption();for(auto& thread:threads)thread->wait();}
        } workers;
        const auto end=std::min(asset.parts.size(),first+maximumConnections);
        for(std::size_t index=first;index<end;++index) {
            workers.threads.emplace_back(QThread::create([&,index] {
                try {
                    const auto& part=asset.parts[index];
                    ModelAsset chunk{part.archive,part.sha256,part.sizeBytes,part.downloadUrl};
                    const bool ok=downloadWithRetry(chunk,root/part.archive,[&,index](auto count,auto) {
                        received[index].store(std::min(count,asset.parts[index].sizeBytes),std::memory_order_relaxed);
                    },errors[index],download,retry);
                    if(ok)received[index].store(part.sizeBytes,std::memory_order_relaxed);
                    outcomes[index].store(ok?1:-1,std::memory_order_release);
                } catch(const std::exception& exception) {
                    errors[index]=exception.what();outcomes[index].store(-1,std::memory_order_release);
                } catch(...) {errors[index]="PROVIDER_DOWNLOAD_WORKER_FAILED";outcomes[index].store(-1,std::memory_order_release);}
            }));
            workers.threads.back()->start();
        }
        bool active=true;
        while(active) {
            const bool cancelled=QThread::currentThread()->isInterruptionRequested();
            bool failed=false;
            for(std::size_t i=first;i<end;++i)if(outcomes[i].load(std::memory_order_acquire)<0)failed=true;
            active=false;
            for(auto& thread:workers.threads) {
                if(cancelled||failed)thread->requestInterruption();
                if(!thread->wait(25))active=true;
            }
            std::uint64_t total=0;
            for(const auto& count:received)total+=count.load(std::memory_order_relaxed);
            if(progress)progress(total,asset.sizeBytes);
        }
        if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_DOWNLOAD_CANCELLED";return false;}
        for(std::size_t i=first;i<end;++i)if(outcomes[i].load(std::memory_order_acquire)<0) {
            error=errors[i];return false;
        }
    }
    return true;
}
}
