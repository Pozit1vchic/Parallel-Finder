#include <pfcore/PathEncoding.hpp>
#include <pfcore/VideoSampleReader.hpp>
#include <pfgpu/PoseEstimator.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
QByteArray digest(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot open fingerprint input");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))throw std::runtime_error("Cannot fingerprint input");
    return hash.result().toHex();
}
void save(const QString& path,const QJsonObject& object) {
    QSaveFile output(path);const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())
        throw std::runtime_error("Cannot publish observation report");
}
double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}

// Dataset observation extraction, using the application's real decoder and
// pose estimator. Action labels are never sent to the estimator. All missing
// detections remain in the output; this does not perform identity admission.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()<5 || args.size()>7) {
        std::cerr<<"usage: pf_pose_observation_probe MODEL MANIFEST OUTPUT_DIR FPS [cpu|cuda] [THREADS=4]\n";return 2;
    }
    try {
        bool valid=false;const double fps=args[4].toDouble(&valid);
        if(!valid || !std::isfinite(fps) || fps<=0 || fps>60)throw std::runtime_error("Invalid sample FPS");
        const QString provider=args.size()>5?args[5]:QStringLiteral("cpu");
        if(provider!="cpu" && provider!="cuda")throw std::runtime_error("Invalid provider");
        const int threads=args.size()>6?args[6].toInt(&valid):4;
        if(threads<1 || threads>32 || (args.size()>6&&!valid))throw std::runtime_error("Invalid thread count");
        QFile manifest(args[2]);
        if(!manifest.open(QIODevice::ReadOnly) || manifest.size()>4*1024*1024)throw std::runtime_error("Cannot open dataset manifest");
        QJsonParseError error;const auto input=QJsonDocument::fromJson(manifest.readAll(),&error).object();
        const auto videos=input["videos"].toArray();
        if(error.error!=QJsonParseError::NoError || input["schemaVersion"].toInt()!=1 || videos.isEmpty())
            throw std::runtime_error("Invalid dataset manifest");
        const auto outputRoot=QFileInfo(args[3]).absoluteFilePath();
        if(!QDir().mkpath(outputRoot))throw std::runtime_error("Cannot create output directory");
        const QDir sourceRoot(QFileInfo(args[2]).absolutePath());
        const auto modelHash=QString::fromLatin1(digest(args[1]));
        pfgpu::PoseEstimatorParams params;params.provider=provider=="cuda"?pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        params.intraOpThreads=static_cast<std::size_t>(threads);
        pfgpu::PoseEstimator estimator(pfcore::pathToUtf8(std::filesystem::path(args[1].toStdWString())),params);
        const auto begin=std::chrono::steady_clock::now();QJsonArray reports;
        for(qsizetype index=0;index<videos.size();++index) {
            const auto video=videos[index].toObject();const auto name=video["file"].toString();
            if(name.isEmpty() || name!=QFileInfo(name).fileName() || name.contains('\\') || name.contains(':'))
                throw std::runtime_error("Unsafe dataset filename");
            const auto path=sourceRoot.filePath(name);const auto sourceHash=QString::fromLatin1(digest(path));
            if(sourceHash!=video["sha256"].toString())throw std::runtime_error("Dataset source fingerprint mismatch");
            QJsonObject contract{{"schemaVersion",1},{"sourceSha256",sourceHash},{"modelSha256",modelHash},
                {"preprocessing",pfgpu::kPosePreprocessingContract},
                {"sampleFps",fps},{"provider",provider},{"threads",threads},{"selection","highest detection confidence; first on exact tie"}};
            const auto output=outputRoot+'/'+name+".poses.json";
            QFile cached(output);bool reused=false;
            if(cached.open(QIODevice::ReadOnly) && cached.size()<64*1024*1024) {
                const auto old=QJsonDocument::fromJson(cached.readAll()).object();
                if(old["contract"].toObject()==contract && old["complete"].toBool()) {
                    reports.append(QJsonObject{{"file",name},{"report",QFileInfo(output).fileName()},{"reused",true}});reused=true;
                }
            }
            cached.close();
            if(!reused) {
                const auto started=std::chrono::steady_clock::now();
                pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions decode;decode.threads=threads;
                decoder.open(pfcore::pathToUtf8(std::filesystem::path(path.toStdWString())),decode);
                const auto info=decoder.info();
                if(!std::isfinite(info.frameRate) || info.frameRate<=0)throw std::runtime_error("Invalid source cadence");
                pfcore::VideoSampleReader samples(decoder,fps,0.,{},true,3,true);
                pfcore::VideoSample sample;QJsonArray frames;qint64 detected=0,ambiguous=0;double inferenceMs=0;
                while(samples.readNext(sample)) {
                    if(!sample.pose)continue;
                    const auto inferred=std::chrono::steady_clock::now();
                    const auto detections=estimator.infer({sample.frame.width,sample.frame.height,sample.frame.rgba.data()});
                    inferenceMs+=milliseconds(inferred);QJsonArray points,box;
                    if(!detections.empty()) {
                        const auto best=std::max_element(detections.begin(),detections.end(),[](const auto& a,const auto& b){return a.confidence<b.confidence;});
                        ++detected;if(detections.size()>1)++ambiguous;
                        for(std::size_t i=0;i+2<best->keypoints.size();i+=3)
                            points.append(QJsonArray{best->keypoints[i],best->keypoints[i+1],best->keypoints[i+2]});
                        box={best->left,best->top,best->right,best->bottom};
                    }
                    frames.append(QJsonObject{{"time",sample.frame.timestampSeconds},{"points",points},{"box",box},
                        {"detections",static_cast<qint64>(detections.size())}});
                }
                samples.finish();
                const auto diagnostics=decoder.diagnostics();const auto waits=samples.waitDiagnostics();
                const QJsonObject timing{{"elapsedMs",milliseconds(started)},{"inferenceMs",inferenceMs},
                    {"consumerWaitMs",waits.consumerMilliseconds},{"producerWaitMs",waits.producerMilliseconds},
                    {"sampledFrames",frames.size()},{"detectedFrames",detected},{"multipleDetectionFrames",ambiguous},
                    {"peakBufferedFrames",static_cast<qint64>(samples.peakBufferedSamples())}};
                save(output,QJsonObject{{"contract",contract},{"complete",true},{"video",video},
                    {"sourceFps",info.frameRate},{"durationSeconds",info.durationSeconds},{"width",info.width},{"height",info.height},
                    {"decodeBackend",QString::fromStdString(diagnostics.backend)},{"frames",frames},{"timing",timing}});
                reports.append(QJsonObject{{"file",name},{"report",QFileInfo(output).fileName()},{"reused",false},{"timing",timing}});
            }
            std::cout<<"completed "<<index+1<<'/'<<videos.size()<<' '<<name.toStdString()<<'\n'<<std::flush;
        }
        save(outputRoot+"/observations-manifest.json",QJsonObject{{"schemaVersion",1},{"datasetManifestSha256",QString::fromLatin1(digest(args[2]))},
            {"planSha256",input["planSha256"]},{"elapsedMs",milliseconds(begin)},{"videos",reports}});
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
