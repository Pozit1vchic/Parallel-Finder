#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <stop_token>
#include <mutex>
#include <optional>
#include <functional>
#include <QString>
#include <pfservices/FfmpegCapabilities.hpp>

namespace pfservices {

enum class CutMode {
    Exact,
    Fast,
    // Pixel/audio-preserving re-encode. Kept after the existing values so
    // older callers that pass Exact=0 or Fast=1 remain source-compatible.
    ExactLossless,
};

// Supported video containers for user initiated exports. Exact lossless
// output is restricted to MOV to preserve decoded pixels and audio samples.
[[nodiscard]] bool isSupportedVideoContainer(std::string_view extension);

struct CutRequest {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    CutMode mode = CutMode::Exact;
    // Constant quality by default; a positive bitrate selects target kb/s instead.
    int quality = 18;
    int videoBitrateKbps = 0;
    // Optional output ceiling. Zero leaves the source dimensions unchanged.
    // Resizing requires Exact re-encoding because Fast stream-copies and
    // ExactLossless preserves the source pixel dimensions.
    int maxWidth = 0;
    int maxHeight = 0;
    // Internal montage normalization. Ordinary clips leave these unset.
    int canvasWidth = 0;
    int canvasHeight = 0;
    double outputFrameRate = 0.0;
    // Internal lossless montage plan: native pixels on a padded canvas, VFR,
    // a deterministic lossless encoder and PCM. No bitrate limit or lossy encoder fallback is allowed.
    QString losslessPixelFormat;
    int losslessAudioRate = 0;
    int losslessAudioChannels = 0;
    int losslessDisplayWidth = 0;
    int losslessDisplayHeight = 0;
    bool ensureStereoAudio = false;
    bool sourceHasAudio = true;
    // Optional in-picture label for a result transition. Burned into the
    // output video, so it requires re-encoding and cannot be pixel-lossless.
    QString transitionSignature;
    // Queue-assigned share of the machine's encoder threads. Zero keeps the
    // standalone CutService default based on the logical CPU count.
    unsigned workerThreads = 0;
    std::stop_token stopToken;
    // Zero allows long source scenes; UI cancellation remains responsive.
    // Preview/automation callers may opt into a finite deadline.
    int timeoutMs = 0;
    // Abort a stuck encoder, not a slowly advancing or long clip. Zero disables.
    int stallTimeoutMs = 120000;
    // Called on the export worker, with encoded seconds relative to this clip.
    std::function<void(double)> progress;
    // Optional worker-side preparation, performed just before this clip is cut.
    // Large batches must not decode every scene boundary before writing clip 1.
    std::function<void(CutRequest&)> prepare;
};

struct CutResult {
    // Actual destination, including the collision suffix for public exports.
    std::filesystem::path outputPath;
    bool success = false;
    bool reused = false;
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
    [[nodiscard]] static std::vector<std::string> videoEncodingArguments(
        const std::string& encoder, int quality, int bitrateKbps, unsigned threads);

    [[nodiscard]] CutResult cut(const CutRequest& request) const;

private:
    std::string ffmpegExecutable_;
    mutable std::mutex capabilitiesMutex_;
    mutable std::optional<FfmpegCapabilities> capabilities_;
    mutable std::string preferredEncoder_;
};

} // namespace pfservices
