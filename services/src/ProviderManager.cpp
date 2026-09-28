#include <pfservices/ProviderManager.hpp>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace pfservices {
namespace {
QString runtimeIn(const QString& root)
{
    if (!QFileInfo(root).isDir() || QFileInfo(root).isSymLink()) return {};
    const auto direct = QDir(root).filePath("onnxruntime.dll");
    if (QFileInfo(direct).isFile() && !QFileInfo(direct).isSymLink()) return direct;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    for (int visited = 0; visited < 256 && it.hasNext(); ++visited) {
        const auto path = it.next();
        if (it.fileName().compare("onnxruntime.dll", Qt::CaseInsensitive) == 0) return path;
    }
    return {};
}
}

QString ProviderManager::locateRuntime(const QStringList& roots, const QString& provider)
{
    if (provider != "cuda" && provider != "dml" && provider != "tensorrt") return {};
    for (const auto& root : roots) {
        const auto runtime = runtimeIn(QDir(root).filePath(provider));
        if (!runtime.isEmpty()) return runtime;
    }
    return {};
}

std::vector<ProviderInstallation> ProviderManager::scan(const QStringList& roots,
    const QString& executable, std::stop_token stop)
{
    std::vector<ProviderInstallation> states;
    for (const auto* name : {"dml", "cuda", "tensorrt"}) {
        if (stop.stop_requested()) break;
        ProviderInstallation state;
        state.name = QString::fromLatin1(name);
        state.runtimePath = locateRuntime(roots, state.name);
        state.installed = !state.runtimePath.isEmpty();
        if (!state.installed) {
            state.reason = "Runtime not installed";
            states.push_back(std::move(state));
            continue;
        }
        auto environment = QProcessEnvironment::systemEnvironment();
        const auto directory = QFileInfo(state.runtimePath).absolutePath();
        environment.insert("PF_ORT_DLL", state.runtimePath);
        environment.insert("PF_PROVIDER_ROOT", directory);
        environment.insert("PATH", QDir::toNativeSeparators(directory) + QDir::listSeparator() + environment.value("PATH"));
        // --pf-provider-probe uses QCoreApplication and requires no platform plugin.
        QProcess process;
        process.setProcessEnvironment(environment);
#if defined(_WIN32)
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        process.start(executable, {"--pf-provider-probe", state.name});
        if (!process.waitForStarted(5000)) state.reason = process.errorString();
        else {
            QElapsedTimer timer;
            timer.start();
            while (process.state() != QProcess::NotRunning && !stop.stop_requested() && timer.elapsed() < 120000)
                process.waitForFinished(100);
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(5000);
                state.reason = stop.stop_requested() ? "Probe cancelled" : "Probe timed out";
            } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                state.reason = "Probe process failed: " + QString::fromUtf8(process.readAllStandardError()).right(2048);
            } else {
                const auto output = process.readAllStandardOutput();
                const QByteArray marker("PF_PROVIDER_JSON=");
                const auto position = output.lastIndexOf(marker);
                const auto payload = position >= 0 ? output.mid(position + marker.size()).split('\n').front() : QByteArray();
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(payload, &error);
                const auto object = document.object();
                if (error.error != QJsonParseError::NoError || object.value("provider").toString() != state.name)
                    state.reason = "Invalid probe response";
                else {
                    state.available = object.value("available").toBool(false);
                    state.reason = object.value("reason").toString();
                    if (!state.available && state.reason.isEmpty()) state.reason = "Provider unavailable";
                }
            }
        }
        states.push_back(std::move(state));
    }
    return states;
}
}
