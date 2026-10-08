// Deterministic identity scaling probe. Synthetic evidence is a performance
// control, not a measure of face recognition accuracy.
#include <pfcore/DominantPerson.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
    const auto count=argc>1 ? std::atoi(argv[1]) : 1000;
    if(count<1 || count>10000)return 2;
    std::vector<pfcore::IdentitySummary> people(count);
    for(int i=0;i<count;++i) {
        auto& p=people[i];p.face.assign(512,0);p.body.assign(512,0);
        p.face[i%5]=1;p.body[i%5]=1;p.faceEvidence=p.bodyEvidence=1;
        p.duration=1+(i%5==0);p.area=1;p.observationTimes={double(i*2),double(i*2+1)};
    }
    const auto start=std::chrono::steady_clock::now();
    const auto selected=pfcore::selectDominantIdentities(people);
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("{\"elapsedMs\":%.3f,\"selected\":[",ms);
    bool comma=false;for(int i=0;i<count;++i)if(selected[i]) {std::printf("%s%d",comma ? "," : "",i);comma=true;}
    std::puts("]}");
}
