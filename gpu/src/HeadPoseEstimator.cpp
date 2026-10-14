#include "pfgpu/HeadPoseEstimator.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pfgpu {
namespace {
struct Coefficients { int start; std::vector<int> weights; };
// Separable, antialiased bilinear sampling with a rounded byte intermediate,
// matching the PIL RGB Resize used by the published model's preprocessing.
// The algorithm is independently expressed here and checked against Pillow;
// nearest-neighbour sampling or a float-only resize is not equivalent.
std::vector<Coefficients> coefficients(int source,int target) {
    std::vector<Coefficients> result;
    const double scale=static_cast<double>(source)/target,filterScale=std::max(1.,scale);
    for(int i=0;i<target;++i) {
        const double center=(i+.5)*scale;
        const int begin=std::max(0,static_cast<int>(center-filterScale+.5));
        const int end=std::min(source,static_cast<int>(center+filterScale+.5));
        std::vector<double> weights;double sum=0;
        for(int j=begin;j<end;++j) {
            const double weight=std::max(0.,1.-std::abs((j-center+.5)/filterScale));
            weights.push_back(weight);sum+=weight;
        }
        Coefficients row{begin,{}};
        for(const auto weight:weights)row.weights.push_back(static_cast<int>(weight/sum*(1<<22)+.5));
        result.push_back(std::move(row));
    }
    return result;
}
int cropCenterOffset(int dimension) {
    const int difference=dimension-224;
    // Python round uses ties-to-even; odd differences need this exact rule.
    return difference/2 + (difference%2 && (difference/2)%2 ? 1:0);
}
}

FloatTensor prepareHeadPoseInput(const ReIdImage& image) {
    if(!image.rgba || image.width<=0 || image.height<=0 || image.width>32768 || image.height>32768
        || !std::isfinite(image.left+image.top+image.right+image.bottom)
        || image.left<0 || image.top<0 || image.right>image.width || image.bottom>image.height
        || std::floor(image.left)!=image.left || std::floor(image.top)!=image.top
        || std::floor(image.right)!=image.right || std::floor(image.bottom)!=image.bottom
        || image.right<=image.left || image.bottom<=image.top)
        throw std::invalid_argument("HeadPoseEstimator: invalid crop");
    const int left=static_cast<int>(image.left),top=static_cast<int>(image.top);
    const int width=static_cast<int>(image.right)-left,height=static_cast<int>(image.bottom)-top;
    if(width<16 || height<16 || width>height*8 || height>width*8)
        throw std::invalid_argument("HeadPoseEstimator: face crop is too small or elongated");
    const int outWidth=width<=height ? 256:static_cast<int>(256LL*width/height);
    const int outHeight=height<=width ? 256:static_cast<int>(256LL*height/width);
    const auto horizontal=coefficients(width,outWidth),vertical=coefficients(height,outHeight);
    std::vector<std::uint8_t> intermediate(static_cast<std::size_t>(outWidth)*height*3);
    for(int y=0;y<height;++y)for(int x=0;x<outWidth;++x)for(int c=0;c<3;++c) {
        const auto& row=horizontal[x];std::int64_t sum=1<<21;
        for(std::size_t k=0;k<row.weights.size();++k)
            sum+=static_cast<std::int64_t>(image.rgba[(static_cast<std::size_t>(y+top)*image.width+left+row.start+k)*4+c])*row.weights[k];
        intermediate[(static_cast<std::size_t>(y)*outWidth+x)*3+c]=static_cast<std::uint8_t>(std::clamp<std::int64_t>(sum>>22,0,255));
    }
    FloatTensor result;result.shape={1,3,224,224};result.values.resize(3*224*224);
    constexpr float mean[]={.485F,.456F,.406F},deviation[]={.229F,.224F,.225F};
    const int offsetX=cropCenterOffset(outWidth),offsetY=cropCenterOffset(outHeight);
    for(int y=0;y<224;++y)for(int x=0;x<224;++x)for(int c=0;c<3;++c) {
        const auto& row=vertical[y+offsetY];std::int64_t sum=1<<21;
        for(std::size_t k=0;k<row.weights.size();++k)
            sum+=static_cast<std::int64_t>(intermediate[((row.start+k)*outWidth+x+offsetX)*3+c])*row.weights[k];
        const auto pixel=std::clamp<std::int64_t>(sum>>22,0,255);
        result.values[c*224*224+y*224+x]=(static_cast<float>(pixel)/255.F-mean[c])/deviation[c];
    }
    return result;
}

HeadPoseEstimator::HeadPoseEstimator(std::string path,Provider provider)
    :modelPath_(std::move(path)),provider_(provider) {}

std::optional<std::array<double,9>> HeadPoseEstimator::infer(const ReIdImage& image) {
    const auto input=prepareHeadPoseInput(image);
    const auto session=processSessionCache().getOrCreate(ModelRef::fromPath(modelPath_),{provider_,0,"head-orientation",1});
    if(!session.ok)throw std::runtime_error(session.error);
    const auto spec=describeSession(session.handle);
    if(!spec.ok || spec.input.shape!=std::vector<std::int64_t>{1,3,224,224})
        throw std::runtime_error("HeadPoseEstimator: expected fixed RGB 224x224 model");
    const auto output=runFloat(session.handle,input);
    if(!output.ok)throw std::runtime_error(output.error);
    if(output.outputs.size()!=1 || output.outputs.front().shape!=std::vector<std::int64_t>{1,3,3})
        throw std::runtime_error("HeadPoseEstimator: expected a rotation matrix");
    const auto& values=output.outputs.front().values;
    if(values.size()!=9)return {};
    std::array<double,9> rotation;
    for(int i=0;i<9;++i) {if(!std::isfinite(values[i]))return {};rotation[i]=values[i];}
    // Reject malformed graphs rather than turning their output into a label.
    for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
        double dot=0;for(int k=0;k<3;++k)dot+=rotation[a*3+k]*rotation[b*3+k];
        if(std::abs(dot-(a==b ? 1.:0.))>.002)return {};
    }
    const double determinant=rotation[0]*(rotation[4]*rotation[8]-rotation[5]*rotation[7])
        -rotation[1]*(rotation[3]*rotation[8]-rotation[5]*rotation[6])
        +rotation[2]*(rotation[3]*rotation[7]-rotation[4]*rotation[6]);
    if(std::abs(determinant-1.)>.002)return {};
    return rotation;
}
}
