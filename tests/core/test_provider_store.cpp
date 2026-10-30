#include <gtest/gtest.h>
#include <pfservices/ProviderStore.hpp>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <pfservices/ModelStore.hpp>
#include <pfcore/PathEncoding.hpp>
#include <QDir>

namespace {
std::string digest(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
}

void put(const QString& path, const QByteArray& data) {
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); ASSERT_EQ(file.write(data), data.size());
}
QByteArray get(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}

TEST(ProviderStore, CachedVerifiedInstallerSurvivesFailedProbeAndSupportsOfflineRetry)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString())/pfcore::pathFromUtf8("Пакеты 日本語 spaces");
    const auto destination=root/"cuda";
    std::filesystem::create_directories(destination);
    put(QString::fromStdWString((destination/"onnxruntime.dll").wstring()),"previous-runtime");
    const auto input=directory.filePath("input");
    ASSERT_TRUE(QDir().mkpath(input));put(input+"/onnxruntime.dll","new-runtime");
    const auto archive=directory.filePath("runtime.zip");
    QProcess tar;
    tar.start("tar",{"-a","-cf",archive,"-C",input,"onnxruntime.dll"});
    ASSERT_TRUE(tar.waitForFinished(30000));ASSERT_EQ(tar.exitCode(),0) << tar.readAllStandardError().constData();
    const auto payload=get(archive);
    pfservices::ProviderAsset asset{"cuda","runtime.zip",digest(payload),static_cast<std::uint64_t>(payload.size()),
        "https://github.com/parallel-finder/test-unreachable/releases/download/test/runtime.zip",{}};
    const auto cache=pfservices::ProviderStore::installerCacheDirectory(asset,destination);
    std::filesystem::create_directories(cache);
    put(QString::fromStdWString((cache/asset.archive).wstring()),payload);
    std::string error;int probes=0;
    EXPECT_FALSE(pfservices::ProviderStore::downloadAndInstall(asset,destination,{},error,
        [&](const auto& staging,std::string& reason) {
            ++probes;EXPECT_TRUE(std::filesystem::is_regular_file(staging/"onnxruntime.dll"));
            reason="TEST_PROBE_REJECTED";return false;
        }));
    EXPECT_EQ(probes,1);EXPECT_EQ(error,"TEST_PROBE_REJECTED");
    EXPECT_EQ(get(QString::fromStdWString((destination/"onnxruntime.dll").wstring())),"previous-runtime");
    EXPECT_EQ(get(QString::fromStdWString((cache/asset.archive).wstring())),payload);
    error.clear();
    ASSERT_TRUE(pfservices::ProviderStore::downloadAndInstall(asset,destination,{},error,
        [&](const auto&,std::string&) {++probes;return true;})) << error;
    EXPECT_EQ(probes,2);
    EXPECT_EQ(get(QString::fromStdWString((destination/"onnxruntime.dll").wstring())),"new-runtime");
    EXPECT_EQ(get(QString::fromStdWString((cache/asset.archive).wstring())),payload);
    EXPECT_TRUE(std::filesystem::is_regular_file(cache/"verified.json"));
}

TEST(ProviderStore, PartsAreVerifiedBeforeReplacingExistingArchive)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto a = directory.filePath("a"), b = directory.filePath("b"), output = directory.filePath("complete.zip");
    put(a, "abcd"); put(b, "efgh"); put(output, "previous");
    pfservices::ProviderAsset asset{"cuda", "complete.zip", digest("abcdefgh"), 8, "",
        {{"a", digest("abcd"), 4, "https://github.com/a"}, {"b", digest("efgh"), 4, "https://github.com/b"}}};
    const std::vector<std::filesystem::path> paths{a.toStdWString(), b.toStdWString()};
    std::string error;
    auto broken = asset; broken.sha256 = digest("wrong-final");
    EXPECT_FALSE(pfservices::ProviderStore::assembleParts(broken, paths, output.toStdWString(), error));
    EXPECT_EQ(get(output), "previous");
    put(b, "evil");
    EXPECT_FALSE(pfservices::ProviderStore::assembleParts(asset, paths, output.toStdWString(), error));
    EXPECT_EQ(get(output), "previous");
    put(b, "efgh");
    EXPECT_FALSE(pfservices::ProviderStore::assembleParts(asset, {paths[0]}, output.toStdWString(), error));
    EXPECT_TRUE(pfservices::ProviderStore::assembleParts(asset, paths, output.toStdWString(), error));
    EXPECT_EQ(get(output), "abcdefgh");
}

TEST(ProviderStore, ManifestRejectsUnsafeDuplicateAndWrongSizeParts)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("providers.json");
    QJsonObject part{{"archive", "runtime.zip.part001"}, {"sizeBytes", 4},
        {"sha256", QString::fromStdString(digest("abcd"))}, {"downloadUrl", "https://github.com/test/part"}};
    QJsonObject provider{{"provider", "cuda"}, {"archive", "runtime.zip"}, {"sizeBytes", 4},
        {"sha256", QString::fromStdString(digest("abcd"))}, {"parts", QJsonArray{part}}};
    std::string error;
    const auto read = [&]() {
        put(path, QJsonDocument(QJsonObject{{"providers", QJsonArray{provider}}}).toJson());
        return pfservices::ProviderStore::readManifest(path.toStdWString(), "cuda", error);
    };
    ASSERT_TRUE(read());
    provider["sizeBytes"] = 5; EXPECT_FALSE(read());
    provider["sizeBytes"] = 8; provider["parts"] = QJsonArray{part, part}; EXPECT_FALSE(read());
    part["archive"] = "../escape"; provider["parts"] = QJsonArray{part}; provider["sizeBytes"] = 4; EXPECT_FALSE(read());
    provider.remove("parts"); provider["downloadUrl"] = "https://github.com/test/runtime.zip";
    ASSERT_TRUE(read()); // original single-archive manifests remain supported
}

TEST(ProviderStore, PreparedReleasePayloadsVerifyAndReassemble)
{
    const auto root = qEnvironmentVariable("PF_TEST_PROVIDER_ASSETS");
    if (root.isEmpty()) GTEST_SKIP() << "Set PF_TEST_PROVIDER_ASSETS for multi-gigabyte package verification";
    QTemporaryDir temporary; ASSERT_TRUE(temporary.isValid());
    for (const auto* provider : {"cuda", "tensorrt", "dml"}) {
        std::string error;
        const auto asset = pfservices::ProviderStore::readManifest(
            (root + "/providers.draft.json").toStdWString(), provider, error);
        ASSERT_TRUE(asset) << error;
        auto archive = std::filesystem::path((root + "/" + QString::fromStdString(asset->archive)).toStdWString());
        if (!asset->parts.empty()) {
            std::vector<std::filesystem::path> paths;
            for (const auto& part : asset->parts)
                paths.emplace_back((root + "/" + QString::fromStdString(part.archive)).toStdWString());
            archive = temporary.filePath(QString::fromStdString(asset->archive)).toStdWString();
            ASSERT_TRUE(pfservices::ProviderStore::assembleParts(*asset, paths, archive, error)) << error;
        }
        ASSERT_EQ(std::filesystem::file_size(archive), asset->sizeBytes);
        ASSERT_TRUE(pfservices::ModelStore::verifySha256(archive, asset->sha256, error)) << error;
        QProcess tar;
        tar.start("tar.exe", {"-tf", QString::fromStdWString(archive.wstring())});
        ASSERT_TRUE(tar.waitForFinished(30000)); ASSERT_EQ(tar.exitCode(), 0);
        EXPECT_TRUE(tar.readAllStandardOutput().contains("onnxruntime.dll"));
    }
}

TEST(ProviderStore, FetchesPublishedRuntimeManifestThroughGitHubRedirect)
{
    if (!qEnvironmentVariableIsSet("PF_TEST_GITHUB_DOWNLOAD"))
        GTEST_SKIP() << "Set PF_TEST_GITHUB_DOWNLOAD=1 for the live release test";
    int argc = 1;
    char applicationName[] = "pf_tests";
    char* argv[] = {applicationName, nullptr};
    QCoreApplication application(argc, argv);
    std::string error;
    const auto asset = pfservices::ProviderStore::fetchManifest(
        "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/runtime-v1/providers.json",
        "cuda", error);
    ASSERT_TRUE(asset.has_value()) << error;
    EXPECT_EQ(asset->provider, "cuda");
    EXPECT_FALSE(asset->downloadUrl.empty() && asset->parts.empty());
    EXPECT_GT(asset->sizeBytes, 0U);
}
