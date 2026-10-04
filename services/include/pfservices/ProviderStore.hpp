#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pfservices {

struct ProviderPart {
    std::string archive;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::string downloadUrl;
};

struct ProviderAsset {
    std::string provider;
    std::string archive;
    std::string sha256;
    std::uint64_t sizeBytes = 0;
    std::string downloadUrl;
    std::vector<ProviderPart> parts;
};

using ProviderDownloadProgress = std::function<void(std::uint64_t received,
                                                    std::uint64_t total)>;

class ProviderStore {
public:
    static std::optional<ProviderAsset> readManifest(const std::filesystem::path& path,
                                                    const std::string& provider, std::string& error);
    // Parts are ordinary byte chunks, not a ZIP multi-volume format. Verify
    // every chunk and the complete archive before exposing it to extraction.
    static bool assembleParts(const ProviderAsset& asset,
                              const std::vector<std::filesystem::path>& paths,
                              const std::filesystem::path& destination, std::string& error);
    static bool downloadDirectMl(const std::filesystem::path& destination,
                                 ProviderDownloadProgress progress, std::string& error);
    // Manifest format: { "providers": [{ "provider":"cuda",
    // "archive":"runtime-cuda.zip", "sha256":"...", "sizeBytes":123,
    // "downloadUrl":"https://..." }] }
    static std::optional<ProviderAsset> fetchManifest(const std::string& url,
                                                      const std::string& provider,
                                                      std::string& error);

    // Downloads and verifies the archive, then extracts it into destination.
    // The archive must contain a complete side-by-side ONNX Runtime bundle
    // (onnxruntime.dll plus the provider DLLs), not arbitrary executables.
    static bool downloadAndInstall(const ProviderAsset& asset,
                                   const std::filesystem::path& destination,
                                   ProviderDownloadProgress progress,
                                   std::string& error);
};

} // namespace pfservices
