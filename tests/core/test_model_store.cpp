#include <gtest/gtest.h>

#include <pfservices/ModelStore.hpp>

#include <QCryptographicHash>
#include <QFile>

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

} // namespace
