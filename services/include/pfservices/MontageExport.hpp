#pragma once
#include <pfservices/CutService.hpp>
#include <pfservices/ExportQueue.hpp>

namespace pfservices {
using MontageProgress = std::function<void(std::size_t completed, std::size_t total, int clipPercent)>;
// Sorts A/B segments by source time, then source path; removes identical
// source/range duplicates. Encodes sequentially, normalizes to the first
// clip's display size/FPS, preserves audio (silence for silent sources),
// concatenates by stream copy, and atomically installs only a complete MP4.
CutResult exportChronologicalMontage(std::vector<CutRequest> clips,
    const std::filesystem::path& output, std::stop_token stop = {},
    MontageProgress progress = {}, const std::string& executable = "ffmpeg");
}
