#include "BackendProbeProcess.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <algorithm>
#include <set>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pfui {
QByteArray backendProbeJson(const pfgpu::BackendProbe& probe)
{
    QJsonArray backends;
    for (const auto& backend : probe.backends) backends.append(QJsonObject{
        {"provider", QString::fromLatin1(pfgpu::providerName(backend.provider))},
        {"available", backend.available}, {"reason", QString::fromStdString(backend.reason)},
        {"device", QString::fromStdString(backend.deviceName)}});
    return QJsonDocument(QJsonObject{{"schema", 1}, {"loaded", probe.ortLoaded},
        {"version", QString::fromStdString(probe.ortVersion)}, {"api", static_cast<int>(probe.ortApiVersion)},
        {"error", QString::fromStdString(probe.ortError)}, {"backends", backends}}).toJson(QJsonDocument::Compact);
}

pfgpu::BackendProbe backendProbeFromJson(const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || object.value("schema").toInt() != 1
        || !object.value("loaded").isBool() || !object.value("backends").isArray())
        throw std::runtime_error("Invalid backend probe response");
    pfgpu::BackendProbe probe;
    probe.ortLoaded = object.value("loaded").toBool();
    probe.ortVersion = object.value("version").toString().toStdString();
    probe.ortApiVersion = static_cast<std::uint32_t>(std::max(0, object.value("api").toInt()));
    probe.ortError = object.value("error").toString().toStdString();
    std::set<pfgpu::Provider> seen;
    for (const auto& value : object.value("backends").toArray()) {
        const auto entry = value.toObject();
        const auto provider = pfgpu::parseProvider(entry.value("provider").toString().toStdString());
        if (!provider || *provider == pfgpu::Provider::Auto || !seen.insert(*provider).second
            || !entry.value("available").isBool()) throw std::runtime_error("Invalid backend probe status");
        probe.backends.push_back({*provider, entry.value("available").toBool(),
            entry.value("reason").toString().toStdString(), entry.value("device").toString().toStdString()});
    }
    if (!seen.contains(pfgpu::Provider::Cpu)) throw std::runtime_error("Backend probe omitted CPU status");
    return probe;
}

pfgpu::BackendProbe probeBackendProcess(const QString& executable, std::stop_token stop,
    const QStringList& arguments, int timeoutMs)
{
    if (stop.stop_requested()) throw std::runtime_error("Backend probe cancelled");
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
#endif
    process.start(executable, arguments);
    if (!process.waitForStarted(5000)) throw std::runtime_error(process.errorString().toStdString());
    QElapsedTimer elapsed; elapsed.start();
    while (process.state() != QProcess::NotRunning && !stop.stop_requested() && elapsed.elapsed() < timeoutMs)
        process.waitForFinished(50);
    if (process.state() != QProcess::NotRunning) {
        process.kill(); process.waitForFinished(5000);
        throw std::runtime_error(stop.stop_requested() ? "Backend probe cancelled" : "Backend probe timed out");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error("Backend probe process failed");
    const auto output = process.readAllStandardOutput();
    const QByteArray marker("PF_BACKENDS_JSON=");
    const auto index = output.lastIndexOf(marker);
    if (index < 0) throw std::runtime_error("Backend probe returned no status");
    return backendProbeFromJson(output.mid(index + marker.size()).split('\n').first().trimmed());
}
}
