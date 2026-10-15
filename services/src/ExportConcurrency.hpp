#pragma once
#include <pfservices/CutService.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <limits>
#include <vector>
#include <QString>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
namespace pfservices::detail {
struct ExportBudget { unsigned workers = 1, threads = 1; };
inline ExportBudget exportBudget(std::size_t jobs, unsigned cpu, std::uint64_t availableBytes,
    std::uint64_t pixels) {
    cpu = std::max(1u, cpu);
    // Decoder references, filters, lossless surfaces and encoder queues all
    // coexist. Keep half of available RAM free for the application/OS.
    constexpr std::uint64_t overhead = 256ull * 1024 * 1024;
    const auto perWorker = pixels > (std::numeric_limits<std::uint64_t>::max()-overhead)/96
        ? std::numeric_limits<std::uint64_t>::max() : overhead + pixels*96;
    const auto memoryWorkers = availableBytes ? std::max<std::uint64_t>(1, availableBytes / 2 / perWorker) : 1;
    const auto workers = static_cast<unsigned>(std::min<std::uint64_t>({
        std::max<std::size_t>(1, jobs), 4, std::max(1u, cpu / 4), memoryWorkers}));
    return {workers, std::clamp(cpu / workers / 2, 1u, 8u)};
}
inline std::uint64_t availableExportMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX state{}; state.dwLength = sizeof(state);
    if (GlobalMemoryStatusEx(&state)) return state.ullAvailPhys;
#elif defined(__linux__)
    const auto pages = sysconf(_SC_AVPHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) return static_cast<std::uint64_t>(pages) * pageSize;
#endif
    return 0; // Unknown memory: one safe worker, never optimistic allocation.
}
inline ExportBudget exportBudget(const std::vector<CutRequest>& jobs, std::stop_token stop = {}) {
    std::uint64_t pixels = 3840ull * 2160; // Conservative for unreadable metadata.
    bool unknownMetadata = false;
    std::vector<std::filesystem::path> checked;
    for (const auto& job : jobs) {
        if (stop.stop_requested()) break;
        if (std::find(checked.begin(), checked.end(), job.inputPath) != checked.end()) continue;
        checked.push_back(job.inputPath);
        try {
            pfcore::VideoDecoder decoder;
            decoder.open(QString::fromStdWString(job.inputPath.wstring()).toUtf8().toStdString());
            const auto info = decoder.info();
            const auto sourcePixels = static_cast<std::uint64_t>(std::max(info.width, 0)) * std::max(info.height, 0);
            if (checked.size() == 1) pixels = sourcePixels;
            else pixels = std::max(pixels, sourcePixels);
        } catch (...) { unknownMetadata = true; }
        if (job.stopToken.stop_requested()) break;
    }
    return exportBudget(jobs.size(), std::thread::hardware_concurrency(), unknownMetadata ? 0 : availableExportMemory(), pixels);
}
}
