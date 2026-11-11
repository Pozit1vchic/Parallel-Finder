#include <pfcore/MovementClassifier.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>

// Synthetic implementation benchmark, not classification ground truth.
// Compare binaries built from two revisions with identical arguments.
int main(int argc,char** argv) {
    const int iterations=argc>1?std::atoi(argv[1]):20000;
    if(iterations<=0)return 2;
    pfcore::MotionWindow window;
    for(int i=0;i<240;++i) {
        pfcore::PoseFrame frame;frame.timestampSeconds=i*.05;
        for(int joint=0;joint<17;++joint)
            frame.keypoints.push_back({joint%5+i*.1,joint/5.+i*.02,1.});
        window.frames.push_back(std::move(frame));
    }
    const pfcore::MovementClassifier classifier;
    double checksum=0;
    const auto started=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i) {
        const auto result=classifier.classify(window,2.,8.);
        checksum+=static_cast<int>(result.direction)+static_cast<int>(result.gesture)
            +result.directionConfidence+result.gestureConfidence;
    }
    std::cout<<"{\"iterations\":"<<iterations<<",\"elapsedMs\":"
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
        <<",\"checksum\":"<<checksum<<"}\n";
}
