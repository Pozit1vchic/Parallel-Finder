#include <gtest/gtest.h>

#include <pfservices/ModelStore.hpp>
#include <pfservices/ProviderStore.hpp>

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <filesystem>
#include <memory>
#include "../../services/src/ModelPublication.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

TEST(ModelStore, CompleteVerifiedPartialInstallsWithoutNetwork)
{
    int argc = 1;
    char applicationName[] = "pf_tests";
    char* argv[] = {applicationName, nullptr};
    std::unique_ptr<QCoreApplication> application;
    if (!QCoreApplication::instance()) application = std::make_unique<QCoreApplication>(argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto destination = std::filesystem::path(directory.filePath("model.onnx").toStdWString());
    auto partial = destination;
    partial += ".part";
    QFile data(QString::fromStdWString(partial.wstring()));
    ASSERT_TRUE(data.open(QIODevice::WriteOnly));
    ASSERT_EQ(data.write("model-data"), 10);
    data.close();
    pfservices::ModelAsset asset;
    asset.filename = "model.onnx";
    asset.sizeBytes = 10;
    asset.sha256 = QCryptographicHash::hash(QByteArray("model-data"), QCryptographicHash::Sha256).toHex().toStdString();
    // No request should be made; bound an accidental regression into the
    // network branch so this component test cannot hang on a connection.
    asset.downloadUrl = "https://github.com:1/unused/model.onnx";
    std::stop_source cancellation;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] { cancellation.request_stop(); });
    watchdog.start(1000);
    std::string error;
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error, cancellation.get_token())) << error;
    EXPECT_FALSE(cancellation.stop_requested());
    EXPECT_FALSE(std::filesystem::exists(partial));
    ASSERT_TRUE(pfservices::ModelStore::verifySha256(destination, asset.sha256, error)) << error;
}

TEST(ModelStore, FailedPublicationPreservesExistingModel)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path(directory.filePath("pose.onnx").toStdWString());
    QFile previous(QString::fromStdWString(target.wstring()));
    ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
    ASSERT_EQ(previous.write("previous-model"), 14);
    previous.close();
    std::error_code error;
    pfservices::detail::installDownloadedModel(target.parent_path() / "missing.part", target, error);
    EXPECT_TRUE(error);
    ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
    EXPECT_EQ(previous.readAll(), QByteArray("previous-model"));
}

TEST(ModelStore, PublicationDoesNotReplaceAnExistingDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path(directory.filePath("pose.onnx").toStdWString());
    ASSERT_TRUE(std::filesystem::create_directory(target));
    const auto partial = target.parent_path() / "pose.onnx.part";
    QFile downloaded(QString::fromStdWString(partial.wstring()));
    ASSERT_TRUE(downloaded.open(QIODevice::WriteOnly));
    ASSERT_EQ(downloaded.write("verified-model"), 14);
    downloaded.close();
    std::error_code error;
    pfservices::detail::installDownloadedModel(partial, target, error);
    EXPECT_TRUE(error);
    EXPECT_TRUE(std::filesystem::is_directory(target));
    EXPECT_TRUE(std::filesystem::is_regular_file(partial));
}

TEST(ModelStore, PublicationReplacesOnlyTheRequestedModel)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path(directory.filePath("модель.onnx").toStdWString());
    const auto partial = target.parent_path() / "model.part";
    QFile previous(QString::fromStdWString(target.wstring()));
    ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
    ASSERT_EQ(previous.write("previous-model"), 14);
    previous.close();
    QFile downloaded(QString::fromStdWString(partial.wstring()));
    ASSERT_TRUE(downloaded.open(QIODevice::WriteOnly));
    ASSERT_EQ(downloaded.write("verified-model"), 14);
    downloaded.close();
    std::error_code error = std::make_error_code(std::errc::permission_denied);
    pfservices::detail::installDownloadedModel(partial, target, error);
    EXPECT_FALSE(error) << error.message();
    EXPECT_FALSE(std::filesystem::exists(partial));
    ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
    EXPECT_EQ(previous.readAll(), QByteArray("verified-model"));
    previous.close();
    const auto firstInstall = target.parent_path() / "first.onnx";
    pfservices::detail::installDownloadedModel(target, firstInstall, error);
    EXPECT_FALSE(error) << error.message();
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(std::filesystem::is_regular_file(firstInstall));
}

#ifdef _WIN32
TEST(ModelStore, PartialSharingFailurePreservesModelAndAllowsRetry)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = std::filesystem::path(directory.filePath("pose.onnx").toStdWString());
    const auto partial = target.parent_path() / "pose.onnx.part";
    QFile previous(QString::fromStdWString(target.wstring()));
    ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
    ASSERT_EQ(previous.write("previous-model"), 14);
    previous.close();
    QFile downloaded(QString::fromStdWString(partial.wstring()));
    ASSERT_TRUE(downloaded.open(QIODevice::WriteOnly));
    ASSERT_EQ(downloaded.write("verified-model"), 14);
    downloaded.close();
    std::unique_ptr<void, decltype(&CloseHandle)> held(
        CreateFileW(partial.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &CloseHandle);
    ASSERT_NE(held.get(), INVALID_HANDLE_VALUE);
    std::error_code error;
    pfservices::detail::installDownloadedModel(partial, target, error);
    EXPECT_TRUE(error);
    EXPECT_TRUE(std::filesystem::is_regular_file(partial));
    ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
    EXPECT_EQ(previous.readAll(), QByteArray("previous-model"));
    previous.close();
    held.reset();
    pfservices::detail::installDownloadedModel(partial, target, error);
    EXPECT_FALSE(error) << error.message();
    EXPECT_FALSE(std::filesystem::exists(partial));
    ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
    EXPECT_EQ(previous.readAll(), QByteArray("verified-model"));
}
#endif

TEST(ModelStore, CancelledOperationsDoNotTouchFilesOrStartDownloads)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto destination = std::filesystem::path(directory.filePath("models/test.onnx").toStdWString());
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto stop = cancellation.get_token();
    std::string error;
    pfservices::ModelAsset model;
    model.filename = "test.onnx";
    model.sizeBytes = 10;
    model.downloadUrl = "https://github.com/example/test.onnx";
    EXPECT_FALSE(pfservices::ModelStore::download(model, destination, {}, error, stop));
    EXPECT_EQ(error, "operation cancelled");
    EXPECT_FALSE(std::filesystem::exists(destination.parent_path()));
    EXPECT_FALSE(pfservices::ModelStore::fetchManifest(model.downloadUrl, model.filename, error, stop));
    EXPECT_FALSE(pfservices::ModelStore::verifySha256(destination, std::string(64, '0'), error, stop));
    pfservices::ProviderAsset provider;
    provider.provider = "cuda";
    provider.archive = "cuda.zip";
    const auto cuda = destination.parent_path() / "cuda";
    EXPECT_FALSE(pfservices::ProviderStore::fetchManifest(model.downloadUrl, "cuda", error, stop));
    EXPECT_FALSE(pfservices::ProviderStore::downloadAndInstall(provider, cuda, {}, error, stop));
    EXPECT_FALSE(pfservices::ProviderStore::assembleParts(provider, {}, cuda, error, stop));
    EXPECT_FALSE(pfservices::ProviderStore::downloadDirectMl(destination.parent_path() / "dml", {}, error, stop));
    EXPECT_EQ(error, "operation cancelled");
    EXPECT_FALSE(std::filesystem::exists(destination.parent_path()));
}

TEST(ModelStore, VerifiesAndResolvesLocalAsset)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-model-test";
    const auto models = root / "models";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(models);
    const auto model = models / "test.onnx";
    QFile file(QString::fromStdWString(model.wstring()));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("model-data"), 10);
    file.close();
    const auto hash = QCryptographicHash::hash(QByteArray("model-data"), QCryptographicHash::Sha256).toHex().toStdString();

    pfservices::ModelAsset asset;
    asset.filename = "test.onnx";
    asset.sha256 = hash;
    std::string error;
    // PF_MODEL_ROOT is deliberately not mutated in tests; resolve the exact
    // root by making it the executable directory's models folder.
    const auto resolved = pfservices::ModelStore::resolve(asset, root, error);
    ASSERT_TRUE(resolved.has_value()) << error;
    EXPECT_EQ(resolved->filename().string(), "test.onnx");

    std::filesystem::remove_all(root, ignored);
}

TEST(ModelStore, ReadsManifestAssetMetadata)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-manifest-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const auto manifest = root / "manifest.json";
    QJsonObject model;
    model[QStringLiteral("filename")] = QStringLiteral("pose.onnx");
    model[QStringLiteral("sha256")] = QStringLiteral("abc123");
    model[QStringLiteral("sizeBytes")] = 42;
    model[QStringLiteral("url")] = QStringLiteral("https://example.invalid/pose.onnx");
    QJsonArray models;
    models.push_back(model);
    QJsonObject rootObject;
    rootObject[QStringLiteral("models")] = models;
    QFile file(QString::fromStdWString(manifest.wstring()));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(rootObject).toJson());
    file.close();

    std::string error;
    const auto asset = pfservices::ModelStore::readManifest(manifest, "pose.onnx", error);
    ASSERT_TRUE(asset.has_value()) << error;
    EXPECT_EQ(asset->sha256, "abc123");
    EXPECT_EQ(asset->sizeBytes, 42U);
    EXPECT_EQ(asset->downloadUrl, "https://example.invalid/pose.onnx");
    std::filesystem::remove_all(root, ignored);
}

TEST(ModelStore, RejectsNonHttpsDownloads)
{
    const auto destination = std::filesystem::temp_directory_path() / "parallel-finder-download-test.onnx";
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    std::filesystem::remove(destination.string() + ".part", ignored);

    pfservices::ModelAsset asset;
    asset.downloadUrl = "http://example.invalid/model.onnx";
    std::string error;
    EXPECT_FALSE(pfservices::ModelStore::download(asset, destination, {}, error));
    EXPECT_EQ(error, "model download requires an HTTPS URL");
    EXPECT_FALSE(std::filesystem::exists(destination));
}

TEST(ModelStore, DownloadsPublishedReleaseAssetThroughGitHubRedirect)
{
    if (!qEnvironmentVariableIsSet("PF_TEST_GITHUB_DOWNLOAD"))
        GTEST_SKIP() << "Set PF_TEST_GITHUB_DOWNLOAD=1 for the live release test";
    int argc = 1;
    char applicationName[] = "pf_tests";
    char* argv[] = {applicationName, nullptr};
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::ModelAsset asset;
    asset.filename = "manifest.json";
    asset.sizeBytes = 6490;
    asset.sha256 = "fc3af3b1a31c440c1380185e154bc603cba9fb3dca6d3790fb6678f788d1f8f5";
    asset.downloadUrl = "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/v0.1.0-models/manifest.json";
    std::string error;
    const auto destination = std::filesystem::path(directory.filePath("manifest.json").toStdWString());
    QFile previous(QString::fromStdWString(destination.wstring()));
    ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
    ASSERT_EQ(previous.write("previous-manifest"), 17);
    previous.close();
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error)) << error;
    ASSERT_TRUE(pfservices::ModelStore::verifySha256(destination, asset.sha256, error)) << error;
    // The exact verified asset remains reusable after publication.
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error)) << error;
    auto completePartial = destination;
    completePartial += ".part";
    ASSERT_TRUE(QFile::rename(QString::fromStdWString(destination.wstring()),
                             QString::fromStdWString(completePartial.wstring())));
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(completePartial));
    ASSERT_TRUE(pfservices::ModelStore::verifySha256(destination, asset.sha256, error)) << error;
    // A complete but invalid old partial must restart instead of requesting
    // another byte past the end and getting permanently stuck on HTTP 416.
    ASSERT_TRUE(QFile::remove(QString::fromStdWString(destination.wstring())));
    QFile corrupt(QString::fromStdWString(completePartial.wstring()));
    ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly));
    ASSERT_EQ(corrupt.write(QByteArray(static_cast<qsizetype>(asset.sizeBytes), 'x')),
              static_cast<qint64>(asset.sizeBytes));
    corrupt.close();
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error)) << error;
    ASSERT_TRUE(pfservices::ModelStore::verifySha256(destination, asset.sha256, error)) << error;
}

TEST(ModelStore, DownloadsPublishedYoloAndRuntimeAssets)
{
    if (!qEnvironmentVariableIsSet("PF_TEST_RELEASE_ASSETS"))
        GTEST_SKIP() << "Set PF_TEST_RELEASE_ASSETS=1 for the live binary download test";
    int argc = 1;
    char applicationName[] = "pf_tests";
    char* argv[] = {applicationName, nullptr};
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::string error;
    const auto model = pfservices::ModelStore::fetchManifest(
        "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/v0.1.0-models/manifest.json",
        "yolo26n-pose.onnx", error);
    ASSERT_TRUE(model.has_value()) << error;
    const auto modelPath = std::filesystem::path(directory.path().toStdWString())
        / L"модели" / L"yolo26n-pose.onnx";
    ASSERT_TRUE(pfservices::ModelStore::download(*model, modelPath, {}, error)) << error;
    EXPECT_EQ(std::filesystem::file_size(modelPath), model->sizeBytes);

    error.clear();
    const auto provider = pfservices::ProviderStore::fetchManifest(
        "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/runtime-v1/providers.json",
        "dml", error);
    ASSERT_TRUE(provider.has_value()) << error;
    ASSERT_FALSE(provider->downloadUrl.empty());
    pfservices::ModelAsset archive;
    archive.filename = provider->archive;
    archive.sha256 = provider->sha256;
    archive.sizeBytes = provider->sizeBytes;
    archive.downloadUrl = provider->downloadUrl;
    const auto archivePath = std::filesystem::path(directory.filePath("runtime.zip").toStdWString());
    ASSERT_TRUE(pfservices::ModelStore::download(archive, archivePath, {}, error)) << error;
    EXPECT_EQ(std::filesystem::file_size(archivePath), archive.sizeBytes);
}

} // namespace
