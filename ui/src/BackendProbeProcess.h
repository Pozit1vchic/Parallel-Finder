#pragma once

#include <pfgpu/DeviceInfo.hpp>
#include <QByteArray>
#include <QStringList>
#include <stop_token>

namespace pfui {
QByteArray backendProbeJson(const pfgpu::BackendProbe& probe);
pfgpu::BackendProbe backendProbeFromJson(const QByteArray& json);
pfgpu::BackendProbe probeBackendProcess(const QString& executable, std::stop_token stop,
    const QStringList& arguments = {QStringLiteral("--pf-backend-probe")}, int timeoutMs = 120000);
}
