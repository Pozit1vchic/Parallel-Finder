#include "pfservices/PfCache.hpp"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QUuid>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <set>

namespace pfservices {
namespace {

constexpr std::array<char, 8> kMagic {'P', 'F', 'C', 'A', 'C', 'H', 'E', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxCacheEntryBytes = 512ULL * 1024ULL * 1024ULL;
std::atomic_uint64_t kTemporaryCounter {0};

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

} // namespace

PfCache::PfCache(std::filesystem::path root, std::size_t limitBytes)
    : root_(std::move(root)), limitBytes_(limitBytes)
{
    if (root_.empty() || limitBytes_ == 0) {
        throw std::invalid_argument("PfCache: root and limit must be valid");
    }
}

std::filesystem::path PfCache::fileFor(const std::string& key) const
{
    return root_ / fileStem(key);
}

bool PfCache::readEntry(const std::filesystem::path& path, const std::string& key,
                        std::vector<std::uint8_t>& payload) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::array<char, kMagic.size()> magic {};
    if (!stream.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != kMagic)
        return false;
    std::uint32_t version = 0;
    std::uint32_t keySize = 0;
    std::uint64_t payloadSize = 0;
    if (!readScalar(stream, version) || !readScalar(stream, keySize) || !readScalar(stream, payloadSize)
        || version != kVersion || keySize != key.size()
        || payloadSize > limitBytes_ || payloadSize > kMaxCacheEntryBytes
        || payloadSize > std::numeric_limits<std::size_t>::max()
        || keySize > 1024U) return false;
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
    const auto temporary = path.string() + ".tmp."
        + std::to_string(QCoreApplication::applicationPid()) + "."
        + std::to_string(kTemporaryCounter.fetch_add(1, std::memory_order_relaxed));
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
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    std::filesystem::remove(path, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        error = "install cache entry: " + filesystemError.message();
        std::filesystem::remove(temporary, filesystemError);
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

std::optional<std::vector<std::uint8_t>> PfCache::get(const std::string& key) const
{
    if (key.empty()) return std::nullopt;
    const auto path = fileFor(key);
    std::vector<std::uint8_t> payload;
    if (!readEntry(path, key, payload)) return std::nullopt;
    std::error_code ignored;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
    return payload;
}

bool PfCache::put(const std::string& key, const std::vector<std::uint8_t>& payload,
                  std::string& error)
{
    if (!writeEntry(fileFor(key), key, payload, error)) return false;
    evictIfNeeded(fileFor(key));
    return true;
}

bool PfCache::erase(const std::string& key, std::string& error)
{
    std::error_code filesystemError;
    if (!std::filesystem::remove(fileFor(key), filesystemError) && filesystemError) {
        error = "remove cache entry: " + filesystemError.message();
        return false;
    }
    return true;
}

std::string PfCache::sourceGeneration(const std::string& source) const
{
    const auto key = "source-generation-v1|" + source;
    auto path = fileFor(key);
    path.replace_extension(".generation");
    std::vector<std::uint8_t> payload;
    if (!readEntry(path, key, payload)) return {};
    return std::string(payload.begin(), payload.end());
}

bool PfCache::resetSource(const std::string& source, std::string& error)
{
    const auto key = "source-generation-v1|" + source;
    auto path = fileFor(key);
    path.replace_extension(".generation");
    const auto generation = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    // Rotate before deletion: even a failed or interrupted purge cannot
    // reactivate old observations. Delete only entries owned by this source.
    if (!writeEntry(path, key, std::vector<std::uint8_t>(generation.begin(), generation.end()), error)) return false;
    std::string scanError;
    const auto ownedKeys=keysForSource(source,&scanError);
    if(!scanError.empty()) {error=scanError;return false;}
    for(const auto& entryKey:ownedKeys) {
        if(!erase(entryKey,error))return false;
        for(int version=1;version<=5;++version) {
            const auto derived="scene-sequence-v"+std::to_string(version)+"|"
                +QCryptographicHash::hash(QByteArray::fromStdString(entryKey),QCryptographicHash::Sha256).toHex().toStdString();
            if(!erase(derived,error))return false;
        }
        auto owner=fileFor("source-owner-v1|"+entryKey);owner.replace_extension(".owner");
        std::error_code removalError;std::filesystem::remove(owner,removalError);
        if(removalError) {error="remove cache ownership: "+removalError.message();return false;}
    }
    return true;
}

bool PfCache::rememberSourceKey(const std::string& source,const std::string& key,std::string& error)
{
    auto owner=fileFor("source-owner-v1|"+key);owner.replace_extension(".owner");
    return writeEntry(owner,"source-owner-v1|"+key,std::vector<std::uint8_t>(source.begin(),source.end()),error);
}

std::vector<std::string> PfCache::keysForSource(const std::string& source,std::string* outputError) const
{
    std::set<std::string> keys;std::error_code error;
    if(!std::filesystem::exists(root_,error)) {
        if(error && outputError)*outputError="inspect cache: "+error.message();
        return {};
    }
    const auto normalize=[](std::string value) {
        std::replace(value.begin(),value.end(),'\\','/');
#ifdef _WIN32
        value=QString::fromUtf8(value).toCaseFolded().toStdString();
#endif
        return value;
    };
    const auto normalized=normalize(source);
    for(const auto& entry:std::filesystem::directory_iterator(root_,error)) {
        if(error)break;
        if(entry.is_symlink(error) || !entry.is_regular_file(error))continue;
        const auto extension=entry.path().extension();
        if(extension!=".pfc" && extension!=".owner")continue;
        std::ifstream stream(entry.path(),std::ios::binary);
        std::array<char,8> magic{};std::uint32_t version=0,size=0;std::uint64_t payloadSize=0;
        if(!stream.read(magic.data(),magic.size()) || magic!=kMagic || !readScalar(stream,version)
            || !readScalar(stream,size) || !readScalar(stream,payloadSize) || version!=kVersion || !size || size>2048)continue;
        std::string stored(size,'\0');if(!stream.read(stored.data(),size))continue;
        if(extension==".owner") {
            if(!stored.starts_with("source-owner-v1|") || payloadSize>4096)continue;
            std::string owner(payloadSize,'\0');if(stream.read(owner.data(),owner.size()) && normalize(owner)==normalized)
                keys.insert(stored.substr(16));
        }else {
            const auto comparable=normalize(stored);
            // Legacy primary observations and preview keys retain the path.
            if(comparable.find("|source="+normalized+"|")!=std::string::npos
                || (comparable.starts_with("preview-v1|") && comparable.find("|"+normalized+"|")!=std::string::npos))
                keys.insert(stored);
        }
    }
    if(error && outputError)*outputError="scan cache ownership: "+error.message();
    return {keys.begin(),keys.end()};
}

bool PfCache::clear(std::string& error)
{
    std::error_code filesystemError;
    if (!std::filesystem::exists(root_, filesystemError)) return true;
    for (const auto& entry : std::filesystem::directory_iterator(root_, filesystemError)) {
        if (filesystemError) break;
        if (entry.is_regular_file(filesystemError) && (entry.path().extension() == ".pfc"
            || entry.path().extension()==".owner"))
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
