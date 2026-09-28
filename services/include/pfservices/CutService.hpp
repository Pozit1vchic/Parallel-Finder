#pragma once

#include <filesystem>
#include <string>
#include <stop_token>
#include <mutex>
#include <optional>
#include <pfservices/FfmpegCapabilities.hpp>

namespace pfservices {

enum class CutMode {
    Exact,
    Fast,
};

struct CutRequest {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    CutMode mode = CutMode::Exact;
    // Optional output ceiling. Zero leaves the source dimensions unchanged.
    // Resizing requires Exact mode because Fast mode stream-copies frames.
    int maxWidth = 0;
    int maxHeight = 0;
    std::stop_token stopToken;
    // Zero allows long source scenes; UI cancellation remains responsive.
    // Preview/automation callers may opt into a finite deadline.
    int timeoutMs = 0;
};

struct CutResult {
    bool success = false;
    int exitCode = -1;
    std::string encoder;
    std::string error;
    bool cancelled = false;
    std::string executable;
    std::vector<std::string> arguments;
};

class CutService {
public:
    explicit CutService(std::string ffmpegExecutable = "ffmpeg");

    // Runs ffmpeg without a shell. Output is written to a sibling temporary
    // file that keeps the media extension (for example clip.part.mp4) and is
    // moved into place only after a successful process exit.
    [[nodiscard]] CutResult cut(const CutRequest& request) const;

private:
    std::string ffmpegExecutable_;
    mutable std::mutex capabilitiesMutex_;
    mutable std::optional<FfmpegCapabilities> capabilities_;
};

} // namespace pfservices
