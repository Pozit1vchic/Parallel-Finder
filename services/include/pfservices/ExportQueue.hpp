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

// Synchronous queue runner, intended for a background worker. It runs a
// bounded pool of independent clips by default, spreads FFmpeg threads across
// active jobs, preserves result order, and reports failed items in progress.
using ExportProgress = std::function<void(std::size_t, std::size_t)>;
ExportBatchResult runExportQueue(const std::vector<CutRequest>& jobs,
    std::stop_token stop = {}, ExportProgress progress = {},
    const std::string& ffmpegExecutable = "ffmpeg");
}
