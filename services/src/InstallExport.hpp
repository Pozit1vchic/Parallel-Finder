#pragma once
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace pfservices::detail {
// Both paths are on the destination filesystem. Publish a completed export
// without copying it or removing the existing destination first.
inline void installExport(const std::filesystem::path& temporary,
                          const std::filesystem::path& destination, std::error_code& error)
{
#ifdef _WIN32
    if (std::filesystem::exists(destination, error)) {
        if (!ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return;
    }
    if (error) return;
#endif
    std::filesystem::rename(temporary, destination, error);
}
}
