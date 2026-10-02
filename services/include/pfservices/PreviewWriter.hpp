#pragma once
#include "pfservices/PfCache.hpp"
#include <QImage>
#include <QString>
#include <future>
#include <memory>

namespace pfservices {
// One bounded encoder overlaps PNG/cache writes with the caller's next seek
// and decode. Only owned QImages cross threads; decoder access stays serial.
// Every returned future is ready before finish()/destruction returns.
class PreviewWriter {
public:
    explicit PreviewWriter(PfCache* cache, std::size_t capacity = 3);
    ~PreviewWriter();
    PreviewWriter(const PreviewWriter&) = delete;
    PreviewWriter& operator=(const PreviewWriter&) = delete;
    std::shared_future<QString> submit(QImage image, QString path, std::string cacheKey);
    void finish() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pfservices
