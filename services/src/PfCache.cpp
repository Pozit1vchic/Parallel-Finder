#include "pfservices/PfCache.hpp"

#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pfservices {
namespace {

constexpr std::array<char, 8> kMagic {'P', 'F', 'C', 'A', 'C', 'H', 'E', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxCacheEntryBytes = 512ULL * 1024ULL * 1024ULL;

template <typename T>
void writeScalar(std::ostream& stream, T value)
{
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T>
bool readScalar(std::istream& stream, T& value)
{
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

std::string fileStem(const std::string& key)
{
    const QByteArray digest = QCryptographicHash::hash(QByteArray::fromStdString(key),
                                                       QCryptographicHash::Sha256).toHex();
    return digest.toStdString() + ".pfc";
}

std::shared_ptr<std::shared_mutex> rootAccess(const std::filesystem::path& root)
{
    static std::mutex registryMutex;
    static std::unordered_map<std::wstring, std::weak_ptr<std::shared_mutex>> registry;
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(root, error);
    if (error) canonical = std::filesystem::absolute(root).lexically_normal();
#ifdef _WIN32
    const auto key = QString::fromStdWString(canonical.wstring()).toCaseFolded().toStdWString();
#else
    const auto key = canonical.wstring();
#endif
    const std::lock_guard lock(registryMutex);
    // Retain no root/lock forever after its last cache owner goes away.
    std::erase_if(registry, [](const auto& item) { return item.second.expired(); });
    if (const auto found = registry.find(key); found != registry.end())
        if (auto access = found->second.lock()) return access;
    auto access = std::make_shared<std::shared_mutex>();
    registry.insert_or_assign(key, access);
    return access;
}

} // namespace

PfCache::PfCache(std::filesystem::path root, std::size_t limitBytes)
    : root_(root / "ParallelFinder-cache"), limitBytes_(limitBytes)
{
    if (root.empty() || limitBytes_ == 0) {
        throw std::invalid_argument("PfCache: root and limit must be valid");
    }
    access_ = rootAccess(root_);
    const std::unique_lock lock(*access_);
    // Older versions scattered entries directly in the selected folder.
    // Move only self-identifying PF files; never infer ownership from a hash
    // looking name alone, or touch videos and other user files.
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) return;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) || entry.path().extension() != ".pfc") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        std::array<char, 8> magic{};
        std::uint32_t version = 0, keySize = 0;
        std::uint64_t payloadSize = 0;
        if (!input.read(magic.data(), magic.size()) || magic != kMagic
            || !readScalar(input, version) || version != kVersion
            || !readScalar(input, keySize) || !keySize || keySize > 1024
            || !readScalar(input, payloadSize) || payloadSize > kMaxCacheEntryBytes) continue;
        std::string key(keySize, '\0');
        if (!input.read(key.data(), keySize) || entry.path().filename() != fileStem(key)) continue;
        input.close();
        const auto expectedSize = 8ULL + 4 + 4 + 8 + keySize + payloadSize;
        if (entry.file_size(error) != expectedSize || error) { error.clear(); continue; }
        const auto target = root_ / entry.path().filename();
        if (!std::filesystem::exists(target, error)) std::filesystem::rename(entry.path(), target, error);
        error.clear();
    }
}

std::filesystem::path PfCache::fileFor(const std::string& key) const
{
    return root_ / fileStem(key);
}

bool PfCache::readEntry(const std::filesystem::path& path, const std::string& key,
                        std::vector<std::uint8_t>& payload, std::size_t maxPayloadBytes) const
{
    // Inspect the length through the same open file, before trusting a header
    // that could otherwise allocate hundreds of MB for a truncated entry.
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    const auto fileSize = static_cast<std::streamoff>(stream.tellg());
    if (fileSize < 0 || !stream.seekg(0, std::ios::beg)) return false;
    std::array<char, kMagic.size()> magic {};
    if (!stream.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != kMagic)
        return false;
    std::uint32_t version = 0;
    std::uint32_t keySize = 0;
    std::uint64_t payloadSize = 0;
    if (!readScalar(stream, version) || !readScalar(stream, keySize) || !readScalar(stream, payloadSize)
        || version != kVersion || keySize != key.size()
        || payloadSize > limitBytes_ || payloadSize > kMaxCacheEntryBytes || payloadSize > maxPayloadBytes
        || payloadSize > std::numeric_limits<std::size_t>::max()
        || keySize > 1024U) return false;
    const auto expectedSize = kMagic.size() + sizeof(version) + sizeof(keySize)
        + sizeof(payloadSize) + static_cast<std::uint64_t>(keySize) + payloadSize;
    if (static_cast<std::uint64_t>(fileSize) != expectedSize) return false;
    std::string storedKey(keySize, '\0');
    if (!stream.read(storedKey.data(), static_cast<std::streamsize>(storedKey.size()))
        || storedKey != key) return false;
    payload.resize(static_cast<std::size_t>(payloadSize));
    return payload.empty() || static_cast<bool>(stream.read(
        reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size())));
}

bool PfCache::writeEntry(const std::filesystem::path& path, const std::string& key,
                         const std::vector<std::uint8_t>& payload, std::string& error) const
{
    if (key.empty() || key.size() > 1024U || payload.size() > limitBytes_
        || payload.size() > kMaxCacheEntryBytes) {
        error = "cache entry exceeds PFCACHE1 limits";
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        error = "create cache directory: " + filesystemError.message();
        return false;
    }
    // Reserve an owned staging directory on the same volume. Close the file
    // before publishing: QSaveFile retains an exclusive Windows handle after
    // rename until destruction, causing transient reader sharing failures.
    QTemporaryDir staging(QFileInfo(QString::fromStdWString(path.wstring())).absolutePath()
        + QStringLiteral("/.parallelfinder-cache-XXXXXX"));
    if (!staging.isValid()) {
        error = "create temporary cache directory: " + staging.errorString().toStdString();
        return false;
    }
    const auto temporary = std::filesystem::path(staging.filePath(QStringLiteral("entry.pfc")).toStdWString());
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "open cache entry for write failed";
        return false;
    }
    stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    writeScalar(stream, kVersion);
    writeScalar(stream, static_cast<std::uint32_t>(key.size()));
    writeScalar(stream, static_cast<std::uint64_t>(payload.size()));
    stream.write(key.data(), static_cast<std::streamsize>(key.size()));
    if (!payload.empty()) stream.write(reinterpret_cast<const char*>(payload.data()),
                                       static_cast<std::streamsize>(payload.size()));
    stream.flush();
    stream.close();
    if (!stream) {
        error = "write cache entry failed";
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        filesystemError = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(temporary, path, filesystemError);
#endif
    if (filesystemError) {
        error = "install cache entry: " + filesystemError.message();
        return false;
    }
    return true;
}

void PfCache::evictIfNeeded(const std::filesystem::path& protectedPath) const
{
    std::error_code filesystemError;
    std::filesystem::create_directories(root_, filesystemError);
    struct File { std::filesystem::path path; std::uintmax_t size; std::filesystem::file_time_type time; };
    std::vector<File> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_, filesystemError)) {
        if (filesystemError || !entry.is_regular_file(filesystemError)
            || entry.path().extension() != ".pfc") continue;
        const auto size = entry.file_size(filesystemError);
        if (filesystemError) continue;
        files.push_back({entry.path(), size, entry.last_write_time(filesystemError)});
        total += size;
    }
    std::sort(files.begin(), files.end(), [](const File& left, const File& right) {
        return left.time < right.time;
    });
    for (const auto& file : files) {
        if (total <= limitBytes_) break;
        if (file.path == protectedPath) continue;
        std::filesystem::remove(file.path, filesystemError);
        if (!filesystemError) total -= file.size;
        filesystemError.clear();
    }
}

std::optional<std::vector<std::uint8_t>> PfCache::get(const std::string& key, std::size_t maxPayloadBytes) const
{
    if (key.empty()) return std::nullopt;
    const std::shared_lock lock(*access_);
    const auto path = fileFor(key);
    std::vector<std::uint8_t> payload;
    if (!readEntry(path, key, payload, maxPayloadBytes)) return std::nullopt;
    std::error_code ignored;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
    return payload;
}

bool PfCache::put(const std::string& key, const std::vector<std::uint8_t>& payload,
                  std::string& error)
{
    const std::unique_lock lock(*access_);
    if (!writeEntry(fileFor(key), key, payload, error)) return false;
    evictIfNeeded(fileFor(key));
    return true;
}

bool PfCache::erase(const std::string& key, std::string& error)
{
    const std::unique_lock lock(*access_);
    std::error_code filesystemError;
    if (!std::filesystem::remove(fileFor(key), filesystemError) && filesystemError) {
        error = "remove cache entry: " + filesystemError.message();
        return false;
    }
    return true;
}

bool PfCache::clear(std::string& error)
{
    const std::unique_lock lock(*access_);
    std::error_code filesystemError;
    if (!std::filesystem::exists(root_, filesystemError)) return true;
    for (const auto& entry : std::filesystem::directory_iterator(root_, filesystemError)) {
        if (filesystemError) break;
        if (entry.is_regular_file(filesystemError) && entry.path().extension() == ".pfc")
            std::filesystem::remove(entry.path(), filesystemError);
    }
    if (filesystemError) {
        error = "clear cache: " + filesystemError.message();
        return false;
    }
    return true;
}

std::size_t PfCache::bytesUsed() const
{
    const std::shared_lock lock(*access_);
    std::error_code filesystemError;
    std::uintmax_t total = 0;
    if (!std::filesystem::exists(root_, filesystemError)) return 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_, filesystemError)) {
        if (filesystemError || !entry.is_regular_file(filesystemError)
            || entry.path().extension() != ".pfc") continue;
        total += entry.file_size(filesystemError);
    }
    return total > std::numeric_limits<std::size_t>::max()
        ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(total);
}

} // namespace pfservices
