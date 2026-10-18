#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>

namespace pfservices::detail {

struct ExportBudget {
    std::size_t workers = 0;
    unsigned threadsPerWorker = 0;
};

// Run several independent clips at once while splitting FFmpeg's per-process
// CPU budget across the pool. The process cap bounds decoder/model memory for
// large 4K export batches; one clip can still use several CPU threads.
inline ExportBudget exportBudget(std::size_t jobs, unsigned logicalCores = std::thread::hardware_concurrency()) noexcept
{
    if (jobs == 0) return {};
    logicalCores = std::max(1u, logicalCores);
    const auto workers = std::min<std::size_t>({jobs, logicalCores, 8u});
    const auto threads = std::clamp(
        (logicalCores + static_cast<unsigned>(workers) - 1) / static_cast<unsigned>(workers), 1u, 8u);
    return {workers, threads};
}

} // namespace pfservices::detail
