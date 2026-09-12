#pragma once

#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pfservices {

struct ModelAsset {
    std::string filename = "yolo26m-pose-640-b1.onnx";
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::string downloadUrl;
    std::string license;
    std::string minimumAppVersion;
};

using DownloadProgress = std::function<void(std::uint64_t received, std::uint64_t total)>;

class ModelStore {
public:
    // Searches local, explicitly configured locations and verifies optional
    // manifest size/hash metadata. Network download remains an explicit call.
    static std::optional<std::filesystem::path> resolve(
        const ModelAsset& asset,
        const std::filesystem::path& executableDirectory,
        std::string& error);

    static bool verifySha256(const std::filesystem::path& path,
                             const std::string& expected,
                             std::string& error);

    static std::optional<ModelAsset> readManifest(const std::filesystem::path& path,
                                                  const std::string& filename,
                                                  std::string& error);

    // Downloads HTTPS assets with resumable .part files and verifies both
    // declared size and SHA-256 before replacing the destination.
    static bool download(const ModelAsset& asset,
                         const std::filesystem::path& destination,
                         DownloadProgress progress,
                         std::string& error);
};

} // namespace pfservices
