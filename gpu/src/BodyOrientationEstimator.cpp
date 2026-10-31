#include "pfgpu/BodyOrientationEstimator.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pfgpu {
namespace {
constexpr int side=1024;
int roundEven(double value) {
    const auto integer=static_cast<int>(std::floor(value));const auto fraction=value-integer;
    return integer+(fraction>.5||(fraction==.5&&integer%2));
}
struct Sample {int first,second,a,b;};
std::vector<Sample> samples(int source,int target) {
    std::vector<Sample> result;result.reserve(target);
    for(int i=0;i<target;++i) {
        const double position=(i+.5)*source/target-.5;
        const auto first=static_cast<int>(std::floor(position));const auto fraction=position-first;
        result.push_back({std::clamp(first,0,source-1),std::clamp(first+1,0,source-1),
            roundEven((1-fraction)*2048),roundEven(fraction*2048)});
    }
    return result;
}
double wrap(double degrees) {return std::remainder(degrees,360.);}
bool valid(const BodyOrientationRegion& r) {
    return std::isfinite(r.left)&&std::isfinite(r.top)&&std::isfinite(r.right)&&std::isfinite(r.bottom)
        &&r.right>r.left&&r.bottom>r.top;
}
BodyOrientationRegion region(const BodyOrientationDetection& d) {return {d.left,d.top,d.right,d.bottom};}
double iou(const BodyOrientationRegion& a,const BodyOrientationRegion& b) {
    if(!valid(a)||!valid(b))return 0;
    const auto intersection=std::max(0.,std::min(a.right,b.right)-std::max(a.left,b.left))
        *std::max(0.,std::min(a.bottom,b.bottom)-std::max(a.top,b.top));
    return intersection/((a.right-a.left)*(a.bottom-a.top)+(b.right-b.left)*(b.bottom-b.top)-intersection);
}
struct Assignment {std::optional<std::size_t> index;double overlap=-1,margin=-1;bool ambiguous=false;};
std::vector<Assignment> assign(const std::vector<BodyOrientationRegion>& subjects,const std::vector<BodyOrientationDetection>& detections) {
    std::vector<Assignment> result(subjects.size());
    for(std::size_t i=0;i<subjects.size();++i) {
        double best=0,second=0;std::optional<std::size_t> index;
        for(std::size_t j=0;j<detections.size();++j) {
            const auto& d=detections[j];
            if(!std::isfinite(d.yawDegrees)||!std::isfinite(d.detectionScore)||d.detectionScore<.35||d.detectionScore>1)continue;
            const double overlap=iou(subjects[i],region(d));
            if(overlap>best) {second=best;best=overlap;index=j;}else second=std::max(second,overlap);
        }
        auto& a=result[i];a.overlap=best;a.margin=best-second;
        if(!index||best<.5)continue;
        if(best-second<.15) {a.ambiguous=true;continue;}
        double competitor=0;
        for(std::size_t j=0;j<subjects.size();++j)if(j!=i)competitor=std::max(competitor,iou(subjects[j],region(detections[*index])));
        a.margin=std::min(a.margin,best-competitor);
        if(best-competitor<.15) {a.ambiguous=true;continue;}
        a.index=index;
    }
    return result;
}
}
BodyOrientationInput prepareBodyOrientationInput(const PoseImage& image,bool mirrored) {
    if(!image.rgba||image.width<=0||image.height<=0||image.width>32768||image.height>32768)
        throw std::invalid_argument("BodyOrientationEstimator: invalid image");
    BodyOrientationInput result;result.sourceWidth=image.width;result.sourceHeight=image.height;result.mirrored=mirrored;
    result.scale=std::min(double(side)/image.width,double(side)/image.height);
    const int width=std::max(1,roundEven(image.width*result.scale)),height=std::max(1,roundEven(image.height*result.scale));
    result.padLeft=(side-width)/2;result.padTop=(side-height)/2;
    result.tensor.shape={1,3,side,side};result.tensor.values.assign(3*side*side,114.F/255.F);
    const auto horizontal=samples(image.width,width),vertical=samples(image.height,height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<3;++c) {
        const auto& a=horizontal[x];const auto& b=vertical[y];
        const auto pixel=[&](int px,int py) {
            if(mirrored)px=image.width-1-px;
            return image.rgba[(static_cast<std::size_t>(py)*image.width+px)*4+c];
        };
        const auto upper=std::int64_t(pixel(a.first,b.first))*a.a+std::int64_t(pixel(a.second,b.first))*a.b;
        const auto lower=std::int64_t(pixel(a.first,b.second))*a.a+std::int64_t(pixel(a.second,b.second))*a.b;
        const auto value=std::clamp<std::int64_t>((upper*b.a+lower*b.b+(1<<21))>>22,0,255);
        result.tensor.values[c*side*side+(y+result.padTop)*side+x+result.padLeft]=float(value)/255.F;
    }
    return result;
}
std::vector<BodyOrientationDetection> decodeBodyOrientationOutput(const FloatTensor& output,const BodyOrientationInput& input) {
    if(output.shape.size()!=3||output.shape[0]!=1||output.shape[1]<=0||output.shape[1]>65280||output.shape[2]!=7
        ||output.values.size()!=static_cast<std::size_t>(output.shape[1])*7||input.sourceWidth<=0||input.sourceHeight<=0
        ||!std::isfinite(input.scale)||input.scale<=0||input.padLeft<0||input.padTop<0||input.padLeft>=side||input.padTop>=side)
        throw std::invalid_argument("BodyOrientationEstimator: invalid output contract");
    std::vector<BodyOrientationDetection> candidates;
    for(std::size_t i=0;i<output.values.size();i+=7) {
        const auto* r=output.values.data()+i;
        if(!std::all_of(r,r+7,[](float x){return std::isfinite(x);})||r[2]<=0||r[3]<=0
            ||r[4]<0||r[4]>1||r[5]<0||r[5]>1||r[6]<0||r[6]>1)continue;
        const double score=r[4]*r[5];if(score<=.35)continue;
        candidates.push_back({r[0]-r[2]*.5,r[1]-r[3]*.5,r[0]+r[2]*.5,r[1]+r[3]*.5,score,wrap(180.-double(r[6])*360.)});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.detectionScore>b.detectionScore;});
    if(candidates.size()>30000)candidates.resize(30000);
    std::vector<BodyOrientationDetection> retained;
    for(auto d:candidates) {
        if(std::any_of(retained.begin(),retained.end(),[&](const auto& other){return iou(region(d),region(other))>.45;}))continue;
        retained.push_back(d);if(retained.size()==300)break;
    }
    std::vector<BodyOrientationDetection> result;
    for(auto d:retained) {
        d.left=(d.left-input.padLeft)/input.scale;d.right=(d.right-input.padLeft)/input.scale;
        d.top=(d.top-input.padTop)/input.scale;d.bottom=(d.bottom-input.padTop)/input.scale;
        if(input.mirrored) {const double left=d.left;d.left=input.sourceWidth-d.right;d.right=input.sourceWidth-left;d.yawDegrees=wrap(-d.yawDegrees);}
        d.left=std::clamp(d.left,0.,double(input.sourceWidth));d.right=std::clamp(d.right,0.,double(input.sourceWidth));
        d.top=std::clamp(d.top,0.,double(input.sourceHeight));d.bottom=std::clamp(d.bottom,0.,double(input.sourceHeight));
        if(valid(region(d)))result.push_back(d);
    }
    return result;
}
std::vector<BodyYawObservation> associateBodyOrientations(const std::vector<BodyOrientationRegion>& subjects,
    const std::vector<BodyOrientationDetection>& original,const std::vector<BodyOrientationDetection>& mirrored) {
    const auto a=assign(subjects,original),b=assign(subjects,mirrored);
    std::vector<BodyYawObservation> result(subjects.size());
    for(std::size_t i=0;i<subjects.size();++i) {
        auto& o=result[i];o.associationIou=std::min(a[i].overlap,b[i].overlap);o.associationMargin=std::min(a[i].margin,b[i].margin);
        if(!valid(subjects[i])) {o.status=BodyYawStatus::InvalidSubject;continue;}
        if(a[i].ambiguous||b[i].ambiguous) {o.status=BodyYawStatus::AmbiguousAssociation;continue;}
        if(!a[i].index||!b[i].index)continue;
        const auto& x=original[*a[i].index];const auto& y=mirrored[*b[i].index];
        o.detectionScore=std::min(x.detectionScore,y.detectionScore);o.mirrorDifferenceDegrees=std::abs(wrap(x.yawDegrees-y.yawDegrees));
        if(o.mirrorDifferenceDegrees>25) {o.status=BodyYawStatus::MirrorDisagreement;continue;}
        o.yawDegrees=wrap(x.yawDegrees+wrap(y.yawDegrees-x.yawDegrees)*.5);o.status=BodyYawStatus::Observed;
    }
    return result;
}
BodyOrientationEstimator::BodyOrientationEstimator(std::string path,Provider provider,std::size_t threads)
    :modelPath_(std::move(path)),provider_(provider),threads_(threads) {}
std::vector<BodyOrientationDetection> BodyOrientationEstimator::infer(const PoseImage& image,bool mirrored) {
    const auto input=prepareBodyOrientationInput(image,mirrored);
    const auto session=processSessionCache().getOrCreate(ModelRef::fromPath(modelPath_),{provider_,0,"body-orientation",threads_});
    if(!session.ok)throw std::runtime_error(session.error);
    const auto spec=describeSession(session.handle);
    if(!spec.ok||spec.input.shape!=std::vector<std::int64_t>{1,3,side,side})
        throw std::runtime_error("BodyOrientationEstimator: expected fixed RGB 1024x1024 model");
    const auto output=runFloat(session.handle,input.tensor);
    if(!output.ok)throw std::runtime_error(output.error);
    if(output.outputs.size()!=1||output.outputs.front().shape!=std::vector<std::int64_t>{1,65280,7})
        throw std::runtime_error("BodyOrientationEstimator: expected JointBDOE-S output");
    return decodeBodyOrientationOutput(output.outputs.front(),input);
}
}
