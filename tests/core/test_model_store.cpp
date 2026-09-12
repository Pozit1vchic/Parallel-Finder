#include <gtest/gtest.h>

#include <pfservices/ModelStore.hpp>

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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

} // namespace
