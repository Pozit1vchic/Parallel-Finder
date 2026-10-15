#pragma once

#include <pfservices/CutService.hpp>
#include <functional>
#include <stop_token>
#include <vector>

namespace pfservices {
struct ExportBatchResult {
    std::vector<CutResult> completed;
    bool cancelled = false;
};

// Synchronous queue runner, intended for a background worker. Concurrency is
// adapts to CPU, available memory and source resolution (at most four encoders).
// Preparation and callbacks are serialized; results retain the input order.
using ExportProgress = std::function<void(std::size_t, std::size_t)>;
ExportBatchResult runExportQueue(const std::vector<CutRequest>& jobs,
    std::stop_token stop = {}, ExportProgress progress = {},
    const std::string& ffmpegExecutable = "ffmpeg");
}
