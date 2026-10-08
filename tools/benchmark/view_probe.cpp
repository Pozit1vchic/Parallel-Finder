// Real-video, bit-exact comparison of independent and bounded parallel seeks.
#include <pfcore/SceneDetector.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
int main(int argc,char** argv) {
    if(argc!=5 && argc!=6)return 2;
    const std::string mode=argv[1],source=argv[2];const int count=std::atoi(argv[3]);
    const bool nvidia=mode=="batched-nvdec" || mode=="parallel-nvdec";
    if((mode!="serial" && mode!="batched" && mode!="parallel" && !nvidia) || count<1 || count>10000)return 2;
    pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;options.threads=8;
    options.preferNvidia=nvidia;
    decoder.open(source,options);decoder.setRgbaMaxDimensions(160,90);
    const auto duration=decoder.info().durationSeconds;
    std::vector<pfcore::SceneViewRequest> requests;
    if(argc==6) {
        std::ifstream targets(argv[5]);pfcore::SceneViewRequest request;
        while(targets>>request.target>>request.start>>request.end)requests.push_back(request);
        if(!targets.eof() || requests.size()!=static_cast<std::size_t>(count))return 2;
    } else for(int i=0;i<count;++i)requests.push_back({duration*(i+.5)/count,0,duration});
    const auto start=std::chrono::steady_clock::now();
    std::vector<pfcore::SceneViewObservation> observations(count);
    if(mode=="parallel" || mode=="parallel-nvdec")observations=pfcore::sampleSceneViewsParallel(source,requests,{}, {},8,nvidia);
    else if(mode=="batched" || mode=="batched-nvdec")observations=pfcore::sampleSceneViews(decoder,requests);
    else for(int i=0;i<count;++i) {
        decoder.seek(requests[i].target);pfcore::DecodedFrame frame;
        while(decoder.readNext(frame,false)) {
            if(frame.timestampSeconds+1e-6<requests[i].target)continue;
            if(frame.timestampSeconds<=duration+1e-6 && decoder.convertCurrentFrameToRgba(frame))
                observations[i]={frame.timestampSeconds,pfcore::sceneViewDescriptor({frame.timestampSeconds,frame.width,frame.height,frame.rgba})};
            break;
        }
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::ofstream out(argv[4],std::ios::binary);if(!out)return 3;
    for(const auto& o:observations) {
        const auto size=static_cast<std::uint32_t>(o.pixels.size());
        out.write(reinterpret_cast<const char*>(&o.timestampSeconds),sizeof(double));
        out.write(reinterpret_cast<const char*>(&size),sizeof(size));
        out.write(reinterpret_cast<const char*>(o.pixels.data()),o.pixels.size()*sizeof(float));
    }
    std::printf("{\"mode\":\"%s\",\"samples\":%d,\"elapsedMs\":%.3f}\n",mode.c_str(),count,ms);
    return out ? 0 : 3;
}
