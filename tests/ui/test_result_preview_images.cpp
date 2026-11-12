#include <ResultPreviewImages.h>
#include <pfservices/PfCache.hpp>
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QBuffer>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QUrl>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <cmath>
#include <cstring>

class ResultPreviewTests:public QObject {
    Q_OBJECT
    static QString id(const QString& url) {return QUrl(url).path().mid(1);}
    static std::vector<std::uint8_t> png(const QImage& image) {
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
        return {bytes.begin(),bytes.end()};
    }
    static QByteArray pixelHash(QImage image) {
        image=image.convertToFormat(QImage::Format_RGBA8888);
        return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256);
    }
private slots:
    void decodeFailureKeepsTheWindowPreviewWithoutCachingItAsAnExactFrame() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto source=directory.path()+"/invalid-video";
        QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write("not video");file.close();
        QImage expected(60,40,QImage::Format_RGBA8888);expected.fill(Qt::blue);
        const auto fallback=directory.path()+"/window.png";QVERIFY(expected.save(fallback,"PNG"));
        auto cache=std::make_shared<pfservices::PfCache>(std::filesystem::path(directory.path().toStdWString())/"cache",65536);
        auto session=pfui::ResultPreviewImages::create({{source.toStdString(),"preview-v1|exact","",.2,QUrl::fromLocalFile(fallback).toString()}},cache,{});
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(session->url(0))).convertToFormat(expected.format()),expected);
        QVERIFY(!cache->get("preview-v1|exact"));QCOMPARE(session->statistics().cacheWrites,0U);
    }
    void realCachedEpisodePreviewsPreserveEveryPixel() {
        const auto reference=qEnvironmentVariable("PF_PREVIEW_NATIVE_REFERENCE");
        const auto cacheRoot=qEnvironmentVariable("PF_PREVIEW_NATIVE_CACHE");
        if(reference.isEmpty() || cacheRoot.isEmpty())QSKIP("Opt-in actual episode preview cache control");
        QFile report(reference);QVERIFY(report.open(QIODevice::ReadOnly));
        const auto referenceBytes=report.readAll();
        const auto rows=QJsonDocument::fromJson(referenceBytes).object().value("results").toArray();
        QVERIFY(!rows.empty());
        auto cache=std::make_shared<pfservices::PfCache>(cacheRoot.toStdWString(),20ULL*1024*1024*1024);
        std::vector<pfui::ResultPreviewImages::Request> requests;
        std::vector<std::pair<QSize,QByteArray>> expected;QSet<QString> keys;
        for(const auto& value:rows)for(const auto& side:{QStringLiteral("left"),QStringLiteral("right")}) {
            const auto row=value.toObject();
            const auto stem=QFileInfo(QUrl(row.value(side+"Preview").toString()).toLocalFile()).completeBaseName();
            if(keys.contains(stem))continue;
            keys.insert(stem);
            QFile record(cacheRoot+'/'+stem+".pfc");QVERIFY(record.open(QIODevice::ReadOnly));
            const auto header=record.read(24);QCOMPARE(header.size(),24);
            QCOMPARE(header.left(8),QByteArray("PFCACHE1"));
            std::uint32_t length=0;std::memcpy(&length,header.constData()+12,4);
            QVERIFY(length>0 && length<=16384);
            const auto key=record.read(length);QCOMPARE(key.size(),length);
            const auto bytes=cache->get(key.toStdString());QVERIFY(bytes);
            const auto source=row.value(side+"Source").toString().toStdString();
            const double seconds=row.value(side+"Start").toDouble();
            QVERIFY(key.endsWith("|t="+QByteArray::number(std::llround(seconds*1000000.))));
            requests.push_back({source,key.toStdString(),cache->sourceGeneration(source),seconds});
            const auto image=QImage::fromData(bytes->data(),static_cast<int>(bytes->size()),"PNG");
            QVERIFY(!image.isNull());expected.emplace_back(image.size(),pixelHash(image));
        }
        auto session=pfui::ResultPreviewImages::create(std::move(requests),cache,{});
        QCOMPARE(session->statistics().requests,0U);
        for(std::size_t i=0;i<expected.size();++i) {
            const auto image=pfui::ResultPreviewImages::resolve(id(session->url(i)));
            QCOMPARE(image.size(),expected[i].first);QCOMPARE(pixelHash(image),expected[i].second);
        }
        const auto stats=session->statistics();
        QCOMPARE(stats.requests,expected.size());QCOMPARE(stats.cacheHits,expected.size());
        QCOMPARE(stats.cacheMisses,0U);QCOMPARE(stats.decodedFrames,0U);QCOMPARE(stats.cacheWrites,0U);
        // Force actual source-video fallback once per source, using an empty
        // cache pointer. This does not alter or erase the frozen cached images.
        QSet<QString> decodedSources;std::size_t fallbackFrames=0;
        for(const auto& value:rows)for(const auto& side:{QStringLiteral("left"),QStringLiteral("right")}) {
            const auto row=value.toObject();const auto source=row.value(side+"Source").toString();
            if(decodedSources.contains(source))continue;
            decodedSources.insert(source);
            const auto stem=QFileInfo(QUrl(row.value(side+"Preview").toString()).toLocalFile()).completeBaseName();
            QFile record(cacheRoot+'/'+stem+".pfc");QVERIFY(record.open(QIODevice::ReadOnly));
            const auto header=record.read(24);std::uint32_t length=0;
            QCOMPARE(header.size(),24);std::memcpy(&length,header.constData()+12,4);QVERIFY(length<=16384);
            const auto bytes=cache->get(record.read(length).toStdString());QVERIFY(bytes);
            const auto oldImage=QImage::fromData(bytes->data(),static_cast<int>(bytes->size()),"PNG");
            pfcore::VideoDecodeOptions options;options.threads=8;options.maxWidth=1920;options.maxHeight=1080;
            auto fallback=pfui::ResultPreviewImages::create({{source.toStdString(),"","",row.value(side+"Start").toDouble()}},nullptr,options);
            const auto actual=pfui::ResultPreviewImages::resolve(id(fallback->url(0)));
            QCOMPARE(actual.size(),oldImage.size());QCOMPARE(pixelHash(actual),pixelHash(oldImage));
            QCOMPARE(fallback->statistics().cacheHits,0U);QVERIFY(fallback->statistics().decodedFrames>0);
            fallbackFrames+=fallback->statistics().decodedFrames;
        }
        const auto output=qEnvironmentVariable("PF_PREVIEW_NATIVE_REPORT");
        if(!output.isEmpty()) {
            QFile result(output);QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            result.write(QJsonDocument(QJsonObject{{"scope","All cached previews from the frozen native episode report; pixel equality, not pipeline timing"},
                {"pairs",rows.size()},{"uniquePreviews",static_cast<qint64>(expected.size())},
                {"referenceSha256",QString::fromLatin1(QCryptographicHash::hash(referenceBytes,QCryptographicHash::Sha256).toHex())},
                {"allPixelsEqual",true},{"cachedLoadDecodedFrames",0},{"cacheWrites",0},
                {"fallbackSources",decodedSources.size()},{"fallbackDecodedFrames",static_cast<qint64>(fallbackFrames)},
                {"fallbackPixelsEqual",true}}).toJson());
        }
    }
    void cachedPixelsAreLoadedOnDemandAndExpireWithTheResults() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto source=directory.path()+"/not-a-video";
        QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write("source");file.close();
        auto cache=std::make_shared<pfservices::PfCache>(directory.path().toStdWString()/std::filesystem::path("cache"),65536);
        const std::string key="preview-v1|fixture";std::string error;
        QImage expected(96,54,QImage::Format_RGBA8888);expected.fill(QColor(12,45,78,255));
        auto session=pfui::ResultPreviewImages::create({{source.toStdString(),key,"",0}},cache,{});
        const auto url=session->url(0);QVERIFY(url.startsWith("image://pfresults/"));
        // Insert AFTER registration: no eager read or retained encoded payload.
        QVERIFY(cache->put(key,png(expected),error));
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(url)).convertToFormat(expected.format()),expected);
        QVERIFY(pfui::ResultPreviewImages::resolve(id(url)+"/extra").isNull());
        QVERIFY(pfui::ResultPreviewImages::resolve(id(url).section('/',0,0)+"/01").isNull());
        QVERIFY(session->url(1).isEmpty());
        session->close();QVERIFY(pfui::ResultPreviewImages::resolve(id(url)).isNull());
        const std::weak_ptr<pfui::ResultPreviewImages> weak=session;
        session.reset();QVERIFY(pfui::ResultPreviewImages::resolve(id(url)).isNull());
        QVERIFY(weak.expired());
        QCOMPARE(*cache->get(key),png(expected));
    }
    void sourceChangesAndResetsCannotResurrectCachedImages() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto source=directory.path()+"/source";
        QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write("original");file.close();
        auto cache=std::make_shared<pfservices::PfCache>(std::filesystem::path(directory.path().toStdWString())/"cache",65536);
        QImage image(40,30,QImage::Format_RGBA8888);image.fill(Qt::red);
        std::string error;const std::string key="preview-v1|reset";
        QVERIFY(cache->putForSource(source.toStdString(),key,png(image),error));
        auto session=pfui::ResultPreviewImages::create({{source.toStdString(),key,"",0}},cache,{});
        QVERIFY(!pfui::ResultPreviewImages::resolve(id(session->url(0))).isNull());
        QVERIFY(cache->resetSource(source.toStdString(),error));
        QVERIFY(cache->put(key,png(image),error));
        QVERIFY(pfui::ResultPreviewImages::resolve(id(session->url(0))).isNull());
        auto fresh=pfui::ResultPreviewImages::create({{source.toStdString(),key,cache->sourceGeneration(source.toStdString()),0}},cache,{});
        QVERIFY(!pfui::ResultPreviewImages::resolve(id(fresh->url(0))).isNull());
        QVERIFY(file.open(QIODevice::Append));file.write("changed");file.close();
        QVERIFY(pfui::ResultPreviewImages::resolve(id(fresh->url(0))).isNull());
    }
    void missingAndCorruptEntriesDecodeTheSameFrameWithoutTemporaryPngs() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const std::string source=PF_PREVIEW_TEST_VIDEO,key="preview-v1|decode";
        auto cache=std::make_shared<pfservices::PfCache>(directory.path().toStdWString(),65536);
        auto session=pfui::ResultPreviewImages::create({{source,key,"",.2}},cache,{});
        QVERIFY(!cache->get(key));
        pfcore::VideoDecoder decoder;decoder.open(source);decoder.setRgbaMaxDimensions(1920,1080);decoder.seek(.2);
        pfcore::DecodedFrame frame;
        while(decoder.readNext(frame,false))if(frame.timestampSeconds+1e-3>=.2)break;
        QVERIFY(decoder.convertCurrentFrameToRgba(frame));
        const QImage expected=QImage(frame.rgba.data(),frame.width,frame.height,QImage::Format_RGBA8888).copy();
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(session->url(0))),expected);
        QVERIFY(cache->get(key));
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(session->url(0))).convertToFormat(expected.format()),expected);
        std::string error;QVERIFY(cache->put(key,{0,1,2},error));
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(session->url(0))),expected);
        QCOMPARE(QDir(directory.path()).entryList({"*.png"},QDir::Files).size(),0);
        auto disabled=pfui::ResultPreviewImages::create({{source,key,"",.2}},nullptr,{});
        QCOMPARE(pfui::ResultPreviewImages::resolve(id(disabled->url(0))),expected);
    }
    void qmlImageLoadsThroughTheAsynchronousProvider() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto source=directory.path()+"/source";QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("source");file.close();
        auto cache=std::make_shared<pfservices::PfCache>(std::filesystem::path(directory.path().toStdWString())/"cache",65536);
        QImage image(96,54,QImage::Format_RGBA8888);image.fill(Qt::green);
        std::string error;QVERIFY(cache->put("preview-v1|qml",png(image),error));
        auto session=pfui::ResultPreviewImages::create({{source.toStdString(),"preview-v1|qml","",0}},cache,{});
        QQmlEngine engine;pfui::installResultPreviewImages(engine);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { width: 96; height: 54 }",QUrl());
        std::unique_ptr<QObject> item(component.create());QVERIFY2(item,qPrintable(component.errorString()));
        item->setProperty("source",QUrl(session->url(0)));
        QTRY_COMPARE_WITH_TIMEOUT(item->property("status").toInt(),1,5000);
        QCOMPARE(item->property("sourceSize").toSize(),QSize(96,54));
    }
};
QTEST_MAIN(ResultPreviewTests)
#include "test_result_preview_images.moc"
