#include "ProviderDownload.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <cstdio>
#include <string_view>

// Controlled scheduling benchmark. No public network, disk throughput, hash
// verification or general download speed is represented by these timings.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QJsonArray profiles;
    for(const auto* profile:{"uniform","straggler","transient"}) {
        QJsonArray timings;int overallPeak=0;
        for(int pass=0;pass<5;++pass) {
            pfservices::ProviderAsset asset;asset.sizeBytes=16;
            for(int i=0;i<16;++i)asset.parts.push_back({std::to_string(i),std::string(64,'a'),1,"https://example.invalid/chunk"});
            std::vector<std::atomic_int> attempts(16);std::atomic_int active=0,peak=0;
            std::string error;QElapsedTimer clock;clock.start();
            const bool ok=pfservices::detail::downloadProviderParts(asset,{}, {},error,
                [&](const auto& chunk,const auto&,auto report,std::string& failure) {
                    const auto index=std::stoi(chunk.filename);const int attempt=++attempts[index];
                    const int running=++active;int previous=peak.load();
                    while(previous<running&&!peak.compare_exchange_weak(previous,running)){}
                    if(std::string_view(profile)=="transient"&&index==0&&attempt==1) {
                        --active;failure="model download: HTTP 429";return false;
                    }
                    QThread::msleep(std::string_view(profile)=="straggler"&&index==3?350:50);
                    report(1,1);--active;return true;
                });
            if(!ok) {std::fprintf(stderr,"%s\n",error.c_str());return 1;}
            timings.push_back(clock.nsecsElapsed()/1e6);overallPeak=std::max(overallPeak,peak.load());
            for(int i=0;i<16;++i)if(attempts[i]!=(std::string_view(profile)=="transient"&&i==0?2:1))return 2;
        }
        profiles.push_back(QJsonObject{{"profile",profile},{"passTimesMs",timings},{"peakDownloads",overallPeak}});
    }
    const auto bytes=QJsonDocument(QJsonObject{{"parts",16},{"maximumConnections",4},{"profiles",profiles},
        {"scope","controlled callback scheduling, not network throughput"}}).toJson();
    std::printf("%s",bytes.constData());return 0;
}
