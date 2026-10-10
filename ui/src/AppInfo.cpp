#include "BackendProbeProcess.h"
#include "AppInfo.h"
#include <QKeySequence>
#include "AnalysisController.h"
#include "DiscordPresence.h"
#include <pfupdate/UpdateService.hpp>
#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/Provider.hpp>
#include <pfservices/ProviderStore.hpp>
#include <pfservices/SettingsStore.hpp>
#include <pfservices/ProviderManager.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QQmlEngine>
#include <QThread>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace pfui {
namespace {
constexpr QChar kSeparator = QChar(0x00B7); // middle dot: "CUDA · RTX 4070"
constexpr auto kProviderManifestUrl = "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/runtime-v1/providers.json";
constexpr auto kProviderManifestFallbackUrl = "https://raw.githubusercontent.com/Pozit1vchic/Parallel-Finder/main/providers/providers.json";

QString displayBackend(const QString& backend)
{
    const QString key = backend.trimmed().toLower();
    if (key == QStringLiteral("dml") || key == QStringLiteral("directml")) return QStringLiteral("DirectML");
    if (key == QStringLiteral("cuda")) return QStringLiteral("CUDA");
    if (key == QStringLiteral("tensorrt")) return QStringLiteral("TensorRT");
    if (key == QStringLiteral("cpu")) return QStringLiteral("CPU");
    return backend;
}
}

AppInfo* AppInfo::instance()
{
    static AppInfo inst;
    return &inst;
}

void AppInfo::registerQmlTypes()
{
    // Own URI: PfUi is a "protected" QML module (its qmldir/plugin is
    // generated), installing extra singletons into it is rejected. A separate
    // C++-registered URI also avoids the one-engine limitation of
    // qmlRegisterSingletonInstance on module URIs backed by a plugin.
    qmlRegisterSingletonInstance("PfUiBridge", 1, 0, "AppInfo", instance());
    AnalysisController::registerQmlTypes();
    static pfupdate::UpdateService updates({QCoreApplication::applicationVersion().isEmpty()?QStringLiteral(PF_VERSION):QCoreApplication::applicationVersion(),
        QString::fromStdString(pfservices::SettingsStore::defaultDirectory())+"/updates",QCoreApplication::applicationDirPath()});
    updates.setCanInstall([]{const auto* analysis=AnalysisController::instance();return !analysis->busy() && !analysis->exportBusy();});
    qmlRegisterSingletonInstance("PfUiBridge",1,0,"Updates",&updates);
    static DiscordPresence discord;
    qmlRegisterSingletonInstance("PfUiBridge", 1, 0, "Discord", &discord);
}

AppInfo::~AppInfo()
{
    workerThreads_.finish();
    providerScan_.request_stop();
    if (providerScan_.joinable()) providerScan_.join();
    backendProbe_.request_stop();
    if (backendProbe_.joinable()) backendProbe_.join();
}

AppInfo::AppInfo(QObject* parent)
    : QObject(parent)
{
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        workerThreads_.finish();
        pfservices::SettingsStore::flushPendingWrites();
        providerScan_.request_stop();
        if (providerScan_.joinable()) providerScan_.join();
        backendProbe_.request_stop();
        if (backendProbe_.joinable()) backendProbe_.join();
    });
}

void AppInfo::prepareBackendInitialization()
{
    if (backendInitializing_ || backendSnapshotReady_) return;
    backendInitializing_ = true;
    emit backendInitializationChanged();
}

void AppInfo::initializeBackendsAsync(std::function<pfgpu::BackendProbe()> probe)
{
    startBackendProbe([probe = std::move(probe)](std::stop_token) { return probe ? probe() : pfgpu::probeBackends(); });
}

void AppInfo::initializeBackendsIsolated()
{
    const auto executable = QCoreApplication::applicationFilePath();
    startBackendProbe([executable](std::stop_token stop) { return probeBackendProcess(executable, stop); });
}

void AppInfo::startBackendProbe(std::function<pfgpu::BackendProbe(std::stop_token)> probe)
{
    if (backendProbe_.joinable() || backendSnapshotReady_) return;
    prepareBackendInitialization();
    backendProbe_ = std::jthread([this, probe = std::move(probe)](std::stop_token stop) {
        pfgpu::BackendProbe result;
        try { result = probe(stop); }
        catch (const std::exception& error) { result.ortError = error.what(); }
        if (stop.stop_requested()) return;
        QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
            publishBackendProbe(result);
        }, Qt::QueuedConnection);
    });
}

void AppInfo::publishBackendProbe(const pfgpu::BackendProbe& probe)
{
    backendStatuses_.clear();
    backendStatuses_.insert(QStringLiteral("auto"), QVariantMap{{"available", probe.ortLoaded},
        {"reason", QString::fromStdString(probe.ortError)}});
    const pfgpu::BackendStatus* selected = nullptr;
    for (const auto& status : probe.backends) {
        backendStatuses_.insert(QString::fromLatin1(pfgpu::providerName(status.provider)).toLower(),
            QVariantMap{{"available", status.available}, {"reason", QString::fromStdString(status.reason)}});
        if (!selected && status.available) selected = &status;
    }
    backendSnapshotReady_ = true;
    backendInitializing_ = false;
    setGpuInfo(selected ? QString::fromLatin1(pfgpu::providerName(selected->provider)) : QStringLiteral("cpu"),
        selected ? QString::fromStdString(selected->deviceName) : QString(),
        selected && selected->provider != pfgpu::Provider::Cpu, QString::fromStdString(probe.ortVersion));
    ++providersRevision_;
    emit backendInitializationChanged();
    emit providersChanged();
}

QVariantMap AppInfo::providerInstallation(const QString& backend) const
{
    return installations_.value(backend.trimmed().toLower()).toMap();
}

void AppInfo::rescanProviders()
{
    if (backendInitializing_ || providersScanning_ || providerDownloading_) return;
    providersScanning_ = true;
    emit providersChanged();
    const QStringList roots{
        QString::fromStdString(pfservices::SettingsStore::defaultDirectory()) + "/providers",
        QCoreApplication::applicationDirPath() + "/providers"};
    const auto executable = QCoreApplication::applicationFilePath();
    providerScan_ = std::jthread([this, roots, executable](std::stop_token stop) {
        QVariantMap installations;
        try {
            for (const auto& state : pfservices::ProviderManager::scan(roots, executable, stop)) {
                installations.insert(state.name, QVariantMap{{"installed", state.installed},
                    {"available", state.available}, {"reason", state.reason}, {"runtimePath", state.runtimePath}});
            }
        } catch (const std::exception& error) {
            for (const auto* name : {"dml", "cuda", "tensorrt"})
                installations.insert(QString::fromLatin1(name), QVariantMap{{"available", false},
                    {"reason", QString::fromUtf8(error.what())}});
        }
        QMetaObject::invokeMethod(this, [this, installations] {
            installations_ = installations;
            providersScanning_ = false;
            ++providersRevision_;
            emit providersChanged();
        }, Qt::QueuedConnection);
    });
}

QString AppInfo::version() const
{
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral(PF_VERSION) : v;
}

QVariantMap AppInfo::loadPreferences() const
{
    if (preferences_) return *preferences_;
    std::string error;
    const auto settings = pfservices::SettingsStore().load(error);
    auto result = settings.appearance.toVariantMap();
    result.insert(QStringLiteral("language"), QString::fromStdString(settings.language));
    return result;
}

QString AppInfo::keySequence(int key, int modifiers) const
{
    if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift
        || key == Qt::Key_Alt || key == Qt::Key_Meta) return {};
    const auto flags = Qt::KeyboardModifiers(modifiers) & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(flags, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText);
}

bool AppInfo::savePreferences(const QVariantMap& preferences)
{
    if (preferences_ && *preferences_ == preferences) return true;
    preferences_ = preferences;
    emit preferencesChanged();
    const QPointer<AppInfo> guard(this);
    pfservices::SettingsStore().updateAsync("appearance", [preferences](auto& settings) {
        settings.language = preferences.value(QStringLiteral("language")).toString() == QStringLiteral("ru") ? "ru" : "en";
        settings.appearance = QJsonObject::fromVariantMap(preferences);
        settings.appearance.remove(QStringLiteral("language"));
    }, [guard](std::string error) {
        if (guard && !error.empty()) QMetaObject::invokeMethod(guard, [guard, error] {
            if (guard) emit guard->preferencesSaveFailed(QString::fromStdString(error));
        }, Qt::QueuedConnection);
    });
    return true;
}

QString AppInfo::gpuBackend() const
{
    return m_gpuBackend;
}

QString AppInfo::gpuDevice() const
{
    return m_gpuDevice;
}

QString AppInfo::gpuSummary() const
{
    if (m_gpuDevice.isEmpty()) {
        return displayBackend(m_gpuBackend);
    }
    return displayBackend(m_gpuBackend) + QStringLiteral(" ") + kSeparator + QStringLiteral(" ")
        + m_gpuDevice;
}

QString AppInfo::ortVersion() const
{
    return m_ortVersion;
}

bool AppInfo::backendIsGpu() const
{
    return m_backendIsGpu;
}

bool AppInfo::backendAvailable(const QString& backend) const
{
    if (backendInitializing_) return false; // never wait on the pfgpu probe mutex in QML
    const auto provider = pfgpu::parseProvider(backend.toStdString());
    if (!provider) return false;
    if (backendSnapshotReady_)
        return backendStatuses_.value(QString::fromLatin1(pfgpu::providerName(*provider)).toLower()).toMap().value("available").toBool();
    // A QML getter may be re-evaluated by a font/theme change. It must never
    // initialize a GPU runtime or wait for its global probe mutex.
    return false;
}

QString AppInfo::providerGuideUrl(const QString& backend) const
{
    const QString key = backend.trimmed().toLower();
    if (key == QStringLiteral("cuda"))
        return QStringLiteral("https://developer.nvidia.com/cuda-downloads");
    if (key == QStringLiteral("tensorrt"))
        return QStringLiteral("https://developer.nvidia.com/tensorrt-getting-started");
    if (key == QStringLiteral("dml"))
        return QStringLiteral("https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html");
    return QStringLiteral("https://onnxruntime.ai/docs/install/");
}

QString AppInfo::backendReason(const QString& backend) const
{
    if (backendInitializing_) return QStringLiteral("Provider initialization in progress");
    const auto provider = pfgpu::parseProvider(backend.toStdString());
    if (!provider.has_value()) return QStringLiteral("Неизвестный провайдер");
    if (backendSnapshotReady_)
        return backendStatuses_.value(QString::fromLatin1(pfgpu::providerName(*provider)).toLower()).toMap().value("reason").toString();
    return QStringLiteral("Provider initialization pending");
}

void AppInfo::downloadProvider(const QString& backend)
{
    const QString provider = backend.trimmed().toLower();
    if (provider.isEmpty() || provider == QStringLiteral("auto")
        || provider == QStringLiteral("cpu")) {
        providerDownloadStatus_ = QStringLiteral("Для CPU отдельный runtime не нужен");
        emit providerDownloadChanged();
        return;
    }
    if (backendInitializing_ || providerDownloading_ || providersScanning_) return;
    providerDownloading_ = true;
    providerDownloadState_ = QStringLiteral("checking");
    providerDownloadProgress_ = 0.0;
    providerDownloadStatus_ = QStringLiteral("Проверяем интернет и release-манифест…");
    emit providerDownloadChanged();

    QThread* thread = QThread::create([this, provider] {
        try {
        std::string error;
        std::string manifestUrl = qEnvironmentVariable("PF_PROVIDER_MANIFEST_URL").toStdString();
        if (manifestUrl.empty()) manifestUrl = kProviderManifestUrl;
        std::optional<pfservices::ProviderAsset> asset;
        if (provider != QStringLiteral("dml")) asset = pfservices::ProviderStore::fetchManifest(
            manifestUrl, provider.toStdString(), error);
        if (provider != QStringLiteral("dml") && !asset && manifestUrl == kProviderManifestUrl) {
            std::string fallbackError;
            asset = pfservices::ProviderStore::fetchManifest(
                kProviderManifestFallbackUrl, provider.toStdString(), fallbackError);
            if (!asset && !fallbackError.empty()) error += "; fallback: " + fallbackError;
        }
        if (!asset && provider != QStringLiteral("dml")) {
            const QString message = QString::fromStdString(error);
            QMetaObject::invokeMethod(this, [this, message] {
                providerDownloading_ = false;
                providerDownloadState_ = QStringLiteral("error");
                providerDownloadStatus_ = QStringLiteral("Не удалось скачать runtime: ") + message;
                emit providerDownloadChanged();
            }, Qt::QueuedConnection);
            return;
        }

        const std::filesystem::path destination = std::filesystem::path(
            QString::fromStdString(pfservices::SettingsStore::defaultDirectory()).toStdWString())
            / "providers" / provider.toStdWString();
        const auto reportProgress = [this, lastPercent = -1](std::uint64_t received, std::uint64_t total) mutable {
                const double progress = total > 0
                    ? std::clamp(static_cast<double>(received) / static_cast<double>(total), 0.0, 1.0)
                    : 0.0;
                const int percent = static_cast<int>(std::round(progress * 100.0));
                // QNetwork/curl style progress callbacks can arrive for every
                // small chunk. Flooding the GUI thread with queued updates made
                // the draggable settings popup visibly stutter during a large
                // provider download. One UI update per percentage point is more
                // than enough and bounds the event rate to ~101 updates.
                if (percent == lastPercent) return;
                lastPercent = percent;
                QMetaObject::invokeMethod(this, [this, progress, percent] {
                    providerDownloadProgress_ = progress;
                    providerDownloadState_ = QStringLiteral("downloading");
                    providerDownloadStatus_ = QStringLiteral("Скачивание runtime… %1%")
                        .arg(percent);
                    emit providerDownloadChanged();
                }, Qt::QueuedConnection);
            };
        const bool ok = provider == QStringLiteral("dml")
            ? pfservices::ProviderStore::downloadDirectMl(destination, reportProgress, error)
            : pfservices::ProviderStore::downloadAndInstall(*asset, destination, reportProgress, error);
        bool installed = ok;
        if (installed) {
            std::ofstream active(destination.parent_path() / "active.txt",
                                 std::ios::binary | std::ios::trunc);
            active << provider.toStdString();
            if (!active) {
                installed = false;
                error = "runtime installed, but provider activation marker could not be written";
            }
        }
        const QString message = installed
            ? QStringLiteral("Runtime установлен. Перезапустите приложение, чтобы применить провайдер.")
            : QStringLiteral("Не удалось установить runtime: ") + QString::fromStdString(error);
        QMetaObject::invokeMethod(this, [this, installed, message] {
            providerDownloading_ = false;
            providerDownloadState_ = installed ? QStringLiteral("installed") : QStringLiteral("error");
            providerDownloadProgress_ = installed ? 1.0 : 0.0;
            providerDownloadStatus_ = message;
            emit providerDownloadChanged();
            if (installed) rescanProviders();
        }, Qt::QueuedConnection);
        } catch (const std::exception& exception) {
            const auto message = QString::fromUtf8(exception.what());
            QMetaObject::invokeMethod(this, [this, message] {
                providerDownloading_ = false;
                providerDownloadState_ = QStringLiteral("error");
                providerDownloadStatus_ = message;
                emit providerDownloadChanged();
            }, Qt::QueuedConnection);
        }
    });
    workerThreads_.start(thread);
}

void AppInfo::setGpuInfo(const QString& backend,
                         const QString& device,
                         bool isGpu,
                         const QString& ortVersion)
{
    if (m_gpuBackend == backend && m_gpuDevice == device && m_backendIsGpu == isGpu
        && m_ortVersion == ortVersion) {
        return;
    }
    m_gpuBackend = backend;
    m_gpuDevice = device;
    m_backendIsGpu = isGpu;
    m_ortVersion = ortVersion;
    emit gpuInfoChanged();
}

} // namespace pfui
