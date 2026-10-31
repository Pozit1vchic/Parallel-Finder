#include <pfcore/BodyOrientationSample.hpp>
#include <algorithm>
#include <map>

namespace pfcore {
BodyYawSummary summarizeBodyYaw(const std::vector<BodyOrientationSample>& samples,const std::string& source,double start,double end) {
    BodyYawSummary result;
    if(source.empty()||!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start)return result;
    std::map<double,const BodyOrientationSample*> unique;
    for(const auto& sample:samples) {
        if(!std::isfinite(sample.time)||sample.time<start||sample.time>=end)continue;
        const auto [position,inserted]=unique.emplace(sample.time,&sample);
        if(!inserted&&position->second) {
            if(*position->second!=sample)position->second=nullptr;
        }
    }
    result.total=unique.size();std::vector<const BodyOrientationSample*> supported;
    for(const auto& [time,sample]:unique)if(sample&&validBodyOrientationSample(*sample,source))supported.push_back(sample);
    result.observed=supported.size();
    if(supported.size()<3||supported.size()<.6*result.total)return result;
    const auto span=supported.back()->time-supported.front()->time;
    if(span<.3||span/(end-start)<.5)return result;
    for(std::size_t i=1;i<supported.size();++i) {
        const double elapsed=supported[i]->time-supported[i-1]->time;
        const auto change=std::abs(std::remainder(supported[i]->yawDegrees-supported[i-1]->yawDegrees,360.));
        if(change>std::max(30.,200.*elapsed)) {result.ambiguous=true;return result;}
    }
    const auto midpoint=(start+end)*.5;
    const auto selected=*std::min_element(supported.begin(),supported.end(),[&](const auto* a,const auto* b) {
        return std::abs(a->time-midpoint)<std::abs(b->time-midpoint);
    });
    double strength=1;
    for(const auto* sample:supported)strength=std::min({strength,sample->associationIou,
        1.-sample->mirrorDifference/50.,1.-sample->cropDifference/40.});
    result.yawDegrees=selected->yawDegrees;
    // Observation support, not calibrated angle accuracy. Missing data and
    // temporal coverage remain independent from the person's detection score.
    result.evidenceStrength=std::clamp(strength*double(supported.size())/result.total*std::min(1.,span/(end-start)),0.,1.);
    return result;
}
}
