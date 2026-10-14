#include <pfcore/FaceConsensus.hpp>
#include <algorithm>
#include <cmath>

namespace pfcore {
namespace {
std::vector<const DirectFaceSample*> independent(std::span<const DirectFaceSample> input,
    double start, double end)
{
    std::vector<const DirectFaceSample*> sorted, result;
    if(!std::isfinite(start) || !std::isfinite(end) || start>end)return result;
    for(const auto& sample:input) {
        if(!std::isfinite(sample.timestampSeconds) || sample.timestampSeconds<start
            || sample.timestampSeconds>end || sample.embedding.empty())continue;
        double norm=0;
        for(float value:sample.embedding)norm+=static_cast<double>(value)*value;
        if(!std::isfinite(norm) || norm<1e-12)continue;
        sorted.push_back(&sample);
    }
    std::stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
    // Conflicting descriptors attributed to one source frame are ambiguous.
    // Do not choose whichever record happened to arrive first.
    for(std::size_t i=1;i<sorted.size();++i)
        if(std::abs(sorted[i]->timestampSeconds-sorted[i-1]->timestampSeconds)<1e-6
            && sorted[i]->embedding!=sorted[i-1]->embedding)return {};
    for(auto sample:sorted) {
        if(result.empty() || sample->timestampSeconds-result.back()->timestampSeconds>=.1-1e-9)
            result.push_back(sample);
    }
    return result;
}
double cosine(const std::vector<float>& a,const std::vector<float>& b)
{
    if(a.size()!=b.size())return -2.;
    double aa=0,bb=0,ab=0;
    for(std::size_t i=0;i<a.size();++i){aa+=double(a[i])*a[i];bb+=double(b[i])*b[i];ab+=double(a[i])*b[i];}
    return std::clamp(ab/std::sqrt(aa*bb),-1.,1.);
}
}
FaceConsensusResult compareDirectFaces(std::span<const DirectFaceSample> left,double leftStart,double leftEnd,
    std::span<const DirectFaceSample> right,double rightStart,double rightEnd)
{
    FaceConsensusResult result;
    const auto a=independent(left,leftStart,leftEnd),b=independent(right,rightStart,rightEnd);
    result.leftIndependent=a.size();result.rightIndependent=b.size();
    if(a.size()<3 || b.size()<3)return result;
    // Require repeated support in both directions. One favourable image or
    // repeated entries with one source timestamp cannot establish evidence.
    double supportedSimilaritySum=0;
    const auto count=[&](const auto& from,const auto& to,bool& contradiction) {
        std::size_t supported=0;
        for(auto sample:from) {
            std::size_t usable=0,positive=0;double best=-2;
            std::vector<double> similarities;
            for(auto reference:to) {
                const auto score=cosine(sample->embedding,reference->embedding);
                if(score<-1)continue;
                ++usable;best=std::max(best,score);positive+=score>=.363;
                similarities.push_back(score);
            }
            if(usable>=3 && best<=.25)contradiction=true;
            if(usable>=3 && positive>=2 && positive*3>=usable*2) {
                ++supported;
                std::sort(similarities.begin(),similarities.end());
                const auto n=similarities.size();
                supportedSimilaritySum+=(similarities[(n-1)/2]+similarities[n/2])*.5;
            }
        }
        return supported;
    };
    bool contradiction=false;
    result.leftSupported=count(a,b,contradiction);result.rightSupported=count(b,a,contradiction);
    if(contradiction)result.decision=FaceConsensusDecision::Contradicted;
    else if(result.leftSupported>=3 && result.rightSupported>=3
        && result.leftSupported*3>=a.size()*2 && result.rightSupported*3>=b.size()*2)
        result.decision=FaceConsensusDecision::Supported;
    if(result.decision==FaceConsensusDecision::Supported)
        result.meanSupportedSimilarity=supportedSimilaritySum/(result.leftSupported+result.rightSupported);
    return result;
}
}
