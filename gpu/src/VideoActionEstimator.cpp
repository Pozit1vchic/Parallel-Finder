#include "pfgpu/VideoActionEstimator.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pfgpu {
VideoActionInput prepareVideoActionInput(std::span<const VideoActionFrame> frames,
                                        std::span<const VideoActionSubject> subjects)
{
    if(frames.size()<5 || frames.size()>192 || subjects.empty() || subjects.size()>32)
        throw std::invalid_argument("VideoActionEstimator: unbounded clip or subject count");
    const int width=frames.front().width,height=frames.front().height;
    if(width<2 || height<2 || width>1280 || height>720)
        throw std::invalid_argument("VideoActionEstimator: unsupported source geometry");
    for(const auto& frame:frames)
        if(!frame.rgba || frame.width!=width || frame.height!=height)
            throw std::invalid_argument("VideoActionEstimator: inconsistent clip pixels");
    const int outHeight=width<height ? static_cast<int>(std::floor(double(height)/width*256)):256;
    const int outWidth=width<height ? 256:static_cast<int>(std::floor(double(width)/height*256));
    if(outWidth>1024 || outHeight>1024)
        throw std::invalid_argument("VideoActionEstimator: unsupported clip aspect ratio");
    const float scale=width<height ? float(outHeight)/height:float(outWidth)/width;
    VideoActionInput result;
    result.boxes.shape={static_cast<std::int64_t>(subjects.size()),5};
    for(const auto& subject:subjects) {
        if(!std::isfinite(subject.left) || !std::isfinite(subject.top)
            || !std::isfinite(subject.right) || !std::isfinite(subject.bottom)
            || subject.left>=subject.right || subject.top>=subject.bottom
            || subject.right<=0 || subject.bottom<=0 || subject.left>=width || subject.top>=height)
            throw std::invalid_argument("VideoActionEstimator: invalid subject box");
        const float left=std::clamp(subject.left,0.F,float(width-1));
        const float top=std::clamp(subject.top,0.F,float(height-1));
        const float right=std::clamp(subject.right,0.F,float(width-1));
        const float bottom=std::clamp(subject.bottom,0.F,float(height-1));
        if(right-left<1 || bottom-top<1)
            throw std::invalid_argument("VideoActionEstimator: empty clipped subject box");
        result.boxes.values.insert(result.boxes.values.end(),{0,
            std::clamp(left*scale,0.F,float(outWidth-1)),std::clamp(top*scale,0.F,float(outHeight-1)),
            std::clamp(right*scale,0.F,float(outWidth-1)),std::clamp(bottom*scale,0.F,float(outHeight-1))});
    }
    result.clip.shape={1,3,32,outHeight,outWidth};
    const std::size_t plane=static_cast<std::size_t>(outWidth)*outHeight;
    result.clip.values.resize(3*32*plane);
    struct Axis {int first,second;float weight;};
    const auto positions=[](int source,int output) {
        std::vector<Axis> axis;axis.reserve(output);
        for(int i=0;i<output;++i) {
            const float position=std::clamp((i+.5F)*float(source)/output-.5F,0.F,float(source-1));
            const int first=static_cast<int>(position);
            axis.push_back({first,std::min(first+1,source-1),position-first});
        }
        return axis;
    };
    const auto horizontal=positions(width,outWidth),vertical=positions(height,outHeight);
    for(int time=0;time<32;++time) {
        const auto index=static_cast<std::size_t>(time)*(frames.size()-1)/31;
        const auto* pixels=frames[index].rgba;
        for(int y=0;y<outHeight;++y)for(int x=0;x<outWidth;++x) {
            const auto row=vertical[y],column=horizontal[x];
            for(int channel=0;channel<3;++channel) {
                const auto read=[&](int sx,int sy) {return pixels[(static_cast<std::size_t>(sy)*width+sx)*4+channel]/255.F;};
                const float upper=read(column.first,row.first)*(1-column.weight)+read(column.second,row.first)*column.weight;
                const float lower=read(column.first,row.second)*(1-column.weight)+read(column.second,row.second)*column.weight;
                const float value=upper*(1-row.weight)+lower*row.weight;
                result.clip.values[(channel*32+time)*plane+static_cast<std::size_t>(y)*outWidth+x]=(value-.45F)/.225F;
            }
        }
    }
    return result;
}

VideoActionEstimator::VideoActionEstimator(std::string path,Provider provider,std::size_t threads)
    :modelPath_(std::move(path)),provider_(provider),intraOpThreads_(threads) {}

std::vector<std::array<double,80>> VideoActionEstimator::infer(std::span<const VideoActionFrame> frames,
                                                             std::span<const VideoActionSubject> subjects)
{
    auto prepared=prepareVideoActionInput(frames,subjects);
    const auto session=processSessionCache().getOrCreate(ModelRef::fromPath(modelPath_),
        {provider_,0,"ava-actions",intraOpThreads_});
    if(!session.ok)throw std::runtime_error(session.error);
    const auto spec=describeSession(session.handle);
    if(!spec.ok || spec.inputs.size()!=2 || spec.inputs[0].shape.size()!=5
        || spec.inputs[0].shape[0]!=1 || spec.inputs[0].shape[1]!=3 || spec.inputs[0].shape[2]!=32
        || spec.inputs[1].shape.size()!=2 || spec.inputs[1].shape[1]!=5)
        throw std::runtime_error("VideoActionEstimator: expected video and subject-box inputs");
    const std::array<FloatTensor,2> inputs{std::move(prepared.clip),std::move(prepared.boxes)};
    const auto output=runFloats(session.handle,inputs);
    if(!output.ok)throw std::runtime_error(output.error);
    if(output.outputs.size()!=1 || output.outputs[0].shape!=std::vector<std::int64_t>{static_cast<std::int64_t>(subjects.size()),80}
        || output.outputs[0].values.size()!=subjects.size()*80)
        throw std::runtime_error("VideoActionEstimator: expected one 80-action vector per subject");
    std::vector<std::array<double,80>> result(subjects.size());
    for(std::size_t subject=0;subject<subjects.size();++subject)for(int action=0;action<80;++action) {
        const float value=output.outputs[0].values[subject*80+action];
        if(!std::isfinite(value) || value<0 || value>1)
            throw std::runtime_error("VideoActionEstimator: malformed action score");
        result[subject][action]=value;
    }
    return result;
}
}
