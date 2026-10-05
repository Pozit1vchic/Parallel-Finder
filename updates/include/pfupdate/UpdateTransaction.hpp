#pragma once
#include <pfupdate/UpdateManifest.hpp>
#include <functional>

namespace pfupdate {
struct InstallHooks {
    std::function<bool(const QString& executable,QString& error)> healthCheck;
    std::function<bool(const QString& executable,QString& error)> restart;
    std::function<bool(QString& error)> beforeActivate;
};
class UpdateTransaction {
public:
    explicit UpdateTransaction(QString installationRoot);
    bool install(const QString& package,const Manifest& manifest,const InstallHooks& hooks,QString& error);
    bool recover(QString& error);
    QString journalPath() const;
private:
    QString root_, stage_, backup_;
    bool journal(const QString& phase,QString& error);
    bool rollback(QString& error);
};
}
