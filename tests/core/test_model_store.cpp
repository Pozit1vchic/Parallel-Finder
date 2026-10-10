#include "../../services/src/NetworkOperation.hpp"
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
#include <QEventLoop>

#include <filesystem>
#include <atomic>
#include <future>

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

namespace {
class BufferedTestReply final : public QNetworkReply {
public:
    BufferedTestReply() { open(QIODevice::ReadOnly); }
    void abort() override { aborted = true; emit finished(); }
    qint64 bytesAvailable() const override { return bytes + QNetworkReply::bytesAvailable(); }
    void deliver(qint64 count) { bytes += count; emit readyRead(); }
    bool aborted = false;
protected:
    qint64 readData(char*, qint64) override { return -1; }
private:
    qint64 bytes = 0;
};
}
TEST(ModelStore, BufferedManifestStopsAtLimitBeforeTransferFinishes) {
    BufferedTestReply reply;
    std::string error;
    pfservices::detail::boundManifestReply(reply, 1024, error, "too large");
    EXPECT_EQ(reply.readBufferSize(), 1025);
    reply.deliver(1024);
    EXPECT_FALSE(reply.aborted);
    EXPECT_TRUE(error.empty());
    reply.deliver(1);
    EXPECT_TRUE(reply.aborted);
    EXPECT_EQ(error, "too large");
}

TEST(ModelStore, CompleteVerifiedPartialRetriesPublicationWithoutNetwork) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto destination = std::filesystem::path(directory.filePath("sample.onnx").toStdWString());
    auto partial = destination; partial += ".part";
    QFile file(QString::fromStdWString(partial.wstring()));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("new-model"), 9); file.close();
    pfservices::ModelAsset asset;
    asset.filename = "sample.onnx"; asset.sizeBytes = 9;
    asset.sha256 = QCryptographicHash::hash(QByteArray("new-model"), QCryptographicHash::Sha256).toHex().toStdString();
    asset.downloadUrl = "https://github.com/example/unused";
    // A directory prevents publication. The verified partial must survive.
    std::filesystem::create_directory(destination);
    std::string error;
    EXPECT_FALSE(pfservices::ModelStore::download(asset, destination, {}, error));
    EXPECT_TRUE(std::filesystem::is_directory(destination));
    EXPECT_TRUE(std::filesystem::is_regular_file(partial));
    std::filesystem::remove(destination);
    QFile previous(QString::fromStdWString(destination.wstring()));
    ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
    ASSERT_EQ(previous.write("old"), 3); previous.close();
    error.clear();
    ASSERT_TRUE(pfservices::ModelStore::download(asset, destination, {}, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(partial));
    EXPECT_TRUE(pfservices::ModelStore::verifySha256(destination, asset.sha256, error));
}

TEST(ModelStore, InterruptionAbortsReplyOnItsOwningThread) {
    int argc = 1;
    char name[] = "pf-network-test";
    char* argv[] = {name, nullptr};
    std::unique_ptr<QCoreApplication> application;
    if (!QCoreApplication::instance()) application = std::make_unique<QCoreApplication>(argc, argv);
    std::promise<void> started;
    auto ready = started.get_future();
    std::atomic_bool aborted = false;
    std::string error;
    std::unique_ptr<QThread> worker(QThread::create([&] {
        BufferedTestReply reply;
        QTimer timer;
        QEventLoop loop;
        QObject::connect(&reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        pfservices::detail::watchInterruption(timer, reply, error);
        started.set_value();
        loop.exec();
        aborted = reply.aborted;
    }));
    worker->start();
    const bool running = ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    worker->requestInterruption();
    const bool stopped = worker->wait(2000);
    if (!stopped) { worker->quit(); worker->wait(); }
    EXPECT_TRUE(running);
    EXPECT_TRUE(stopped);
    EXPECT_TRUE(aborted);
    EXPECT_EQ(error, "download cancelled");
}
