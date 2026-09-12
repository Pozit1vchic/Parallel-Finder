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
    double sceneThreshold = 27.0;
    std::size_t sceneMinFrames = 8;
    double sceneAdaptiveMultiplier = 3.0;
    double similarityThreshold = 0.85;
    double candidateThreshold = 0.55;
    double minRepeatGapSec = 6.0;
    double sameFileGapSec = 2.0;
    double crossFileGapSec = 0.0;
    double duplicateWindowSec = 1.5;
    double noiseFactor = 1.0;
    std::size_t maxUniqueResults = 100;
    double timeWeight = 0.25;
    double sakoeChibaRatio = 0.10;
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
