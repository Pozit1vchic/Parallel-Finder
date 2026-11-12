#include "ResultPreviewImages.h"
#include <pfservices/PfCache.hpp>
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QUuid>
#include <QUrl>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

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
class Provider final:public QQuickImageProvider {
public:
    Provider():QQuickImageProvider(Image,ForceAsynchronousImageLoading){}
    QImage requestImage(const QString& id,QSize* size,const QSize& requested) override {
        auto image=ResultPreviewImages::resolve(id);
        if(size)*size=image.size();
        if(!image.isNull() && requested.width()>0 && requested.height()>0)
            image=image.scaled(requested,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        return image;
    }
};
}
struct ResultPreviewImages::Impl {
    struct SourceState { qint64 size;QDateTime modified; };
    QString token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::vector<Request> requests;
    std::map<std::string,SourceState> sources;
    std::shared_ptr<pfservices::PfCache> cache;
    pfcore::VideoDecodeOptions options;
    std::atomic_bool closed=false;
    std::atomic_size_t requested=0,hits=0,misses=0,decoded=0,writes=0;
    std::mutex mutex;
    bool compatible(const Request& request) const {
        const QFileInfo source(QString::fromStdString(request.source));
        const auto& expected=sources.at(request.source);
        return source.isFile() && source.size()==expected.size && source.lastModified()==expected.modified
            && (!cache || cache->sourceGeneration(request.source)==request.generation);
    }
    QImage load(std::size_t index) {
        std::lock_guard lock(mutex);
        if(closed || index>=requests.size())return {};
        const auto& request=requests[index];
        if(!std::isfinite(request.seconds) || request.seconds<0)return {};
        // A manual reset must not resurrect an earlier result set's records.
        if(!compatible(request))return {};
        ++requested;
        QImage image;
        if(cache)if(const auto bytes=cache->get(request.key))image=readPreview(*bytes);
        if(!image.isNull())++hits;else ++misses;
        if(image.isNull() && !closed)try {
            pfcore::VideoDecoder decoder;decoder.open(request.source,options);
            decoder.setRgbaMaxDimensions(1920,1080);decoder.seek(request.seconds);
            pfcore::DecodedFrame frame;
            while(!closed && decoder.readNext(frame,false)) {
                ++decoded;
                if(frame.timestampSeconds+1e-3<request.seconds)continue;
                if(!decoder.convertCurrentFrameToRgba(frame))break;
                image=QImage(frame.rgba.data(),frame.width,frame.height,QImage::Format_RGBA8888).copy();
                if(image.width()>1920 || image.height()>1080)
                    image=image.scaled(1920,1080,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                break;
            }
            if(cache && !closed && !image.isNull()) {
                QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);
                QImageWriter writer(&output,"png");writer.setCompression(12);
                if(writer.write(image) && !closed && compatible(request)) {
                    std::string error;
                    if(cache->putForSource(request.source,request.key,{bytes.begin(),bytes.end()},error))++writes;
                }
            }
        }catch(...) { /* Retain the existing window-preview fallback. */ }
        if(image.isNull() && !closed && !request.fallbackUrl.isEmpty()) {
            const QUrl url(request.fallbackUrl);
            if(url.isLocalFile()) {
                QImageReader reader(url.toLocalFile(),"png");
                const auto size=reader.size();
                if(!size.isEmpty() && size.width()<=1920 && size.height()<=1080)image=reader.read();
            }
        }
        return closed || !compatible(request)?QImage{}:image;
    }
};
ResultPreviewImages::ResultPreviewImages(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::shared_ptr<ResultPreviewImages> ResultPreviewImages::create(std::vector<Request> requests,
    std::shared_ptr<pfservices::PfCache> cache,pfcore::VideoDecodeOptions options) {
    auto impl=std::make_unique<Impl>();impl->requests=std::move(requests);
    impl->cache=std::move(cache);impl->options=options;
    for(const auto& request:impl->requests) {
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
void ResultPreviewImages::close() noexcept { impl_->closed=true; }
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
void installResultPreviewImages(QQmlEngine& engine) {
    if(!engine.imageProvider(QLatin1String(providerName)))engine.addImageProvider(QLatin1String(providerName),new Provider);
}
}
