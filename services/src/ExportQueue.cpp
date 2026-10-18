#include <pfservices/ExportQueue.hpp>
#include "ExportDestination.hpp"
#include "ExportConcurrency.hpp"
#include "ExportQueueInternal.hpp"
#include <chrono>
#include <thread>
#include <QFile>
#include <QSaveFile>
#include <deque>
#include <future>
#include <map>
#include <set>
#include <tuple>

namespace pfservices {
ExportBatchResult detail::runExportQueueWithExecutor(const std::vector<CutRequest>& jobs,
    ExportBudget budget, CutExecutor execute, std::stop_token stop, ExportProgress progress)
{
    ExportBatchResult batch;
    batch.completed.reserve(jobs.size());
    if (stop.stop_requested()) { batch.cancelled = true; return batch; }
    budget.workers = std::max(1u, budget.workers);
    std::vector<std::optional<CutResult>> indexedResults(jobs.size());
    std::size_t completedCount = 0;
    using Key = std::tuple<std::filesystem::path, double, double, CutMode, int, int,
        int, int, int, int, double, bool, bool, std::string, std::filesystem::path>;
    struct CompletedCut { std::filesystem::path path; CutResult result; };
    std::map<Key, CompletedCut> completedCuts;
    // Futures must be joined before their callback lock is destroyed, even
    // if an external progress callback throws during stack unwinding.
    std::mutex callbackMutex;
    struct Pending { std::size_t index; Key key; std::shared_ptr<detail::ExportDestination> destination; std::future<CutResult> future; };
    std::deque<Pending> pending;
    std::set<Key> activeKeys;
    const auto finishOne = [&] {
        // Reap whichever worker is ready, never hold free slots behind a
        // slow earlier clip. Public results are assembled by input index.
        auto ready = pending.end();
        while (ready == pending.end()) {
            ready = std::find_if(pending.begin(), pending.end(), [](auto& item) {
                return item.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
            });
            if (ready == pending.end()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        auto item = std::move(*ready); pending.erase(ready);
        CutResult result;
        try { result = item.future.get(); }
        catch (const std::exception& error) { result.error = error.what(); }
        catch (...) { result.error = "unexpected export worker failure"; }
        activeKeys.erase(item.key);
        if (result.success) {
            item.destination->commit();
            result.outputPath = item.destination->path();
            if (!result.reused) completedCuts.insert_or_assign(item.key, CompletedCut{result.outputPath, result});
        }
        indexedResults[item.index] = std::move(result);
        if (progress) { std::lock_guard lock(callbackMutex); progress(++completedCount, jobs.size()); }
        else ++completedCount;
    };
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index];
        if (stop.stop_requested()) break;
        while (pending.size() >= budget.workers) finishOne();
        if (stop.stop_requested()) break;
        try {
            auto request = job;
            request.stopToken = stop;
            if (request.prepare) { auto prepare = std::move(request.prepare); prepare(request); }
            if (stop.stop_requested()) break;
            const Key key{request.inputPath, request.startSeconds, request.endSeconds, request.mode,
                request.quality, request.videoBitrateKbps, request.maxWidth, request.maxHeight,
                request.canvasWidth, request.canvasHeight, request.outputFrameRate,
                request.ensureStereoAudio, request.sourceHasAudio,
                request.transitionSignature.toUtf8().toStdString(), request.outputPath.extension()};
            // A duplicate waits for the first encode, then copies its bytes.
            while (activeKeys.contains(key)) finishOne();
            if (stop.stop_requested()) break;
            auto destination = std::make_shared<detail::ExportDestination>(request.outputPath);
            request.outputPath = destination->path();
            request.workerThreads = budget.threads;
            if (request.progress) {
                auto callback = std::move(request.progress);
                request.progress = [&, callback = std::move(callback)](double seconds) {
                    std::lock_guard lock(callbackMutex); callback(seconds);
                };
            }
            std::optional<CompletedCut> previous;
            if (auto found = completedCuts.find(key); found != completedCuts.end()) previous = found->second;
            pending.push_back({index, key, destination, std::async(std::launch::async,
                [&, request = std::move(request), previous]() mutable {
                    CutResult result;
                    try {
                        if (previous) {
                            QFile source(QString::fromStdWString(previous->path.wstring()));
                            QSaveFile target(QString::fromStdWString(request.outputPath.wstring()));
                            if (source.open(QIODevice::ReadOnly) && target.open(QIODevice::WriteOnly)) {
                                bool copied = true;
                                while (!source.atEnd() && !stop.stop_requested()) {
                                    const auto bytes = source.read(1024 * 1024);
                                    if (bytes.isEmpty() || target.write(bytes) != bytes.size()) { copied = false; break; }
                                }
                                if (copied && !stop.stop_requested() && target.commit()) {
                                    result = previous->result; result.reused = true; result.arguments.clear();
                                    if (request.progress) request.progress(request.endSeconds - request.startSeconds);
                                }
                            }
                        }
                        if (!result.success) result = execute(request);
                    } catch (const std::exception& error) { result.error = error.what(); }
                    return result;
                })});
            activeKeys.insert(key);
        } catch (const std::exception& error) {
            CutResult failed; failed.error = error.what(); indexedResults[index] = std::move(failed);
            if (progress) { std::lock_guard lock(callbackMutex); progress(++completedCount, jobs.size()); }
            else ++completedCount;
        }
    }
    while (!pending.empty()) finishOne();
    // Every started/prepared job has an index; unstarted cancellation tail
    // stays absent. Completions may arrive in any order without reordering A/B.
    for (auto& result : indexedResults) if (result) batch.completed.push_back(std::move(*result));
    batch.cancelled = stop.stop_requested();
    return batch;
}

ExportBatchResult runExportQueue(const std::vector<CutRequest>& jobs,
    std::stop_token stop, ExportProgress progress, const std::string& executable)
{
    const auto budget = detail::exportBudget(jobs, stop);
    const CutService cutter(executable);
    return detail::runExportQueueWithExecutor(jobs, budget,
        [&](const CutRequest& request) { return cutter.cut(request); }, stop, std::move(progress));
}
}
