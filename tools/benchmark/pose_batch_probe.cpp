#include <pfgpu/PoseEstimator.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

// Real-frame batch parity and throughput. Same float32 checkpoint; each tail
// is padded by the application's actual workspace, then discarded by decoder.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=6){std::cerr<<"usage: pf_pose_batch_probe b1.onnx batch.onnx batch frames-directory repetitions\n";return 2;}
    try {
        const int batch=args[3].toInt(),repeat=std::clamp(args[5].toInt(),1,100);
        if(batch<2||batch>16)throw std::runtime_error("Invalid batch size");
        const QDir directory(args[4]);
        const auto names=directory.entryList({"*.png","*.jpg"},QDir::Files,QDir::Name);
        if(names.isEmpty()||names.size()>128)throw std::runtime_error("Expected 1..128 frames");
        std::vector<QImage> pixels;
        for(const auto& name:names) {
            auto image=QImage(directory.filePath(name)).convertToFormat(QImage::Format_RGBA8888);
            if(image.isNull())throw std::runtime_error("Cannot load frame");
            pixels.push_back(std::move(image));
        }
        std::vector<pfgpu::PoseImage> images;
        for(const auto& image:pixels)images.push_back({image.width(),image.height(),image.constBits()});
        pfgpu::PoseEstimatorParams singleParams;singleParams.provider=pfgpu::Provider::Cuda;
        auto batchParams=singleParams;batchParams.profile="b"+std::to_string(batch);batchParams.batchSize=batch;
        pfgpu::PoseEstimator single(args[1].toStdString(),singleParams),batched(args[2].toStdString(),batchParams);
        single.prepare();batched.prepare();
        std::vector<std::vector<pfgpu::PoseDetection>> references;
        for(const auto& image:images)references.push_back(single.infer(image));
        const auto runBatch=[&] {
            std::vector<std::vector<pfgpu::PoseDetection>> outputs;
            for(size_t begin=0;begin<images.size();begin+=static_cast<size_t>(batch)) {
                const auto end=std::min(images.size(),begin+batch);
                const auto part=batched.inferBatch(std::vector<pfgpu::PoseImage>(images.begin()+begin,images.begin()+end));
                outputs.insert(outputs.end(),part.begin(),part.end());
            }
            return outputs;
        };
        const auto actual=runBatch();
        if(actual.size()!=references.size())throw std::runtime_error("Frame count changed");
        size_t changedCounts=0,matchedDetections=0;
        double maxBox=0,maxConfidence=0,maxKeypoint=0;
        for(size_t f=0;f<actual.size();++f) {
            if(actual[f].size()!=references[f].size())++changedCounts;
            std::vector<bool> used(actual[f].size());
            for(const auto& expected:references[f]) {
                size_t best=actual[f].size();double distance=INFINITY;
                for(size_t d=0;d<actual[f].size();++d)if(!used[d]) {
                    const auto& candidate=actual[f][d];
                    const double score=std::abs(candidate.left-expected.left)+std::abs(candidate.right-expected.right)
                        +std::abs(candidate.top-expected.top)+std::abs(candidate.bottom-expected.bottom);
                    if(score<distance){distance=score;best=d;}
                }
                if(best==actual[f].size())continue;
                used[best]=true;++matchedDetections;const auto& candidate=actual[f][best];
                maxBox=std::max({maxBox,double(std::abs(candidate.left-expected.left)),double(std::abs(candidate.top-expected.top)),
                    double(std::abs(candidate.right-expected.right)),double(std::abs(candidate.bottom-expected.bottom))});
                maxConfidence=std::max(maxConfidence,double(std::abs(candidate.confidence-expected.confidence)));
                if(candidate.keypoints.size()!=expected.keypoints.size())throw std::runtime_error("Keypoint shape changed");
                for(size_t k=0;k<candidate.keypoints.size();++k) {
                    const double diff=std::abs(candidate.keypoints[k]-expected.keypoints[k]);
                    if(k%3==2)maxConfidence=std::max(maxConfidence,diff);else maxKeypoint=std::max(maxKeypoint,diff);
                }
            }
        }
        using Clock=std::chrono::steady_clock;
        double singleMs=0,batchMs=0;
        for(int r=0;r<repeat;++r) {
            const auto runSingle=[&]{const auto start=Clock::now();for(const auto& image:images)(void)single.infer(image);
                singleMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
            const auto runBatched=[&]{const auto start=Clock::now();(void)runBatch();
                batchMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
            if(r%2){runBatched();runSingle();}else{runSingle();runBatched();}
        }
        const QJsonObject report{{"frames",static_cast<int>(images.size())},{"batch",batch},{"repeat",repeat},
            {"singleMs",singleMs},{"batchMs",batchMs},{"speedup",singleMs/batchMs},
            {"changedDetectionCounts",static_cast<double>(changedCounts)},{"matchedDetections",static_cast<double>(matchedDetections)},
            {"maxBoxPixelDifference",maxBox},{"maxKeypointPixelDifference",maxKeypoint},{"maxConfidenceDifference",maxConfidence}};
        std::cout<<QJsonDocument(report).toJson().constData();
        // This reports numerical change; it does not silently redefine parity
        // to a tolerance or claim classification quality from stable counts.
        return changedCounts||maxBox||maxKeypoint||maxConfidence ? 3:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
