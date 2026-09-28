#include <pfservices/ExportQueue.hpp>

namespace pfservices {
ExportBatchResult runExportQueue(const std::vector<CutRequest>& jobs,
    std::stop_token stop, ExportProgress progress, const std::string& executable)
{
    ExportBatchResult batch;
    batch.completed.reserve(jobs.size());
    const CutService cutter(executable);
    for (const auto& job : jobs) {
        if (stop.stop_requested()) { batch.cancelled = true; break; }
        try {
            auto request = job;
            request.stopToken = stop;
            batch.completed.push_back(cutter.cut(request));
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
