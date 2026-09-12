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
    double sceneThreshold = 0.30;
    std::size_t sceneMinFrames = 8;
    double sceneAdaptiveMultiplier = 3.0;
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
