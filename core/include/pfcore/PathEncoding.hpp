#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace pfcore {
// Public string paths are UTF-8. path::string() is locale-sensitive on Windows
// and can itself throw while reporting an unrelated load/probe error.
inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto encoded=path.u8string();
    return {encoded.begin(),encoded.end()};
}
inline std::filesystem::path pathFromUtf8(std::string_view path)
{
    return std::filesystem::path(std::u8string(path.begin(),path.end()));
}
}
