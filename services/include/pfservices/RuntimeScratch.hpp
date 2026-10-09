#pragma once
#include <QString>
#include <QLockFile>
#include <QTemporaryDir>
#include <memory>

namespace pfservices {
// Only owns generated preview sessions, never source material or model caches.
class RuntimeScratch {
public:
    explicit RuntimeScratch(const QString& root, int retentionSeconds = 86400);
    ~RuntimeScratch();
    [[nodiscard]] QString path() const;
private:
    std::unique_ptr<QTemporaryDir> directory_;
    std::unique_ptr<QLockFile> owner_; // released before directory removal
};
}
