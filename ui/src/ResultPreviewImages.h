#pragma once

#include <QImage>
#include <QString>
#include <pfcore/VideoDecoder.hpp>
#include <memory>
#include <vector>

class QQmlEngine;
namespace pfservices { class PfCache; }

namespace pfui {
// One immutable request table belongs to one published result set. The image
// provider holds weak references; closing results never retains videos/cache.
class ResultPreviewImages final {
public:
    struct Request {
        std::string source,key,generation;
        double seconds=0;
        QString fallbackUrl={};
    };
    static std::shared_ptr<ResultPreviewImages> create(std::vector<Request> requests,
        std::shared_ptr<pfservices::PfCache> cache, pfcore::VideoDecodeOptions options);
    ~ResultPreviewImages();
    QString url(std::size_t index) const;
    struct Statistics { std::size_t requests=0,cacheHits=0,cacheMisses=0,decodedFrames=0,cacheWrites=0; };
    Statistics statistics() const noexcept;
    void close() noexcept;
    static QImage resolve(const QString& id);
private:
    struct Impl;
    explicit ResultPreviewImages(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Qt owns the provider; requests run on its image-loading thread, with at most
// one decoder/image operation per session and no retained decoded image pool.
void installResultPreviewImages(QQmlEngine& engine);
}
