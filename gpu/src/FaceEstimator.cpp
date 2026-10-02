#include "pfgpu/FaceEstimator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace pfgpu {
namespace {
// Opt-in stage timings only; normal analysis does not sample the clock here.
struct FaceTimings {
    using Clock = std::chrono::steady_clock;
    bool enabled;
    Clock::time_point started;
    double prepare = 0, detect = 0, post = 0, align = 0, recognize = 0;
    FaceTimings() : enabled([] { const auto* flag = std::getenv("PF_DEBUG_INFERENCE"); return flag && *flag && *flag != '0'; }())
    { if (enabled) started = Clock::now(); }
    Clock::time_point mark() const { return enabled ? Clock::now() : Clock::time_point{}; }
    void add(Clock::time_point from, double& stage) const
    { if (enabled) stage += std::chrono::duration<double, std::milli>(Clock::now() - from).count(); }
    ~FaceTimings()
    {
        if (!enabled) return;
        static thread_local std::size_t calls = 0;
        static thread_local double total = 0, preparation = 0, detection = 0, postprocess = 0, alignment = 0, recognition = 0;
        total += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        preparation += prepare; detection += detect; postprocess += post; alignment += align; recognition += recognize;
        if (++calls % 128 == 0)
            std::fprintf(stderr, "PF_DEBUG_INFERENCE face_calls=%zu total_ms=%.3f prepare_ms=%.3f detect_ms=%.3f post_ms=%.3f align_ms=%.3f recognize_ms=%.3f\n",
                         calls, total, preparation, detection, postprocess, alignment, recognition);
    }
};
struct Face {
    double x, y, w, h, score;
    std::array<std::pair<double, double>, 5> landmarks;
};

float pixel(const ReIdImage& image, double x, double y, int channel)
{
    if (!std::isfinite(x + y) || x < 0 || y < 0 || x > image.width - 1 || y > image.height - 1) return 0;
    const int ix = static_cast<int>(x), iy = static_cast<int>(y);
    const int nx = std::min(ix + 1, image.width - 1), ny = std::min(iy + 1, image.height - 1);
    const auto at = [&](int px, int py) { return image.rgba[(static_cast<std::size_t>(py) * image.width + px) * 4 + channel]; };
    const double dx = x - ix, dy = y - iy;
    return static_cast<float>((1-dy) * ((1-dx)*at(ix,iy) + dx*at(nx,iy))
        + dy*((1-dx)*at(ix,ny) + dx*at(nx,ny)));
}

double overlap(const Face& a, const Face& b)
{
    const double intersection = std::max(0.0, std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x))
        * std::max(0.0, std::min(a.y+a.h,b.y+b.h)-std::max(a.y,b.y));
    return intersection / std::max(1e-9, a.w*a.h+b.w*b.h-intersection);
}
}

FaceEstimator::FaceEstimator(std::string detector, std::string recognizer, Provider provider)
    : detector_(std::move(detector)), recognizer_(std::move(recognizer)), provider_(provider) {}

std::vector<float> FaceEstimator::infer(const ReIdImage& image, FaceRecognitionDiagnostics* diagnostics)
{
    if (diagnostics) *diagnostics = {};
    FaceTimings timings;
    auto stageStarted = timings.mark();
    if (!image.rgba || image.width <= 0 || image.height <= 0) return {};
    const double left = std::clamp<double>(image.left, 0, image.width-1);
    const double top = std::clamp<double>(image.top, 0, image.height-1);
    const double width = std::clamp<double>(image.right, left+1, image.width)-left;
    const double height = std::clamp<double>(image.bottom, top+1, image.height)-top;
    if (width < 32 || height < 32) return {};
    const auto detector = processSessionCache().getOrCreate(ModelRef::fromPath(detector_), {provider_,0,"face-detector",1});
    if (!detector.ok) throw std::runtime_error(detector.error);
    const auto spec = describeSession(detector.handle);
    if (!spec.ok || spec.input.shape.size() != 4)
        throw std::runtime_error("YuNet input must be NCHW");
    const int size = spec.input.shape[2] > 0 ? static_cast<int>(spec.input.shape[2]) : 320;
    if (size < 32 || size > 1024 || size % 32 != 0
        || (spec.input.shape[3] > 0 && spec.input.shape[3] != size))
        throw std::runtime_error("Unsupported YuNet input size");
    const double scale = size / std::max(width, height);
    FloatTensor input;
    input.shape = {1,3,size,size};
    input.values.assign(3*size*size, 0);
    // YuNet consumes unnormalised BGR; SFace below consumes RGB, also 0..255.
    for (int y=0; y<size && y<height*scale; ++y)
        for (int x=0; x<size && x<width*scale; ++x)
            for (int c=0; c<3; ++c)
                input.values[c*size*size+y*size+x] = pixel(image,left+(x+0.5)/scale-0.5,top+(y+0.5)/scale-0.5,2-c);
    timings.add(stageStarted, timings.prepare);
    stageStarted = timings.mark();
    // These small models use one CPU thread to avoid idle ORT pools competing
    // with pose inference. They share the configured provider/session lifecycle.
    const auto output = runFloat(detector.handle, input);
    timings.add(stageStarted, timings.detect);
    stageStarted = timings.mark();
    if (!output.ok) throw std::runtime_error(output.error);
    const auto tensor = [&](const std::string& name, std::size_t count) -> const std::vector<float>& {
        const auto it = std::find(output.outputNames.begin(), output.outputNames.end(), name);
        const auto index = static_cast<std::size_t>(it-output.outputNames.begin());
        if (index >= output.outputs.size() || output.outputs[index].values.size() != count)
            throw std::runtime_error("YuNet output contract mismatch: " + name);
        return output.outputs[index].values;
    };
    std::vector<Face> candidates;
    for (int stride : {8,16,32}) {
        const int cols = size/stride;
        const std::size_t count = cols*cols;
        const auto suffix = std::to_string(stride);
        const auto& cls = tensor("cls_"+suffix,count);
        const auto& obj = tensor("obj_"+suffix,count);
        const auto& box = tensor("bbox_"+suffix,count*4);
        const auto& kps = tensor("kps_"+suffix,count*10);
        for (std::size_t i=0; i<count; ++i) {
            const double score = std::sqrt(std::clamp(cls[i],0.0F,1.0F)*std::clamp(obj[i],0.0F,1.0F));
            if (!std::isfinite(score) || score<0.85) continue;
            const double cx = (i%cols+box[i*4])*stride/scale+left;
            const double cy = (i/cols+box[i*4+1])*stride/scale+top;
            const double w = std::exp(box[i*4+2])*stride/scale;
            const double h = std::exp(box[i*4+3])*stride/scale;
            if (!std::isfinite(cx+cy+w+h) || w<32 || h<32
                || cx<left || cx>left+width || cy<top || cy>top+height*0.7) continue;
            Face face{cx-w/2,cy-h/2,w,h,score,{}};
            bool finite = true;
            for (int k=0; k<5; ++k) {
                face.landmarks[k] = {(i%cols+kps[i*10+2*k])*stride/scale+left,
                                     (i/cols+kps[i*10+2*k+1])*stride/scale+top};
                finite = finite && std::isfinite(face.landmarks[k].first+face.landmarks[k].second);
            }
            if (finite) candidates.push_back(face);
        }
    }
    timings.add(stageStarted, timings.post);
    if (candidates.empty()) return {};
    stageStarted = timings.mark();
    std::sort(candidates.begin(),candidates.end(),[](const Face& a,const Face& b){return a.score>b.score;});
    const Face& face = candidates.front();
    // Ambiguous crops must not attach a bystander's face to a person's body.
    for (std::size_t i=1;i<candidates.size();++i)
        if (overlap(face,candidates[i])<0.3 && candidates[i].w*candidates[i].h>face.w*face.h*0.4) return {};
    if (std::hypot(face.landmarks[0].first-face.landmarks[1].first,
                   face.landmarks[0].second-face.landmarks[1].second)<10) return {};
    constexpr std::array<std::pair<double,double>,5> target = {{{38.2946,51.6963},{73.5318,51.5014},
        {56.0252,71.7366},{41.5493,92.3655},{70.7299,92.2041}}};
    // Least-squares, orientation-preserving similarity transform from the
    // canonical face to source coordinates; inverse mapping avoids holes.
    double sx=0,sy=0,tx=0,ty=0;
    for (int k=0;k<5;++k) {sx+=face.landmarks[k].first/5; sy+=face.landmarks[k].second/5; tx+=target[k].first/5; ty+=target[k].second/5;}
    double aa=0,bb=0,den=0;
    for (int k=0;k<5;++k) {
        const double x=target[k].first-tx,y=target[k].second-ty;
        const double u=face.landmarks[k].first-sx,v=face.landmarks[k].second-sy;
        aa+=x*u+y*v; bb+=x*v-y*u; den+=x*x+y*y;
    }
    aa/=den; bb/=den;
    FloatTensor aligned;
    aligned.shape={1,3,112,112}; aligned.values.resize(3*112*112);
    for (int y=0;y<112;++y) for (int x=0;x<112;++x) for (int c=0;c<3;++c)
        aligned.values[c*112*112+y*112+x]=pixel(image,aa*(x-tx)-bb*(y-ty)+sx,bb*(x-tx)+aa*(y-ty)+sy,c);
    timings.add(stageStarted, timings.align);
    stageStarted = timings.mark();
    const auto recognizer=processSessionCache().getOrCreate(ModelRef::fromPath(recognizer_),{provider_,0,"face-recognizer",1});
    if (!recognizer.ok) throw std::runtime_error(recognizer.error);
    const auto features=runFloat(recognizer.handle,aligned);
    timings.add(stageStarted, timings.recognize);
    if (!features.ok) throw std::runtime_error(features.error);
    if (features.outputs.size()!=1 || features.outputs[0].values.size()!=128)
        throw std::runtime_error("SFace output must contain 128 features");
    auto embedding=features.outputs[0].values;
    double norm=0;
    for (float value:embedding) {if (!std::isfinite(value)) return {}; norm+=value*value;}
    if (norm<1e-12) return {};
    for (auto& value:embedding) value=static_cast<float>(value/std::sqrt(norm));
    if (diagnostics) {
        diagnostics->accepted = true;
        diagnostics->detectorScore = face.score;
        for (int k = 0; k < 5; ++k) {
            diagnostics->landmarks[2*k] = face.landmarks[k].first;
            diagnostics->landmarks[2*k+1] = face.landmarks[k].second;
        }
        diagnostics->canonicalToSource = {aa, -bb, sx-aa*tx+bb*ty,
                                          bb, aa, sy-bb*tx-aa*ty};
    }
    return embedding;
}
}
