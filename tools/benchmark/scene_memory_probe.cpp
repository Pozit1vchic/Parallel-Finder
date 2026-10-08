// Synthetic long-source control for exact boundary and memory comparisons.
#include <pfcore/SceneDetector.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
int main(int argc,char** argv) {
    const int count=argc>1 ? std::atoi(argv[1]) : 20000;
    if(count<2 || count>100000)return 2;
    std::vector<std::vector<std::uint8_t>> images(256,std::vector<std::uint8_t>(64*36*4));
    for(int f=0;f<256;++f)for(int y=0;y<36;++y)for(int x=0;x<64;++x) {
        const int i=(y*64+x)*4;
        const int level=f<128 ? f*2 : (255-f)*2;
        images[f][i]=(x*3+level)%256;images[f][i+1]=(y*5+level)%256;
        images[f][i+2]=(f/16)%2 ? level : 255-level;images[f][i+3]=255;
    }
    std::vector<pfcore::SceneSample> samples;samples.reserve(count);
    for(int i=0;i<count;++i)samples.push_back({i*.25,64,36,images[i%256]});
    const auto start=std::chrono::steady_clock::now();
    const auto boundaries=pfcore::SceneDetector(27,2,3).detect(samples);
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    double peakMb=0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))peakMb=memory.PeakWorkingSetSize/1048576.0;
#endif
    std::printf("{\"samples\":%d,\"elapsedMs\":%.3f,\"peakWorkingSetMb\":%.3f,\"boundaries\":[",count,ms,peakMb);
    bool comma=false;for(const auto& b:boundaries) {
        std::printf("%s[%.17g,%.17g]",comma ? "," : "",b.timestampSeconds,b.score);comma=true;
    }
    std::puts("]}");
}
