#pragma once

#include <filesystem>
#include <string>

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
};

struct CutResult {
    bool success = false;
    int exitCode = -1;
    std::string encoder;
    std::string error;
};

class CutService {
public:
    explicit CutService(std::string ffmpegExecutable = "ffmpeg");

    // Runs ffmpeg without a shell. Output is written to a sibling .part file
    // and moved into place only after a successful process exit.
    [[nodiscard]] CutResult cut(const CutRequest& request) const;

private:
    std::string ffmpegExecutable_;
};

} // namespace pfservices
