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

#include <filesystem>

namespace {

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
