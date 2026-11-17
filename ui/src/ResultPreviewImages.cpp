#include "ResultPreviewImages.h"
#include <pfservices/PfCache.hpp>
#include <QBuffer>
#include <QCache>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QThreadPool>
#include <QUuid>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

namespace pfui {
namespace {
constexpr auto providerName="pfresults";
struct Registry {
    std::mutex mutex;
    std::map<QString,std::weak_ptr<ResultPreviewImages>> sessions;
};
Registry& registry() { static auto* value=new Registry;return *value; }
QImage readPreview(const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()>32U*1024U*1024U)return {};
    QByteArray encoded(reinterpret_cast<const char*>(bytes.data()),static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&encoded);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"png");
    const auto size=reader.size();
    if(size.isEmpty() || size.width()>1920 || size.height()>1080)return {};
    return reader.read();
}
QThreadPool& imageWorkers() {
    static QThreadPool pool;
    static const auto initialized = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(initialized);
    return pool;
}
QThreadPool& prefetchWorkers() {
    static QThreadPool pool;
    static const auto initialized = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(initialized);
    return pool;
}
class PreviewResponse final : public QQuickImageResponse {
public:
    PreviewResponse(const QString& id, const QSize& requested) {
        imageWorkers().start([this, id, requested] {
            QImage image;
            if (!cancelled_) image = ResultPreviewImages::resolve(id);
            if (!image.isNull() && (requested.width() > 0 || requested.height() > 0)) {
                const QSize bounds(requested.width() > 0 ? requested.width() : image.width(),
                                   requested.height() > 0 ? requested.height() : image.height());
                if (image.width() > bounds.width() || image.height() > bounds.height())
                    image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            QMetaObject::invokeMethod(this, [this, image = std::move(image)] {
                image_ = image;
                emit finished();
            }, Qt::QueuedConnection);
        });
    }
    void cancel() override { cancelled_ = true; }
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return image_.isNull() ? QStringLiteral("Preview unavailable") : QString{}; }
private:
    std::atomic_bool cancelled_ = false;
    QImage image_;
};
class Provider final:public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requested) override {
        return new PreviewResponse(id, requested);
    }
};
}
struct ResultPreviewImages::Impl {
    struct SourceState { qint64 size;QDateTime modified; };
    struct DecoderState { pfcore::VideoDecoder decoder; std::string source; };
    using CacheEntries = std::vector<std::pair<std::string,std::vector<std::uint8_t>>>;
    using CacheBatch = std::map<std::string,CacheEntries>;
    QString token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::vector<Request> requests;
    std::map<std::string,SourceState> sources;
    std::shared_ptr<pfservices::PfCache> cache;
    pfcore::VideoDecodeOptions options;
    std::atomic_bool closed=false;
    std::atomic_size_t requested=0,hits=0,misses=0,decoded=0,writes=0;
    std::unique_ptr<std::mutex[]> requestLocks;
    std::mutex memoryMutex;
    QCache<std::size_t, QImage> memory{256 * 1024 * 1024};
    std::mutex queueMutex;
    std::deque<std::size_t> pending;
    int workers = 0;
    bool compatible(const Request& request) const {
        const QFileInfo source(QString::fromStdString(request.source));
        const auto& expected=sources.at(request.source);
        return source.isFile() && source.size()==expected.size && source.lastModified()==expected.modified
            && (!cache || cache->sourceGeneration(request.source)==request.generation);
    }
    void flush(CacheBatch& batch) {
        if (!closed && cache) for (const auto& [source, entries] : batch) {
            const auto request = std::find_if(requests.begin(), requests.end(), [&](const auto& row) { return row.source == source; });
            if (request == requests.end() || !compatible(*request)) continue;
            std::string error;
            if (cache->putForSourceBatch(source, entries, error)) writes += entries.size();
        }
        batch.clear();
    }
    QImage load(std::size_t index, DecoderState* reusable = nullptr, CacheBatch* batch = nullptr) {
        if(closed || index>=requests.size())return {};
        const auto& request=requests[index];
        if(!std::isfinite(request.seconds) || request.seconds<0)return {};
        std::unique_lock lock(requestLocks[index]);
        // A manual reset must not resurrect an earlier result set's records.
        if(!compatible(request))return {};
        ++requested;
        {
            std::lock_guard memoryLock(memoryMutex);
            if (const auto* ready = memory.object(index)) { ++hits; return *ready; }
        }
        QImage image;
        bool decodedImage = false;
        if(cache)if(const auto bytes=cache->get(request.key))image=readPreview(*bytes);
        if(!image.isNull())++hits;else ++misses;
        if(image.isNull() && !closed)try {
            DecoderState scratch;
            auto& state = reusable ? *reusable : scratch;
            auto& decoder = state.decoder;
            if (!decoder.isOpen() || state.source != request.source) {
                state.source.clear();
                decoder.open(request.source,options);
                state.source = request.source;
                decoder.setRgbaMaxDimensions(options.maxWidth > 0 ? options.maxWidth : 1920,
                                             options.maxHeight > 0 ? options.maxHeight : 1080);
            }
            decoder.seek(request.seconds);
            pfcore::DecodedFrame frame;
            while(!closed && decoder.readNext(frame,false)) {
                ++decoded;
                if(frame.timestampSeconds+1e-3<request.seconds)continue;
                if(!decoder.convertCurrentFrameToRgba(frame))break;
                image=QImage(frame.rgba.data(),frame.width,frame.height,QImage::Format_RGBA8888).copy();
                if(image.width()>1920 || image.height()>1080)
                    image=image.scaled(1920,1080,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                decodedImage = !image.isNull();
                break;
            }
        }catch(...) {
            if (reusable) reusable->decoder.close();
            // Retain the existing window-preview fallback.
        }
        if(image.isNull() && !closed && !request.fallbackUrl.isEmpty()) {
            const QUrl url(request.fallbackUrl);
            if(url.isLocalFile()) {
                QImageReader reader(url.toLocalFile(),"png");
                const auto size=reader.size();
                if(!size.isEmpty() && size.width()<=1920 && size.height()<=1080)image=reader.read();
            }
        }
        if (closed || !compatible(request)) return {};
        if (!image.isNull()) {
            std::lock_guard memoryLock(memoryMutex);
            if (!closed) memory.insert(index, new QImage(image), image.sizeInBytes());
        }
        lock.unlock();
        if (cache && !request.key.empty() && decodedImage && !closed) {
            QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
            QImageWriter writer(&output,"png"); writer.setCompression(12);
            if (writer.write(image) && !closed && compatible(request)) {
                if (batch) (*batch)[request.source].emplace_back(request.key, std::vector<std::uint8_t>{bytes.begin(),bytes.end()});
                else {
                    std::string error;
                    if (cache->putForSource(request.source, request.key, {bytes.begin(),bytes.end()}, error)) ++writes;
                }
            }
        }
        return image;
    }
};
ResultPreviewImages::ResultPreviewImages(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::shared_ptr<ResultPreviewImages> ResultPreviewImages::create(std::vector<Request> requests,
    std::shared_ptr<pfservices::PfCache> cache,pfcore::VideoDecodeOptions options) {
    auto impl=std::make_unique<Impl>();impl->requests=std::move(requests);
    impl->requestLocks=std::make_unique<std::mutex[]>(impl->requests.size());
    impl->cache=std::move(cache);impl->options=options;
    if (options.maxWidth > 0 && options.maxWidth <= 480) impl->memory.setMaxCost(32 * 1024 * 1024);
    for(const auto& request:impl->requests) {
        if (impl->sources.contains(request.source)) continue;
        const QFileInfo file(QString::fromStdString(request.source));
        impl->sources.try_emplace(request.source,Impl::SourceState{file.size(),file.lastModified()});
    }
    auto session=std::shared_ptr<ResultPreviewImages>(new ResultPreviewImages(std::move(impl)));
    auto& entries=registry();std::lock_guard lock(entries.mutex);
    entries.sessions.emplace(session->impl_->token,session);
    return session;
}
ResultPreviewImages::~ResultPreviewImages() {
    close();auto& entries=registry();std::lock_guard lock(entries.mutex);
    entries.sessions.erase(impl_->token);
}
void ResultPreviewImages::close() noexcept {
    impl_->closed=true;
    std::lock_guard lock(impl_->memoryMutex);
    impl_->memory.clear();
}
ResultPreviewImages::Statistics ResultPreviewImages::statistics() const noexcept {
    return {impl_->requested.load(),impl_->hits.load(),impl_->misses.load(),impl_->decoded.load(),impl_->writes.load()};
}
QString ResultPreviewImages::url(std::size_t index) const {
    if(impl_->closed || index>=impl_->requests.size())return {};
    return QStringLiteral("image://pfresults/")+impl_->token+QLatin1Char('/')+QString::number(index);
}
QImage ResultPreviewImages::resolve(const QString& id) {
    const auto separator=id.indexOf('/');
    if(separator<=0)return {};
    const auto indexString=id.mid(separator+1);
    bool valid=false;const auto index=indexString.toULongLong(&valid);
    if(!valid || QString::number(index)!=indexString)return {};
    std::shared_ptr<ResultPreviewImages> session;
    {auto& entries=registry();std::lock_guard lock(entries.mutex);
        const auto found=entries.sessions.find(id.left(separator));
        if(found!=entries.sessions.end())session=found->second.lock();}
    try {return session?session->impl_->load(index):QImage{};}
    catch(...) {return {};}
}
void ResultPreviewImages::prefetch(const QStringList& urls) {
    std::map<QString, std::deque<std::size_t>> bySession;
    const QString prefix = QStringLiteral("image://pfresults/");
    for (const auto& url : urls) {
        if (!url.startsWith(prefix)) continue;
        const auto split = url.indexOf('/', prefix.size());
        if (split < 0) continue;
        bool valid = false;
        const auto index = url.mid(split + 1).toULongLong(&valid);
        if (!valid) continue;
        auto& indices = bySession[url.mid(prefix.size(), split - prefix.size())];
        if (indices.size() < 32 && std::find(indices.begin(), indices.end(), index) == indices.end())
            indices.push_back(index);
    }
    for (auto& [token, indices] : bySession) {
        std::shared_ptr<ResultPreviewImages> session;
        {
            auto& entries = registry(); std::lock_guard lock(entries.mutex);
            if (const auto entry = entries.sessions.find(token); entry != entries.sessions.end()) session = entry->second.lock();
        }
        if (!session || session->impl_->closed) continue;
        auto& impl = *session->impl_;
        std::lock_guard lock(impl.queueMutex);
        if (impl.pending == indices) continue;
        impl.pending = std::move(indices);
        while (impl.workers < 2) {
            ++impl.workers;
            prefetchWorkers().start([weak = std::weak_ptr(session)] {
                const auto owner = weak.lock();
                if (!owner) return;
                auto& state = *owner->impl_;
                Impl::DecoderState decoder;
                Impl::CacheBatch batch;
                std::size_t batchCount = 0;
                for (;;) {
                    std::optional<std::size_t> index;
                    {
                        std::lock_guard queueLock(state.queueMutex);
                        if (state.closed || state.pending.empty()) --state.workers;
                        else { index = state.pending.front(); state.pending.pop_front(); }
                    }
                    try {
                        if (!index) { state.flush(batch); return; }
                        state.load(*index, &decoder, &batch);
                        if (++batchCount == 8) { state.flush(batch); batchCount = 0; }
                    } catch (...) {
                        if (!index) return;
                        // A later foreground request can retry.
                    }
                }
            });
        }
    }
}
void installResultPreviewImages(QQmlEngine& engine) {
    if(!engine.imageProvider(QLatin1String(providerName)))engine.addImageProvider(QLatin1String(providerName),new Provider);
}
}
