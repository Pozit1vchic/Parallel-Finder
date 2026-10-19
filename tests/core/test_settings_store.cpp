#include <gtest/gtest.h>

#include <pfservices/SettingsStore.hpp>

#include <QDir>
#include <QTemporaryDir>
#include <QUrl>
#include <QFile>

#include <filesystem>
#include <atomic>
#include <stdexcept>

namespace {
TEST(SettingsStore, AllSixInterfaceLanguagesSurviveDiskRoundTrip)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("settings.json").toStdString());
    std::string error;
    for(const std::string language:{"ru","en","ky","kk","uk","tr"}) {
        pfservices::Settings settings;settings.language=language;
        ASSERT_TRUE(store.save(settings,error))<<error;
        EXPECT_EQ(store.load(error).language,language);
    }
}
TEST(SettingsStore, McpDefaultsAndMalformedSettingsDoNotEnableRemoteControl)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("settings.json").toStdString());std::string error;
    auto settings=store.load(error);EXPECT_FALSE(settings.mcpEnabled);EXPECT_FALSE(settings.mcpAllowControl);EXPECT_EQ(settings.mcpPort,32145);EXPECT_TRUE(settings.mcpToken.empty());
    settings.mcpEnabled=true;settings.mcpAllowControl=true;settings.mcpPort=32456;settings.mcpToken=std::string(64,'a');
    ASSERT_TRUE(store.save(settings,error));const auto restored=store.load(error);
    EXPECT_TRUE(restored.mcpEnabled);EXPECT_TRUE(restored.mcpAllowControl);EXPECT_EQ(restored.mcpPort,32456);EXPECT_EQ(restored.mcpToken,settings.mcpToken);
    QFile malformed(directory.filePath("settings.json"));ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
    malformed.write(R"({"mcpEnabled":"yes","mcpAllowControl":1,"mcpPort":1234.5,"mcpToken":"short"})");malformed.close();
    const auto safe=store.load(error);EXPECT_FALSE(safe.mcpEnabled);EXPECT_FALSE(safe.mcpAllowControl);EXPECT_EQ(safe.mcpPort,32145);EXPECT_TRUE(safe.mcpToken.empty());
}

TEST(SettingsStore, MigratesCachedFolderUrlWithoutDecodingPlainPaths)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    const QString local = directory.path() + QString::fromUtf8("/кэш с пробелами");
    pfservices::Settings legacy;
    legacy.cachePath = QUrl::fromLocalFile(local).toString(QUrl::FullyEncoded).toStdString();
    std::string error;
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).cachePath, local.toStdString());
    legacy.cachePath = (directory.path() + "/literal%20folder").toStdString();
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).cachePath, legacy.cachePath);
}

TEST(SettingsStore, MigratesRetiredClipModeToMotion)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    pfservices::Settings legacy;
    legacy.analysisMode = "clips";
    std::string error;
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).analysisMode, "motion");
}

TEST(SettingsStore, MissingFileDefaultsToEnglishAndAppearanceRoundTrips)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::string error;
    auto settings = store.load(error);
    EXPECT_EQ(settings.language, "en");
    EXPECT_FALSE(settings.expandedSearch);
    settings.expandedSearch = true;
    settings.language = "ru";
    settings.appearance.insert("accentColor", "blue");
    ASSERT_TRUE(store.save(settings, error));
    auto restored = store.load(error);
    EXPECT_EQ(restored.language, "ru");
    EXPECT_TRUE(restored.expandedSearch);
    EXPECT_EQ(restored.appearance.value("accentColor").toString(), "blue");
}

TEST(SettingsStore, RemembersModelChoiceIndependentlyOfDownloadedFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::string error;
    auto settings = store.load(error);
    EXPECT_TRUE(settings.modelChoice.empty());
    settings.modelPath = "C:/models/yolo26m-pose.onnx";
    settings.modelChoice = "yolo26s-pose.onnx";
    ASSERT_TRUE(store.save(settings, error)) << error;
    const auto restored = store.load(error);
    EXPECT_EQ(restored.modelPath, settings.modelPath);
    EXPECT_EQ(restored.modelChoice, "yolo26s-pose.onnx");
}

TEST(SettingsStore, SavesAndLoadsAtomically)
{
    const auto path = std::filesystem::temp_directory_path() / "parallel-finder-settings-test" / "settings.json";
    std::error_code ignored;
    std::filesystem::remove_all(path.parent_path(), ignored);

    pfservices::SettingsStore store(path.string());
    pfservices::Settings expected;
    expected.provider = "dml";
    expected.cacheLimitBytes = 123456;
    expected.processingThreads = 12;
    expected.sceneThreshold = 42.0;
    expected.sceneMinFrames = 6;
    expected.similarityThreshold = 0.91;
    expected.candidateThreshold = 0.61;
    expected.minRepeatGapSec = 7.0;
    expected.sameFileGapSec = 2.5;
    expected.crossFileGapSec = 1.25;
    expected.duplicateWindowSec = 1.75;
    expected.noiseFactor = 0.8;
    expected.maxUniqueResults = 321;
    expected.timeWeight = 0.35;
    expected.sakoeChibaRatio = 0.15;
    std::string error;
    ASSERT_TRUE(store.save(expected, error)) << error;

    const auto loaded = store.load(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(loaded.provider, "dml");
    EXPECT_EQ(loaded.cacheLimitBytes, 123456U);
    EXPECT_EQ(loaded.processingThreads, 12U);
    EXPECT_DOUBLE_EQ(loaded.sceneThreshold, 42.0);
    EXPECT_EQ(loaded.sceneMinFrames, 6U);
    EXPECT_DOUBLE_EQ(loaded.similarityThreshold, 0.91);
    EXPECT_DOUBLE_EQ(loaded.candidateThreshold, 0.61);
    EXPECT_DOUBLE_EQ(loaded.minRepeatGapSec, 7.0);
    EXPECT_DOUBLE_EQ(loaded.sameFileGapSec, 2.5);
    EXPECT_DOUBLE_EQ(loaded.crossFileGapSec, 1.25);
    EXPECT_DOUBLE_EQ(loaded.duplicateWindowSec, 1.75);
    EXPECT_DOUBLE_EQ(loaded.noiseFactor, 0.8);
    EXPECT_EQ(loaded.maxUniqueResults, 321U);
    EXPECT_DOUBLE_EQ(loaded.timeWeight, 0.35);
    EXPECT_DOUBLE_EQ(loaded.sakoeChibaRatio, 0.15);

    std::filesystem::remove_all(path.parent_path(), ignored);
}

} // namespace

TEST(SettingsStore, WriterSurvivesThrowingChangeAndCompletion) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::atomic_bool reported = false;
    store.updateAsync("broken-change", [](auto&) { throw 42; },
        [&](std::string error) { reported = !error.empty(); throw std::runtime_error("callback"); });
    store.updateAsync("language", [](auto& settings) { settings.language = "ru"; });
    pfservices::SettingsStore::flushPendingWrites();
    EXPECT_TRUE(reported);
    std::string error;
    EXPECT_EQ(store.load(error).language, "ru");
    EXPECT_TRUE(error.empty());
}
