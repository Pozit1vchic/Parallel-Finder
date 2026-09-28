#pragma once
#include <QString>
#include <QStringList>
#include <stop_token>
#include <vector>

namespace pfservices {
struct ProviderInstallation {
    QString name;
    QString runtimePath;
    QString reason;
    bool installed = false;
    bool available = false;
};
// Probes each installation in an isolated process. Never swaps an ORT DLL
// under live sessions in the caller. Run scan on a worker, not the GUI thread.
class ProviderManager {
public:
    static QString locateRuntime(const QStringList& roots, const QString& provider);
    static std::vector<ProviderInstallation> scan(const QStringList& roots,
        const QString& probeExecutable, std::stop_token stop = {});
};
}
