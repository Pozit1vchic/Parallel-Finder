#include <pfservices/ModelStore.hpp>

#include <QCryptographicHash>
#include <QFile>

#include <cstdlib>
#include <sstream>
#include <vector>

#include <pfservices/SettingsStore.hpp>

namespace pfservices {
namespace {

std::vector<std::filesystem::path> candidateRoots(const std::filesystem::path& executableDirectory)
{
    std::vector<std::filesystem::path> roots;
    roots.push_back(executableDirectory / "models");
    roots.push_back(std::filesystem::path(SettingsStore::defaultDirectory()) / "models");
    if (const char* root = std::getenv("PF_MODEL_ROOT"); root && *root)
        roots.emplace_back(root);
    return roots;
}

} // namespace

bool ModelStore::verifySha256(const std::filesystem::path& path,
                              const std::string& expected,
                              std::string& error)
{
    if (expected.empty()) return true;
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        error = "open model for hash: " + file.errorString().toStdString();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd()) {
            error = "read model while hashing: " + file.errorString().toStdString();
            return false;
        }
        hash.addData(chunk);
    }
    const std::string actual = hash.result().toHex().toStdString();
    if (actual != expected) {
        error = "model SHA-256 mismatch: expected " + expected + ", got " + actual;
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> ModelStore::resolve(
    const ModelAsset& asset,
    const std::filesystem::path& executableDirectory,
    std::string& error)
{
    std::ostringstream checked;
    for (const auto& root : candidateRoots(executableDirectory)) {
        const auto candidate = root / asset.filename;
        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(candidate, filesystemError)) {
            checked << candidate.string() << "; ";
            continue;
        }
        std::string hashError;
        if (!verifySha256(candidate, asset.sha256, hashError)) {
            error = hashError;
            return std::nullopt;
        }
        return candidate;
    }
    error = "model '" + asset.filename + "' was not found; checked: " + checked.str();
    return std::nullopt;
}

} // namespace pfservices
