#pragma once

#include <cstddef>
#include <string>

namespace pfservices {

struct Settings {
    std::string provider = "auto";
    std::string language = "ru";
    std::string theme = "parallel-dark";
    std::string modelPath;
    std::string cachePath;
    std::size_t cacheLimitBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    // 0 lets ONNX Runtime choose a safe default for the current machine.
    std::size_t processingThreads = 0;
    double sceneThreshold = 27.0;
    std::size_t sceneMinFrames = 8;
    double sceneAdaptiveMultiplier = 3.0;
    double similarityThreshold = 0.78;
    double candidateThreshold = 0.50;
    double minRepeatGapSec = 6.0;
    double sameFileGapSec = 2.0;
    double crossFileGapSec = 0.0;
    double duplicateWindowSec = 1.5;
    double noiseFactor = 1.0;
    std::size_t maxUniqueResults = 100;
    double timeWeight = 0.25;
    double sakoeChibaRatio = 0.10;
    std::string qualityProfile = "maximum";
    // motion, static, clips or combined. Kept as a string for forward-
    // compatible settings files and direct user editing.
    std::string analysisMode = "motion";
    bool normalizeSize = true;
    bool mirrorPoses = true;
    // Explicit costume matching: do not infer actor identity from a mask.
    bool costumeMode = false;
};

class SettingsStore {
public:
    static std::string defaultDirectory();
    static std::string defaultPath();

    explicit SettingsStore(std::string path = {});

    const std::string& path() const noexcept { return path_; }
    Settings load(std::string& error) const;
    bool save(const Settings& settings, std::string& error) const;

private:
    std::string path_;
};

} // namespace pfservices
