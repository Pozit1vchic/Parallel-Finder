#pragma once

#include <filesystem>
#include <system_error>

namespace pfservices::detail {

// Install a closed, fully verified partial download. A failed publication
// must preserve both the previous model and the resumable partial file.
void installDownloadedModel(const std::filesystem::path& partial,
                            const std::filesystem::path& destination,
                            std::error_code& error);

} // namespace pfservices::detail
