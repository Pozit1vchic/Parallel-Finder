#include "AppInfo.h"

#include <QCoreApplication>
#include <QQmlEngine>

namespace pfui {

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

void AppInfo::setGpuBackend(const QString& backend)
{
    if (m_gpuBackend == backend) {
        return;
    }
    m_gpuBackend = backend;
    emit gpuBackendChanged();
}

} // namespace pfui
