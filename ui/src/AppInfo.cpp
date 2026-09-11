#include "AppInfo.h"

#include <QCoreApplication>
#include <QQmlEngine>

namespace pfui {
namespace {
constexpr QChar kSeparator = QChar(0x00B7); // middle dot: "cuda · RTX 4070"
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
        return m_gpuBackend;
    }
    return m_gpuBackend + QStringLiteral(" ") + kSeparator + QStringLiteral(" ")
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
