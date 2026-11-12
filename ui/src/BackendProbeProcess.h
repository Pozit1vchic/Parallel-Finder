#pragma once

#include <pfgpu/DeviceInfo.hpp>
#include <QByteArray>
#include <QStringList>
#include <QProcessEnvironment>
#include <stop_token>

namespace pfui {
QByteArray backendProbeJson(const pfgpu::BackendProbe& probe);
pfgpu::BackendProbe backendProbeFromJson(const QByteArray& json);
pfgpu::BackendProbe probeBackendProcess(const QString& executable, std::stop_token stop,
    const QStringList& arguments = {QStringLiteral("--pf-backend-probe")}, int timeoutMs = 120000,
    const QProcessEnvironment& environment = QProcessEnvironment::systemEnvironment());
pfgpu::BackendProbe probeAutomaticBackends(const QString& executable, std::stop_token stop,
    const QStringList& runtimes, const QProcessEnvironment& environment);
}
