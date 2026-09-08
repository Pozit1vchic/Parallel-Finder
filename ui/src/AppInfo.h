#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace pfui {

// Thin read-only bridge exposed to QML as PfUi.AppInfo (singleton).
// UI never computes anything here — GPU probing lives in pfgpu, the value is
// injected from app main (init step), see spec section 2.
//
// The type is registered manually via registerQmlTypes() (called by the app
// and by UI tests before the engine loads). We deliberately do not use
// QML_ELEMENT + the module plugin: consumers would need to link and import
// the static plugin; manual registration keeps it explicit and testable.
class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString gpuBackend READ gpuBackend WRITE setGpuBackend NOTIFY gpuBackendChanged)

public:
    static AppInfo* instance();

    // Registers PfUi.AppInfo as a QML singleton. Call once per process
    // before creating any QQmlEngine that uses it.
    static void registerQmlTypes();

    explicit AppInfo(QObject* parent = nullptr);

    QString version() const;
    QString gpuBackend() const;
    void setGpuBackend(const QString& backend);

signals:
    void gpuBackendChanged();

private:
    QString m_gpuBackend = QStringLiteral("cpu");
};

} // namespace pfui
