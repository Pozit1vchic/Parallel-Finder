#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pfservices {

struct ModelAsset {
    std::string filename = "yolo26m-pose-640-b1.onnx";
    std::string sha256;
};

class ModelStore {
public:
    // Searches only local, explicitly configured locations. Network download
    // is intentionally a separate operation so offline startup stays safe.
    static std::optional<std::filesystem::path> resolve(
        const ModelAsset& asset,
        const std::filesystem::path& executableDirectory,
        std::string& error);

    static bool verifySha256(const std::filesystem::path& path,
                             const std::string& expected,
                             std::string& error);
};

} // namespace pfservices
