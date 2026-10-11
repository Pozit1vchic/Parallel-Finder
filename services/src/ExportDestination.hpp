#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <filesystem>
#include <stdexcept>

namespace pfservices::detail {
// Reserve a free name atomically. The empty reservation belongs to this
// export and is removed on failure; existing user files are never replaced.
class ExportDestination {
public:
    explicit ExportDestination(const std::filesystem::path& requested) {
        const QFileInfo info(QString::fromStdWString(requested.wstring()));
        if (!QDir().mkpath(info.absolutePath())) throw std::runtime_error("cannot create export folder");
        for (unsigned number = 0; ; ++number) {
            const QString suffix = number ? QString(" (%1)").arg(number) : QString{};
            const auto extension = info.suffix().isEmpty() ? QString{} : "." + info.suffix();
            name_ = info.dir().filePath(info.completeBaseName() + suffix + extension);
            QFile reservation(name_);
            if (reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return;
            if (!QFileInfo::exists(name_)) throw std::runtime_error(reservation.errorString().toStdString());
            if (number == 1000000) throw std::runtime_error("too many occupied export names");
        }
    }
    ExportDestination(const ExportDestination&) = delete;
    ExportDestination& operator=(const ExportDestination&) = delete;
    ~ExportDestination() { if (!committed_) QFile::remove(name_); }
    std::filesystem::path path() const { return std::filesystem::path(name_.toStdWString()); }
    void commit() { committed_ = true; }
private:
    QString name_;
    bool committed_ = false;
};
}
