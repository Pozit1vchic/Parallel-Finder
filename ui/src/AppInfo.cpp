#include "AppInfo.h"
#include "AnalysisController.h"
#include <pfgpu/DeviceInfo.hpp>
#include <pfgpu/Provider.hpp>
#include <pfservices/ProviderStore.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QQmlEngine>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace pfui {
namespace {
constexpr QChar kSeparator = QChar(0x00B7); // middle dot: "CUDA · RTX 4070"
constexpr auto kProviderManifestUrl = "https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/providers.json";
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
}

AppInfo::AppInfo(QObject* parent)
    : QObject(parent)
{
}

QString AppInfo::version() const
{
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral(PF_VERSION) : v;
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
    const auto provider = pfgpu::parseProvider(backend.toStdString());
    return provider.has_value() && pfgpu::isProviderAvailable(*provider);
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
    const auto provider = pfgpu::parseProvider(backend.toStdString());
    if (!provider.has_value()) return QStringLiteral("Неизвестный провайдер");
    if (const auto* status = pfgpu::findBackendStatus(*provider))
        return status->available ? QString() : QString::fromStdString(status->reason);
    return QStringLiteral("Провайдер недоступен");
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
    if (providerDownloading_) return;
    providerDownloading_ = true;
    providerDownloadProgress_ = 0.0;
    providerDownloadStatus_ = QStringLiteral("Проверяем интернет и release-манифест…");
    emit providerDownloadChanged();

    QThread* thread = QThread::create([this, provider] {
        std::string error;
        std::string manifestUrl = qEnvironmentVariable("PF_PROVIDER_MANIFEST_URL").toStdString();
        if (manifestUrl.empty()) manifestUrl = kProviderManifestUrl;
        auto asset = pfservices::ProviderStore::fetchManifest(
            manifestUrl, provider.toStdString(), error);
        if (!asset && manifestUrl == kProviderManifestUrl) {
            std::string fallbackError;
            asset = pfservices::ProviderStore::fetchManifest(
                kProviderManifestFallbackUrl, provider.toStdString(), fallbackError);
            if (!asset && !fallbackError.empty()) error += "; fallback: " + fallbackError;
        }
        if (!asset) {
            const QString message = QString::fromStdString(error);
            QMetaObject::invokeMethod(this, [this, message] {
                providerDownloading_ = false;
                providerDownloadStatus_ = QStringLiteral("Не удалось скачать runtime: ") + message;
                emit providerDownloadChanged();
            }, Qt::QueuedConnection);
            return;
        }

        const std::filesystem::path destination = std::filesystem::path(
            QCoreApplication::applicationDirPath().toStdWString())
            / "providers" / provider.toStdWString();
        const bool ok = pfservices::ProviderStore::downloadAndInstall(
            *asset, destination,
            [this](std::uint64_t received, std::uint64_t total) {
                const double progress = total > 0
                    ? std::clamp(static_cast<double>(received) / static_cast<double>(total), 0.0, 1.0)
                    : 0.0;
                QMetaObject::invokeMethod(this, [this, progress] {
                    providerDownloadProgress_ = progress;
                    providerDownloadStatus_ = QStringLiteral("Скачивание runtime… %1%")
                        .arg(static_cast<int>(std::round(progress * 100.0)));
                    emit providerDownloadChanged();
                }, Qt::QueuedConnection);
            }, error);
        if (ok) {
            std::ofstream active(destination.parent_path() / "active.txt",
                                 std::ios::binary | std::ios::trunc);
            active << provider.toStdString();
        }
        const QString message = ok
            ? QStringLiteral("Runtime установлен. Перезапустите приложение, чтобы применить провайдер.")
            : QStringLiteral("Не удалось установить runtime: ") + QString::fromStdString(error);
        QMetaObject::invokeMethod(this, [this, ok, message] {
            providerDownloading_ = false;
            providerDownloadProgress_ = ok ? 1.0 : 0.0;
            providerDownloadStatus_ = message;
            emit providerDownloadChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
