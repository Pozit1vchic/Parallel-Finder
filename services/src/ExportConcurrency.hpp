#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pfservices::detail {

struct ExportBudget {
    std::size_t workers = 0;
    unsigned threads = 0;
};

inline std::uint64_t availableMemoryBytes() noexcept
{
    #ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 0;
    #else
    return 0;
    #endif
}

// Bound concurrent decoders by both CPU and estimated decoded-frame working
// set. A single worker is always allowed, even when memory cannot be queried.
inline ExportBudget exportBudget(std::size_t jobs,
    unsigned logicalCores = std::thread::hardware_concurrency(),
    std::uint64_t availableBytes = 0,
    std::uint64_t pixels = 3840ull * 2160ull) noexcept
{
    if (jobs == 0) return {};
    logicalCores = std::max(1u, logicalCores);
    constexpr std::uint64_t bytesPerPixel = 1024;
    if (pixels == 0 || pixels > UINT64_MAX / bytesPerPixel) pixels = UINT64_MAX;
    const auto bytesPerWorker = pixels * bytesPerPixel;
    const auto memoryWorkers = availableBytes ? std::max<std::uint64_t>(1, availableBytes / bytesPerWorker) : 1;
    const auto cpuWorkers = std::max(1u, logicalCores / 8u);
    const auto workers = std::min<std::size_t>({jobs, cpuWorkers, 8u,
        static_cast<std::size_t>(std::min<std::uint64_t>(memoryWorkers, SIZE_MAX))});
    const auto threads = std::clamp((logicalCores + static_cast<unsigned>(workers) - 1)
        / static_cast<unsigned>(workers), 1u, 8u);
    return {workers, threads};
}

} // namespace pfservices::detail
