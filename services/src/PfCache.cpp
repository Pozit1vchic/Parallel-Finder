#include "pfservices/PfCache.hpp"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QUuid>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <set>
#include <chrono>
#include <string_view>
#include <mutex>

namespace pfservices {
namespace {

constexpr std::array<char, 8> kMagic {'P', 'F', 'C', 'A', 'C', 'H', 'E', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxCacheEntryBytes = 512ULL * 1024ULL * 1024ULL;
// Observation keys include source/model paths and the inference contract.
// Long Windows paths can exceed 1 KiB without indicating a malformed entry.
constexpr std::uint32_t kMaxCacheKeyBytes = 16U * 1024U;
constexpr std::size_t kEntryHeaderBytes=kMagic.size()+2*sizeof(std::uint32_t)+sizeof(std::uint64_t);

auto& cacheLocks()
{
    // Bounded process-lifetime coordination. The static controller can drain
    // cache workers from its destructor; these locks must outlive that drain.
    // No per-source registry or background worker is retained here.
    static auto* const locks=new std::array<std::recursive_mutex,64>;
    return *locks;
}

std::size_t cacheCoordinationSlot(const std::filesystem::path& root)
{
    std::error_code error;
    const auto canonical=std::filesystem::weakly_canonical(root,error);
    const auto& path=error ? root:canonical;
#ifdef _WIN32
    auto normalized=QDir::cleanPath(QFileInfo(QString::fromStdWString(path.wstring())).absoluteFilePath()).toCaseFolded();
#else
    auto normalized=QDir::cleanPath(QFileInfo(QString::fromStdString(path.string())).absoluteFilePath());
#endif
    return qHash(normalized)%cacheLocks().size();
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

std::optional<std::string> storedEntryKey(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    std::array<char,8> magic{};std::uint32_t version=0,keySize=0;std::uint64_t payloadSize=0;
    if(!input.read(magic.data(),magic.size()) || magic!=kMagic || !readScalar(input,version)
        || version!=kVersion || !readScalar(input,keySize) || !readScalar(input,payloadSize)
        || !keySize || keySize>kMaxCacheKeyBytes)return std::nullopt;
    std::string key(keySize,'\0');
    if(!input.read(key.data(),keySize))return std::nullopt;
    return key;
}

void removeEntryOwner(const std::filesystem::path& root,const std::string& key)
{
    auto owner=root/fileStem("source-owner-v1|"+key);owner.replace_extension(".owner");
    std::error_code ignored;
    std::filesystem::remove(owner,ignored);
}

std::string normalizeSource(std::string value)
{
#ifdef _WIN32
    std::replace(value.begin(), value.end(), '\\', '/');
    value = QString::fromUtf8(value).toCaseFolded().toStdString();
#endif
    return value;
}

struct Retention {
    unsigned cost=0;bool preview=false;
};
Retention retentionClass(const std::filesystem::path& path,std::uintmax_t fileSize)
{
    std::ifstream input(path,std::ios::binary);
    std::array<char,8> magic{};std::uint32_t version=0,keySize=0;std::uint64_t payloadSize=0;
    if(!input.read(magic.data(),magic.size()) || magic!=kMagic || !readScalar(input,version)
        || version!=kVersion || !readScalar(input,keySize) || !readScalar(input,payloadSize)
        || !keySize || keySize>kMaxCacheKeyBytes || payloadSize>kMaxCacheEntryBytes
        || fileSize!=kEntryHeaderBytes+keySize+payloadSize)return {};
    std::array<char,32> prefix{};
    const auto length=std::min<std::size_t>(prefix.size(),keySize);
    if(!input.read(prefix.data(),static_cast<std::streamsize>(length)))return {};
    const std::string_view key(prefix.data(),length);
    const bool observations=key.starts_with("motion-v") || key.starts_with("scene-sequence-v")
        || key.starts_with("matched-face-v");
    const bool refinement=key.starts_with("head-orientation-v") || key.starts_with("video-actions-v")
        || key.starts_with("body-orientation-v");
    return {observations?2U:(key.starts_with("matcher-results-v") || refinement)?1U:0U,key.starts_with("preview-v")};
}

} // namespace

CacheBudget::CacheBudget(std::vector<std::filesystem::path> roots,std::size_t limitBytes)
    : limitBytes_(limitBytes)
{
    if(roots.empty() || !limitBytes_)throw std::invalid_argument("CacheBudget: roots and capacity required");
    for(const auto& root:roots) {
        std::error_code error;
        const auto canonical=std::filesystem::weakly_canonical(root,error);
        if(root.empty() || error)throw std::invalid_argument("CacheBudget: invalid managed root");
        if(std::find(roots_.begin(),roots_.end(),canonical)==roots_.end())roots_.push_back(canonical);
    }
}

namespace {
struct BudgetFile {
    std::filesystem::path path;
    std::uintmax_t size=0;
};
bool scanBudget(const std::vector<std::filesystem::path>& roots,std::vector<BudgetFile>& files,
                std::uintmax_t& total,std::string& error)
{
    for(const auto& root:roots) {
        std::error_code ec;
        if(!std::filesystem::exists(root,ec)) {
            if(ec){error="inspect shared cache root: "+ec.message();return false;}
            continue;
        }
        if(std::filesystem::is_symlink(root,ec) || ec) {
            error="shared cache root must not be a symbolic link";return false;
        }
        for(auto it=std::filesystem::directory_iterator(root,ec);
            !ec && it!=std::filesystem::directory_iterator{};it.increment(ec)) {
            const auto& entry=*it;
            if(entry.is_symlink(ec))continue;
            if(ec)break;
            if(!entry.is_regular_file(ec))continue;
            if(ec)break;
            const auto size=entry.file_size(ec);if(ec)break;
            if(size>std::numeric_limits<std::uintmax_t>::max()-total) {
                error="shared cache size overflow";return false;
            }
            total+=size;files.push_back({entry.path(),size});
        }
        if(ec){error="scan shared cache capacity: "+ec.message();return false;}
    }
    return true;
}
}

std::size_t CacheBudget::bytesUsed() const
{
    std::lock_guard guard(mutex_);
    std::vector<BudgetFile> files;std::uintmax_t total=0;std::string error;
    if(!scanBudget(roots_,files,total,error))throw std::runtime_error(error);
    return static_cast<std::size_t>(std::min<std::uintmax_t>(total,std::numeric_limits<std::size_t>::max()));
}

bool CacheBudget::enforceLimit(std::string& error) const
{
    std::lock_guard guard(mutex_);error.clear();return enforce({},error);
}

bool CacheBudget::enforce(const std::filesystem::path& protectedPath,std::string& error) const
{
    std::lock_guard guard(mutex_);
    std::vector<BudgetFile> files;std::uintmax_t total=0;
    if(!scanBudget(roots_,files,total,error))return false;
    if(total<=limitBytes_)return true;
    const auto remove=[&](const std::filesystem::path& path) {
        const auto found=std::find_if(files.begin(),files.end(),[&](const auto& file){return file.path==path;});
        if(found==files.end() || !found->size)return;
        std::error_code ec;
        if(std::filesystem::remove(path,ec) && !ec){total-=found->size;found->size=0;}
    };
    // Only recognized, correctly named cache files may be deleted. Unknown
    // files still consume capacity; generation tombstones always survive.
    for(const auto& file:files) {
        if(file.path.extension()!=".owner")continue;
        const auto key=storedEntryKey(file.path);
        if(!key || !key->starts_with("source-owner-v1|"))continue;
        auto expected=file.path.parent_path()/fileStem(*key);expected.replace_extension(".owner");
        if(expected!=file.path)continue;
        std::error_code ec;
        const auto data=file.path.parent_path()/fileStem(key->substr(16));
        if(!std::filesystem::exists(data,ec) && !ec)remove(file.path);
    }
    struct Candidate {std::filesystem::path path;std::string key;unsigned priority;bool preview;
        std::filesystem::file_time_type time;std::uintmax_t size;};
    std::vector<Candidate> candidates;
    const auto recent=std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*7);
    for(const auto& file:files) {
        if(!file.size || file.path.extension()!=".pfc")continue;
        const auto key=storedEntryKey(file.path);
        if(!key || file.path.filename()!=fileStem(*key))continue;
        std::error_code ec;const auto time=std::filesystem::last_write_time(file.path,ec);
        if(ec)continue;
        const auto retention=retentionClass(file.path,file.size);
        candidates.push_back({file.path,*key,time>=recent?retention.cost:0U,retention.preview,time,file.size});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        if(a.priority!=b.priority)return a.priority<b.priority;
        if(a.time!=b.time)return a.time<b.time;
        if(a.size!=b.size)return a.size>b.size;
        return a.path.native()<b.path.native();
    });
    std::error_code ec;
    const auto protectedCanonical=protectedPath.empty()?std::filesystem::path{}:std::filesystem::weakly_canonical(protectedPath,ec);
    const auto evict=[&](const Candidate& file) {
        const auto before=total;remove(file.path);
        if(total!=before) {
            auto owner=file.path.parent_path()/fileStem("source-owner-v1|"+file.key);
            owner.replace_extension(".owner");remove(owner);
        }
    };
    for(const auto& file:candidates) {
        if(total<=limitBytes_)break;
        if(file.path==protectedCanonical && !file.preview)continue;
        evict(file);
    }
    if(total>limitBytes_ && !protectedCanonical.empty()) {
        for(const auto& file:candidates)if(file.path==protectedCanonical){evict(file);break;}
    }
    if(total>limitBytes_){error="shared cache capacity cannot be enforced; retained metadata or inaccessible files exceed limit";return false;}
    if(!protectedCanonical.empty() && !std::filesystem::exists(protectedCanonical,ec)) {
        error="cache entry was not retained under the shared capacity policy";return false;
    }
    return true;
}

PfCache::PfCache(std::filesystem::path root, std::size_t limitBytes, std::shared_ptr<CacheBudget> budget)
    : root_(std::move(root)), limitBytes_(budget ? budget->limitBytes_:limitBytes), budget_(std::move(budget))
{
    if (root_.empty() || limitBytes_ == 0) {
        throw std::invalid_argument("PfCache: root and limit must be valid");
    }
    coordinationSlot_=cacheCoordinationSlot(root_);
    if(budget_) {
        std::error_code error;
        const auto canonical=std::filesystem::weakly_canonical(root_,error);
        if(error || std::find(budget_->roots_.begin(),budget_->roots_.end(),canonical)==budget_->roots_.end())
            throw std::invalid_argument("PfCache: root is outside shared budget");
    }
}

std::recursive_mutex& PfCache::coordinationMutex() const
{
    return budget_ ? budget_->mutex_:cacheLocks()[coordinationSlot_];
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
        || keySize > kMaxCacheKeyBytes) return false;
    std::string storedKey(keySize, '\0');
    if (!stream.read(storedKey.data(), static_cast<std::streamsize>(storedKey.size()))
        || storedKey != key) return false;
    // Validate actual bytes before allocating from an untrusted cache header.
    // A tiny truncated file claiming hundreds of MiB must remain a cache miss.
    const auto begin=stream.tellg();
    if(begin<0)return false;
    stream.seekg(0,std::ios::end);
    const auto end=stream.tellg();
    if(end<begin || static_cast<std::uint64_t>(end-begin)!=payloadSize)return false;
    stream.seekg(begin);
    if(!stream)return false;
    payload.resize(static_cast<std::size_t>(payloadSize));
    return payload.empty() || static_cast<bool>(stream.read(
        reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size())));
}

bool PfCache::writeEntry(const std::filesystem::path& path, const std::string& key,
                         const std::vector<std::uint8_t>& payload, std::string& error) const
{
    const bool fitsCapacity=kEntryHeaderBytes<=limitBytes_ && key.size()<=limitBytes_-kEntryHeaderBytes
        && payload.size()<=limitBytes_-kEntryHeaderBytes-key.size();
    if (key.empty() || key.size() > kMaxCacheKeyBytes || !fitsCapacity
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
    QSaveFile output(QString::fromStdWString(path.wstring()));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        error = "open cache entry for write: " + output.errorString().toStdString();
        return false;
    }
    const auto write = [&](const auto& value) {
        return output.write(reinterpret_cast<const char*>(&value),sizeof(value)) == sizeof(value);
    };
    const bool written = output.write(kMagic.data(),kMagic.size()) == static_cast<qint64>(kMagic.size())
        && write(kVersion) && write(static_cast<std::uint32_t>(key.size()))
        && write(static_cast<std::uint64_t>(payload.size()))
        && output.write(key.data(),key.size()) == static_cast<qint64>(key.size())
        && output.write(reinterpret_cast<const char*>(payload.data()),payload.size()) == static_cast<qint64>(payload.size());
    if (!written || !output.commit()) {
        error = "write cache entry: " + output.errorString().toStdString();
        output.cancelWriting();
        return false;
    }
    return true;
}

bool PfCache::evictIfNeeded(const std::filesystem::path& protectedPath,std::string& error) const
{
    std::error_code filesystemError;
    std::filesystem::create_directories(root_, filesystemError);
    if(filesystemError){error="create cache directory: "+filesystemError.message();return false;}
    struct File {
        std::filesystem::path path;std::uintmax_t size;std::filesystem::file_time_type time;
        unsigned priority=0;bool preview=false;
    };
    std::vector<File> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root_, filesystemError)) {
        if (filesystemError || entry.is_symlink(filesystemError) || !entry.is_regular_file(filesystemError)
            || entry.path().extension() != ".pfc") continue;
        const auto size = entry.file_size(filesystemError);
        if (filesystemError) continue;
        files.push_back({entry.path(), size, {}});
        total += size;
    }
    if(filesystemError){error="inspect cache capacity: "+filesystemError.message();return false;}
    if (total <= limitBytes_) return !budget_ || budget_->enforce(protectedPath,error);
    // Reclaim legacy ownership records only when their data file is absent.
    // Generation records deliberately survive eviction: deleting one could
    // reactivate observations from before an explicit source reset.
    for(const auto& entry:std::filesystem::directory_iterator(root_,filesystemError)) {
        if(filesystemError)break;
        if(entry.is_symlink(filesystemError) || !entry.is_regular_file(filesystemError)
            || entry.path().extension()!=".owner")continue;
        const auto ownerKey=storedEntryKey(entry.path());
        if(!ownerKey || !ownerKey->starts_with("source-owner-v1|"))continue;
        const auto data=root_/fileStem(ownerKey->substr(16));
        const bool exists=std::filesystem::exists(data,filesystemError);
        if(!filesystemError && !exists)std::filesystem::remove(entry.path(),filesystemError);
        filesystemError.clear();
    }
    filesystemError.clear();
    const auto protectSince=std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*7);
    for (auto& file : files) {
        file.time=std::filesystem::last_write_time(file.path, filesystemError);
        if(filesystemError)file.time=std::filesystem::file_time_type::min();
        filesystemError.clear();
        const auto retention=retentionClass(file.path,file.size);
        file.priority=file.time>=protectSince ? retention.cost:0U;
        file.preview=retention.preview;
    }
    std::sort(files.begin(), files.end(), [](const File& left, const File& right) {
        if(left.priority!=right.priority)return left.priority<right.priority;
        if(left.time!=right.time)return left.time<right.time;
        if(left.size!=right.size)return left.size>right.size;
        return left.path.native()<right.path.native();
    });
    for (const auto& file : files) {
        if (total <= limitBytes_) break;
        // A new preview is expendable too: keeping it must not evict the
        // expensive observations it will be rebuilt from on the next run.
        if (file.path == protectedPath && !file.preview) continue;
        const auto key=storedEntryKey(file.path);
        const bool removed=std::filesystem::remove(file.path, filesystemError);
        if (removed && !filesystemError) {
            total -= file.size;
            if(key)removeEntryOwner(root_,*key);
        }
        filesystemError.clear();
    }
    if(total>limitBytes_ && !protectedPath.empty()) {
        // Locked older entries cannot justify exceeding the configured limit.
        // Decline the new record; all writes remain optional for analysis.
        const auto found=std::find_if(files.begin(),files.end(),[&](const auto& f){return f.path==protectedPath;});
        const auto key=storedEntryKey(protectedPath);
        if(found!=files.end() && std::filesystem::remove(protectedPath,filesystemError) && !filesystemError) {
            total-=found->size;
            if(key)removeEntryOwner(root_,*key);
        }
        filesystemError.clear();
    }
    if(total>limitBytes_){error="cache capacity cannot be enforced while entries are locked or inaccessible";return false;}
    if(!protectedPath.empty()) {
        const bool retained=std::filesystem::exists(protectedPath,filesystemError);
        if(filesystemError){error="inspect retained cache entry: "+filesystemError.message();return false;}
        if(!retained){error="cache entry was not retained under the capacity policy";return false;}
    }
    return !budget_ || budget_->enforce(protectedPath,error);
}

bool PfCache::enforceLimit(std::string& error) const
{
    std::lock_guard guard(coordinationMutex());
    error.clear();return evictIfNeeded({},error);
}

std::optional<std::vector<std::uint8_t>> PfCache::get(const std::string& key) const
{
    std::lock_guard guard(coordinationMutex());
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
    std::lock_guard guard(coordinationMutex());
    error.clear();
    if (!writeEntry(fileFor(key), key, payload, error)) return false;
    return evictIfNeeded(fileFor(key),error);
}

bool PfCache::putBatch(const std::vector<std::pair<std::string,std::vector<std::uint8_t>>>& entries,
                       std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    error.clear();
    std::filesystem::path lastWritten;
    bool success = true;
    for (const auto& [key,payload] : entries) {
        const auto path = fileFor(key);
        std::string entryError;
        if (!writeEntry(path,key,payload,entryError)) {
            if (success) error = std::move(entryError);
            success = false;
            continue;
        }
        lastWritten = path;
    }
    std::string capacityError;
    if (!lastWritten.empty() && !evictIfNeeded(lastWritten,capacityError)) {
        if(success)error=std::move(capacityError);
        success=false;
    }
    return success;
}

bool PfCache::putForSourceBatch(const std::string& source,
    const std::vector<std::pair<std::string,std::vector<std::uint8_t>>>& entries,std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    error.clear();
    if(source.empty()) { error="cache source must not be empty";return false; }
    std::filesystem::path lastWritten;
    for(const auto& [key,payload]:entries) {
        const auto path=fileFor(key);
        if(!rememberSourceKey(source,key,error) || !writeEntry(path,key,payload,error)) {
            std::error_code inspectError;
            if(!std::filesystem::exists(path,inspectError) && !inspectError)removeEntryOwner(root_,key);
            if(!lastWritten.empty()){std::string ignored;evictIfNeeded(lastWritten,ignored);}
            return false;
        }
        lastWritten=path;
    }
    return lastWritten.empty() || evictIfNeeded(lastWritten,error);
}

bool PfCache::putForSource(const std::string& source,const std::string& key,
    const std::vector<std::uint8_t>& payload,std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    if(!rememberSourceKey(source,key,error))return false;
    const bool stored=put(key,payload,error);
    if(!stored) {
        std::error_code inspectError;
        if(!std::filesystem::exists(fileFor(key),inspectError) && !inspectError)removeEntryOwner(root_,key);
    }
    return stored;
}

bool PfCache::erase(const std::string& key, std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    std::error_code filesystemError;
    if (!std::filesystem::remove(fileFor(key), filesystemError) && filesystemError) {
        error = "remove cache entry: " + filesystemError.message();
        return false;
    }
    removeEntryOwner(root_,key);
    return true;
}

std::string PfCache::sourceGeneration(const std::string& source) const
{
    std::lock_guard guard(coordinationMutex());
    const auto readGeneration = [&](const std::string& name) {
        const auto key = "source-generation-v1|" + name;
        auto path = fileFor(key);
        path.replace_extension(".generation");
        std::vector<std::uint8_t> payload;
        if (!readEntry(path, key, payload)) return std::string{};
        return std::string(payload.begin(), payload.end());
    };
    const auto normalized = normalizeSource(source);
    const auto current = readGeneration(normalized);
    // Preserve existing exact-spelling generations until the next reset.
    return !current.empty() || normalized == source ? current : readGeneration(source);
}

bool PfCache::resetSource(const std::string& source, std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    const auto key = "source-generation-v1|" + normalizeSource(source);
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
    return !budget_ || budget_->enforce({},error);
}

bool PfCache::rememberSourceKey(const std::string& source,const std::string& key,std::string& error)
{
    std::lock_guard guard(coordinationMutex());
    auto owner=fileFor("source-owner-v1|"+key);owner.replace_extension(".owner");
    return writeEntry(owner,"source-owner-v1|"+key,std::vector<std::uint8_t>(source.begin(),source.end()),error);
}

std::vector<std::string> PfCache::keysForSource(const std::string& source,std::string* outputError) const
{
    std::lock_guard guard(coordinationMutex());
    std::set<std::string> keys;std::error_code error;
    if(!std::filesystem::exists(root_,error)) {
        if(error && outputError)*outputError="inspect cache: "+error.message();
        return {};
    }
    const auto normalized=normalizeSource(source);
    for(const auto& entry:std::filesystem::directory_iterator(root_,error)) {
        if(error)break;
        if(entry.is_symlink(error) || !entry.is_regular_file(error))continue;
        const auto extension=entry.path().extension();
        if(extension!=".pfc" && extension!=".owner")continue;
        std::ifstream stream(entry.path(),std::ios::binary);
        std::array<char,8> magic{};std::uint32_t version=0,size=0;std::uint64_t payloadSize=0;
        if(!stream.read(magic.data(),magic.size()) || magic!=kMagic || !readScalar(stream,version)
            || !readScalar(stream,size) || !readScalar(stream,payloadSize) || version!=kVersion || !size || size>kMaxCacheKeyBytes)continue;
        std::string stored(size,'\0');if(!stream.read(stored.data(),size))continue;
        if(extension==".owner") {
            // Use the same key bound as the writer: valid long observation
            // keys must remain discoverable when the source is reset.
            if(!stored.starts_with("source-owner-v1|") || payloadSize>kMaxCacheKeyBytes)continue;
            std::string owner(payloadSize,'\0');if(stream.read(owner.data(),owner.size()) && normalizeSource(owner)==normalized)
                keys.insert(stored.substr(16));
        }else {
            const auto comparable=normalizeSource(stored);
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
    std::lock_guard guard(coordinationMutex());
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
    std::lock_guard guard(coordinationMutex());
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
