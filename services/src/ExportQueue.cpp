#include <pfservices/ExportQueue.hpp>
#include <QFile>
#include <QSaveFile>
#include <map>
#include <tuple>

namespace pfservices {
ExportBatchResult runExportQueue(const std::vector<CutRequest>& jobs,
    std::stop_token stop, ExportProgress progress, const std::string& executable)
{
    ExportBatchResult batch;
    batch.completed.reserve(jobs.size());
    const CutService cutter(executable);
    using Key = std::tuple<std::filesystem::path, double, double, CutMode, int, int,
        int, int, int, int, double, bool, bool>;
    struct CompletedCut { std::filesystem::path path; CutResult result; };
    std::map<Key, CompletedCut> completedCuts;
    for (const auto& job : jobs) {
        if (stop.stop_requested()) { batch.cancelled = true; break; }
        try {
            auto request = job;
            request.stopToken = stop;
            if (request.prepare) {
                auto prepare = std::move(request.prepare);
                prepare(request);
            }
            if (stop.stop_requested()) { batch.cancelled = true; break; }
            const Key key{request.inputPath, request.startSeconds, request.endSeconds, request.mode,
                request.quality, request.videoBitrateKbps, request.maxWidth, request.maxHeight,
                request.canvasWidth, request.canvasHeight, request.outputFrameRate,
                request.ensureStereoAudio, request.sourceHasAudio};
            const auto previous = completedCuts.find(key);
            CutResult result;
            std::error_code error;
            const bool replacesSource = std::filesystem::equivalent(request.inputPath, request.outputPath, error);
            if (previous != completedCuts.end() && !replacesSource
                && std::filesystem::is_regular_file(previous->second.path, error)) {
                QFile source(QString::fromStdWString(previous->second.path.wstring()));
                QSaveFile destination(QString::fromStdWString(request.outputPath.wstring()));
                if (previous->second.path == request.outputPath) result = previous->second.result;
                else if (source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly)) {
                    bool copied = true;
                    while (!source.atEnd() && !stop.stop_requested()) {
                        const auto bytes = source.read(1024 * 1024);
                        if (bytes.isEmpty() || destination.write(bytes) != bytes.size()) { copied = false; break; }
                    }
                    if (copied && !stop.stop_requested() && destination.commit()) result = previous->second.result;
                }
                if (result.success) {
                    result.reused = true;
                    result.arguments.clear();
                    if (request.progress) request.progress(request.endSeconds - request.startSeconds);
                }
            }
            if (!result.success) result = cutter.cut(request);
            if (result.success && !result.reused) {
                // A caller may reuse a filename for a different cut. Do not
                // leave older memo entries pointing at its replacement.
                std::erase_if(completedCuts, [&](const auto& entry) { return entry.second.path == request.outputPath; });
                completedCuts.insert_or_assign(key, CompletedCut{request.outputPath, result});
            }
            batch.completed.push_back(std::move(result));
        } catch (const std::exception& error) {
            CutResult failed;
            failed.error = error.what();
            batch.completed.push_back(std::move(failed));
        }
        if (progress) progress(batch.completed.size(), jobs.size());
    }
    batch.cancelled = batch.cancelled || stop.stop_requested();
    return batch;
}
}
