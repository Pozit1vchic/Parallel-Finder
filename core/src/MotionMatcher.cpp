#include "pfcore/MotionMatcher.hpp"
#include "pfcore/MotionIndex.hpp"
#include "pfcore/MovementClassifier.hpp"
#include "pfcore/PoseSupport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <numeric>
#include <set>
#include <atomic>
#include <map>
#include <tuple>
#include <optional>
#include <span>
#include <mutex>
extern "C" {
#include <libavutil/sha.h>
#include <libavutil/mem.h>
}

namespace pfcore {
namespace {

// Immutable after describe(). A frame participates in many DTW comparisons;
// its stationary/moving fact does not need a fresh scan for every pair.
struct Descriptor : std::vector<double> {
    double maximumVelocity = 0.0;
};

using NormalizedPose = std::vector<std::pair<double, double>>;

constexpr double kMinimumKeypointConfidence = 0.25;
constexpr std::size_t kMinimumComparableJoints = 6;

std::optional<std::array<std::uint8_t,32>> geometryFingerprint(
    const std::vector<MotionWindow>& windows,const std::function<bool()>& cancelled)
{
    auto* sha=av_sha_alloc();
    if(!sha)return {};
    const auto release=std::unique_ptr<AVSHA,decltype(&av_free)>(sha,&av_free);
    if(av_sha_init(sha,256)<0)return {};
    const auto value=[&](const auto& v) {
        av_sha_update(sha,reinterpret_cast<const std::uint8_t*>(&v),sizeof(v));
    };
    const auto sequence=[&](const auto& v) {
        value(v.size());
        if(!v.empty())av_sha_update(sha,reinterpret_cast<const std::uint8_t*>(v.data()),v.size()*sizeof(v[0]));
    };
    value(windows.size());
    for(const auto& w:windows) {
        if(cancelled && cancelled())return {};
        sequence(w.sourceId);value(w.trackId);value(w.sceneIndex);value(w.hasSceneIndex);
        value(w.staticFrameSet);value(w.sceneStartSeconds);value(w.sceneEndSeconds);
        value(w.frames.size());
        for(const auto& f:w.frames) {
            value(f.timestampSeconds);value(f.keypoints.size());
            for(const auto& p:f.keypoints){value(p.x);value(p.y);value(p.confidence);}
        }
        sequence(w.appearanceEmbedding);value(w.appearanceConfidence);
        sequence(w.faceEmbedding);value(w.faceConfidence);sequence(w.sceneContext);
        sequence(w.sceneView);value(w.sceneViewSampled);
        sequence(w.sceneSequence);sequence(w.sceneSequenceTimes);
    }
    std::array<std::uint8_t,32> result{};av_sha_final(sha,result.data());return result;
}

// Raw comparison outputs contain no classification labels or rank yet. Store
// their numeric facts without repeating long source-path strings per hypothesis.
struct CompactComparison {
    std::size_t left,right;
    std::array<double,15> values;
    bool appearance,face,head;
    explicit CompactComparison(const MotionMatch& m):left(m.leftIndex),right(m.rightIndex),
        values{m.similarity,m.dtwDistance,m.durationSeconds,m.leftStartSeconds,m.leftEndSeconds,
            m.rightStartSeconds,m.rightEndSeconds,m.leftSceneStartSeconds,m.leftSceneEndSeconds,
            m.rightSceneStartSeconds,m.rightSceneEndSeconds,m.rankScore,m.appearanceSimilarity,
            m.sceneSimilarity,m.unmirroredSimilarity},
        appearance(m.appearanceVerified),face(m.faceVerified),head(m.headOnlyComparison) {}
    MotionMatch restore(const std::vector<MotionWindow>& windows) const {
        MotionMatch m;m.leftIndex=left;m.rightIndex=right;
        m.leftSourceId=windows[left].sourceId;m.rightSourceId=windows[right].sourceId;
        m.similarity=values[0];m.dtwDistance=values[1];m.durationSeconds=values[2];
        m.leftStartSeconds=values[3];m.leftEndSeconds=values[4];m.rightStartSeconds=values[5];m.rightEndSeconds=values[6];
        m.leftSceneStartSeconds=values[7];m.leftSceneEndSeconds=values[8];
        m.rightSceneStartSeconds=values[9];m.rightSceneEndSeconds=values[10];m.rankScore=values[11];
        m.appearanceSimilarity=values[12];m.sceneSimilarity=values[13];m.unmirroredSimilarity=values[14];
        m.appearanceVerified=appearance;m.faceVerified=face;m.headOnlyComparison=head;return m;
    }
};

bool validPoint(const std::pair<double, double>& point)
{
    return std::isfinite(point.first) && std::isfinite(point.second);
}

bool measuredIdentityCompatible(const MotionWindow& w,double start,double end,bool portrait)
{
    if (w.measuredFaces.empty()) return true;
    // Identity inherited from a wider shot must not label a cropped face as
    // a body pose. Inspect the limbs that are actually visible in this pair.
    if (!portrait) {
        constexpr std::size_t chains[4][3]={{5,7,9},{6,8,10},{11,13,15},{12,14,16}};
        std::size_t sampled=0,body=0;
        for (const auto& frame:w.frames) {
            if(frame.timestampSeconds<start-.01 || frame.timestampSeconds>end+.01)continue;
            if(frame.keypoints.size()!=17)continue;
            ++sampled;
            if(std::any_of(std::begin(chains),std::end(chains),[&](const auto& chain) {
                return std::all_of(std::begin(chain),std::end(chain),[&](std::size_t i) {
                    const auto& p=frame.keypoints[i];return p.confidence>=.25 && std::isfinite(p.x+p.y);
                });
            }))++body;
        }
        // A cropped head must not borrow a body prototype and masquerade as
        // an observed limb pose or motion. Real body/back views keep their ReID proof;
        // face-only pictures require measured face identity on this interval.
        portrait=sampled && body*2<sampled;
    }
    std::set<double> supported,inspected;
    for (const auto& face:w.measuredFaces) {
        if (face.timestampSeconds<start-.01 || face.timestampSeconds>end+.01) continue;
        inspected.insert(face.timestampSeconds);
        if (!face.observed) continue;
        if (!std::isfinite(face.similarity)
            || (face.relativeEyeSpan>=.02 && face.similarity<=.25)) return false;
        if (face.similarity>=.363 && face.relativeEyeSpan>=.02) supported.insert(face.timestampSeconds);
    }
    return !portrait || inspected.size()<3 || supported.size()>=2;
}

double appearanceCosine(const std::vector<float>& left,
                         const std::vector<float>& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!std::isfinite(left[index]) || !std::isfinite(right[index])) return 0.0;
        dot += static_cast<double>(left[index]) * right[index];
        leftNorm += static_cast<double>(left[index]) * left[index];
        rightNorm += static_cast<double>(right[index]) * right[index];
    }
    if (leftNorm <= 1e-12 || rightNorm <= 1e-12) return 0.0;
    return std::clamp(dot / std::sqrt(leftNorm * rightNorm), -1.0, 1.0);
}

double sceneContextSimilarity(const MotionWindow& left, const MotionWindow& right)
{
    if (left.sceneContext.empty() || right.sceneContext.empty()
        || left.sceneContext.size() != right.sceneContext.size()) return 0.5;
    double context = 0.0;
    for (std::size_t i = 0; i < left.sceneContext.size(); ++i)
        context += std::sqrt(std::max(0.0F, left.sceneContext[i])
                             * std::max(0.0F, right.sceneContext[i]));
    return std::clamp(context, 0.0, 1.0);
}

bool sameCameraView(const MotionWindow& left, const MotionWindow& right)
{
    if (left.sceneView.size() != 432
        || right.sceneView.size() != left.sceneView.size()) return false;
    if (!left.sceneContext.empty() && !right.sceneContext.empty()
        && sceneContextSimilarity(left, right) < .90) return false;
    for (std::size_t i = 0; i < left.sceneView.size(); ++i) {
        const double a = left.sceneView[i], b = right.sceneView[i];
        if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b < 0 || a > 1 || b > 1) return false;
    }
    // A returning camera can reframe a portrait slightly. Align by at most
    // one measured grid column (6.25% of frame width), without widening the
    // colour-error threshold or accepting a different background layout.
    for (int shift = -1; shift <= 1; ++shift) {
        double difference = 0, weight = 0;
        for (int row = 0; row < 9; ++row) for (int column = std::max(0, -shift);
             column < std::min(16, 16 - shift); ++column) {
            // Weight both aligned columns so exchanging A/B cannot change
            // camera equality at the threshold after a small reframe.
            const double cellWeight = ((column < 5 || column >= 11 ? 2.0 : 1.0)
                + (column + shift < 5 || column + shift >= 11 ? 2.0 : 1.0)) / 2.0;
            for (int channel = 0; channel < 3; ++channel) {
                difference += cellWeight * std::abs(left.sceneView[(row*16+column)*3+channel]
                    - right.sceneView[(row*16+column+shift)*3+channel]);
                weight += cellWeight;
            }
        }
        if (difference / weight <= .035) return true;
    }
    return false;
}

struct FootageFrameStats {
    std::array<double,3> mean{},variance{};
    std::array<double,12> quadrants{};
};
// Search-local, immutable source pixels; no global cache can outlive a window.
struct FootageStatsCache {
    std::mutex mutex;
    std::map<std::tuple<const std::vector<float>*,int,bool,bool>,std::vector<FootageFrameStats>> entries;
};

bool copiedScene(const MotionWindow& a,const MotionWindow& b,bool cameraOnly=false,
                 const std::function<bool()>& cancelled = {}, FootageStatsCache* statisticsCache=nullptr)
{
    // A returning camera is just as redundant within one film as across files.
    // Keep the stricter copied-footage policy separate from portrait filtering.
    const bool sameSource=a.sourceId==b.sourceId;
    if ((sameSource && (!cameraOnly || a.sceneIndex==b.sceneIndex)) || !a.hasSceneIndex || !b.hasSceneIndex
        || a.sceneSequence.size()<1296 || b.sceneSequence.size()<1296
        || a.sceneSequence.size()%432 || b.sceneSequence.size()%432) return false;
    // Individual portraits in an already matching visual setting need more
    // novelty than a slight change of head angle or framing. This wider gate
    // is ONLY for redundant head views, never proof that body footage is a
    // copy and never a shared-shot quota. Distinct settings keep strict bounds.
    // Returning shots in one film can change colour histograms with framing:
    // the measured Soldier 38:18/38:45 control has only 90% scene similarity.
    // This gate still requires agreement of three measured frame layouts.
    const bool recurringSetting=cameraOnly && sceneContextSimilarity(a,b)>=(sameSource?.88:.93);
    const double meanLimit=!cameraOnly?.018:recurringSetting?.095:.055;
    const double coarseLimit=!cameraOnly?.035:recurringSetting?.14:.08;
    const double localLimit=!cameraOnly?.085:recurringSetting?.45:.20;
    // Every independently sampled frame must agree, in order. A repeated
    // background or one coincidentally similar portrait is not copied video.
    using FrameStats=FootageFrameStats;
    const auto statistics=[](const std::vector<float>& pixels) {
        std::vector<FrameStats> result(pixels.size()/432);
        for (std::size_t frame=0;frame<result.size();++frame) {
            auto& stats=result[frame];
            for (std::size_t i=0;i<432;++i) {
                const double v=pixels[frame*432+i];stats.mean[i%3]+=v/144;stats.variance[i%3]+=v*v/144;
                const auto q=(((i/48)>=4)*2+(((i/3)%16)>=8))*3+i%3;
                stats.quadrants[q]+=v/144;
            }
            for (int c=0;c<3;++c)stats.variance[c]-=stats.mean[c]*stats.mean[c];
        }
        return result;
    };

    const auto shiftedStatistics = [](const std::vector<float>& sequence, int shift, bool mirror, bool right) {
        std::vector<FrameStats> result(sequence.size()/432);
        const double pixels=(16-std::abs(shift))*9;
        for (std::size_t f=0; f<result.size(); ++f) {
            auto& stats=result[f];
            for (int row=0;row<9;++row) for (int x=0;x<16;++x) {
                if(x+shift<0 || x+shift>=16)continue;
                const int column=right ? (mirror?15-x-shift:x+shift) : x;
                for(int c=0;c<3;++c) {
                    const double v=sequence[f*432+(row*16+column)*3+c];
                    stats.mean[c]+=v/pixels;stats.variance[c]+=v*v/pixels;
                    stats.quadrants[((row>=4)*2+(x>=8))*3+c]+=v/pixels;
                }
            }
            for(int c=0;c<3;++c)stats.variance[c]-=stats.mean[c]*stats.mean[c];
        }
        return result;
    };
    FootageStatsCache localCache;
    auto& cache=statisticsCache ? *statisticsCache : localCache;
    const auto cachedStatistics=[&](const std::vector<float>& sequence,int shift,bool mirror,bool right)
        -> const std::vector<FrameStats>& {
        // Mirroring does not change whole-frame means or variances.
        const auto key=std::make_tuple(&sequence,shift,right && mirror,right && (shift || mirror));
        // Nodes and their completed statistics remain immutable until all
        // camera workers join; returning a reference is safe after unlocking.
        std::lock_guard lock(cache.mutex);
        auto [entry,inserted]=cache.entries.try_emplace(key);
        if(inserted) {
            entry->second=shift ? shiftedStatistics(sequence,shift,mirror,right) : statistics(sequence);
            if(!shift && right && mirror)for(auto& frame:entry->second)
                for(int row=0;row<2;++row)for(int c=0;c<3;++c)
                    std::swap(frame.quadrants[row*6+c],frame.quadrants[row*6+3+c]);
        }
        return entry->second;
    };
    const std::vector<FrameStats>* shiftedA=nullptr;
    const std::vector<FrameStats>* shiftedB=nullptr;
    const auto equalFrame=[&](std::size_t left,std::size_t right,bool mirror,int shift) {
        const auto& sa=(*shiftedA)[left];
        const auto& sb=(*shiftedB)[right];
        const auto valid=[&](std::size_t i) {const int x=(i/3)%16;return x+shift>=0 && x+shift<16;};
        const auto indexB=[&](std::size_t i) {
            const int x=(i/3)%16+shift;
            return ((i/48)*16+(mirror?15-x:x))*3+i%3;
        };
        const double pixels=(16-std::abs(shift))*9;
        if (std::accumulate(sa.variance.begin(),sa.variance.end(),0.0)/3<.0004
            || std::accumulate(sb.variance.begin(),sb.variance.end(),0.0)/3<.0004) return false;
        std::array<double,3> gain{},bias{},normalizer{};
        // SCPs often change exposure/colour while copying the same pixels.
        // Fit only a bounded per-channel affine transform; local layout and
        // three successive frames must still agree independently.
        for (int c=0;c<3;++c) {
            gain[c]=std::clamp(std::sqrt(std::max(0.0,sa.variance[c])/std::max(1e-9,sb.variance[c])),cameraOnly?2.0/3:.80,cameraOnly?1.5:1.25);
            normalizer[c]=std::sqrt(gain[c]);
            bias[c]=sa.mean[c]-gain[c]*sb.mean[c];if (std::abs(bias[c])/normalizer[c]>(cameraOnly?.15:.10)) return false;
        }
        // Jensen's inequality gives a necessary lower bound on the SAME
        // absolute pixel residual. Reject impossible layouts before the
        // fine-grid loop; never reject a frame that can satisfy its limit.
        const int begin=std::max(0,-shift),end=std::min(16,16-shift);
        const double leftColumns=std::max(0,std::min(8,end)-begin);
        const double rightColumns=std::max(0,end-std::max(8,begin));
        const std::array<double,4> weights={leftColumns*4/pixels,rightColumns*4/pixels,
            leftColumns*5/pixels,rightColumns*5/pixels};
        double residualBound=0;
        for(int q=0;q<4;++q)for(int c=0;c<3;++c)
            residualBound+=std::abs(sa.quadrants[q*3+c]-gain[c]*sb.quadrants[q*3+c]
                -bias[c]*weights[q])/normalizer[c];
        if(residualBound>meanLimit*3+1e-9)return false;
        const auto residual=[&](std::size_t i) {
            return std::abs(a.sceneSequence[left*432+i]-(gain[i%3]*b.sceneSequence[right*432+indexB(i)]+bias[i%3]))
                /normalizer[i%3];
        };
        double coarse=0;std::size_t coarseSamples=0;
        for(const int column:{2,6,10,14})if(valid(column*3))coarseSamples+=9;
        for (const int row:{2,4,6}) {
            for (const int column:{2,6,10,14})
                for (int c=0;c<3;++c)if(valid((row*16+column)*3+c))coarse+=residual((row*16+column)*3+c);
            // Non-negative residuals can only increase. Dividing by the FINAL
            // sample count preserves the original floating-point threshold,
            // while rejecting impossible frame pairs before more pixel work.
            if(coarse/coarseSamples>coarseLimit)return false;
        }
        double error=0;std::array<double,144> local{};
        for (std::size_t i=0;i<432;++i)if(valid(i)) {
            const auto u=a.sceneSequence[left*432+i],v=b.sceneSequence[right*432+indexB(i)];
            if (!std::isfinite(u) || !std::isfinite(v) || u<0 || u>1 || v<0 || v>1) return false;
            const double difference=residual(i);error+=difference;local[i/3]+=difference/3;
            if (error>meanLimit*pixels*3) return false;
        }
        std::partial_sort(local.begin(),local.begin()+8,local.end(),std::greater<double>());
        // Keep the whole-frame bound strict, while allowing a small moving
        // foreground region to differ at the independently measured PTS.
        // A 4 fps thumbnail in two differently trimmed edits can straddle a
        // head/hand movement even when their background and pixels agree.
        if(std::accumulate(local.begin(),local.begin()+8,0.0)/8>localLimit)return false;
        if(cameraOnly) {
            double product=0,normA=0,normB=0;
            for(std::size_t i=0;i<432;++i)if(valid(i)) {
                const double u=a.sceneSequence[left*432+i]-sa.mean[i%3];
                const double v=b.sceneSequence[right*432+indexB(i)]-sb.mean[i%3];
                product+=u*v;normA+=u*u;normB+=v*v;
            }
            if(product/std::sqrt(std::max(1e-12,normA*normB))<(recurringSetting?.25:.90))return false;
        }
        return true;
    };
    const auto countA=a.sceneSequence.size()/432,countB=b.sceneSequence.size()/432;
    for (const bool mirror:{false,true}) for (const int shift:{0,-1,1,-2,2,-3,3}) {
      if(shift && !cameraOnly)continue;
      if (cancelled && cancelled()) return false;
      shiftedA=&cachedStatistics(a.sceneSequence,shift,mirror,false);
      shiftedB=&cachedStatistics(b.sceneSequence,shift,mirror,true);
      for (std::size_t i=0;i+2<countA;++i) {
        if (cancelled && cancelled()) return false;
        for (std::size_t j=0;j+2<countB;++j) {
        if (!equalFrame(i,j,mirror,shift) || !equalFrame(i+1,j+1,mirror,shift) || !equalFrame(i+2,j+2,mirror,shift)) continue;
        if (a.sceneSequenceTimes.size()==countA && b.sceneSequenceTimes.size()==countB) {
            const double spanA=a.sceneSequenceTimes[i+2]-a.sceneSequenceTimes[i];
            const double spanB=b.sceneSequenceTimes[j+2]-b.sceneSequenceTimes[j];
            if (spanA<.30 || spanB<.30 || std::abs(spanA-spanB)>.10
                || a.sceneSequenceTimes[i+1]<=a.sceneSequenceTimes[i]
                || a.sceneSequenceTimes[i+2]<=a.sceneSequenceTimes[i+1]
                || b.sceneSequenceTimes[j+1]<=b.sceneSequenceTimes[j]
                || b.sceneSequenceTimes[j+2]<=b.sceneSequenceTimes[j+1]) continue;
        }
        return true;
        }
      }
    }
    return false;
}

// An exact range index for copied footage, not an ANN heuristic. A copied
// frame's mean absolute residual is <= .018 after bounded gain/bias. Balanced
// signed projections cancel bias, so |a - gain*b| <= .018*sqrt(gain).
// Intersect conservative intervals for all three consecutive observations.
// The expensive pixel verifier remains the final authority, including mirrors.
std::vector<std::uint64_t> copiedFootageCandidates(const std::vector<MotionWindow>& windows,
    const std::vector<std::size_t>& representatives,const MotionSearchControl& control)
{
    std::unordered_set<std::string> sources;
    for(const auto index:representatives)if(index!=windows.size())sources.insert(windows[index].sourceId);
    if(sources.size()<2)return {}; // strict footage copies apply only across sources
    struct Signature {std::size_t shot;std::array<double,27> features{};};
    std::vector<Signature> signatures;
    for(std::size_t shot=0;shot<representatives.size();++shot) {
        if(control.cancelled && control.cancelled())return {};
        const auto index=representatives[shot];if(index==windows.size())continue;
        const auto& w=windows[index];
        if(!w.hasSceneIndex || w.sceneSequence.size()<1296 || w.sceneSequence.size()%432)continue;
        std::vector<std::array<double,9>> frames(w.sceneSequence.size()/432);
        for(std::size_t f=0;f<frames.size();++f) {
            for(int row=0;row<9;++row)for(int x=0;x<16;++x)for(int c=0;c<3;++c) {
                const double value=w.sceneSequence[f*432+(row*16+x)*3+c]/432.0;
                frames[f][c]+=(x<8?1:-1)*value;
                frames[f][3+c]+=(row<4?1:row>4?-1:0)*value;
                frames[f][6+c]+=((row+x)%2?1:-1)*value;
            }
        }
        for(std::size_t f=0;f+2<frames.size();++f) {
            Signature sig;sig.shot=shot;
            for(int k=0;k<3;++k)std::copy(frames[f+k].begin(),frames[f+k].end(),sig.features.begin()+k*9);
            if(std::all_of(sig.features.begin(),sig.features.end(),[](double v){return std::isfinite(v);}))
                signatures.push_back(sig);
        }
    }
    std::array<std::vector<std::size_t>,27> orders;
    for(std::size_t d=0;d<orders.size();++d) {
        if(control.cancelled && control.cancelled())return {};
        auto& order=orders[d];order.resize(signatures.size());std::iota(order.begin(),order.end(),0);
        std::sort(order.begin(),order.end(),[&](auto a,auto b) {
            const auto x=signatures[a].features[d],y=signatures[b].features[d];return x==y ? a<b : x<y;
        });
    }
    std::unordered_set<std::uint64_t> pairs;
    constexpr double margin=.018/0.8944271909999159+1e-6; // sqrt(minimum strict gain)
    for(std::size_t i=0;i<signatures.size();++i) {
        if(control.cancelled && control.cancelled())return {};
        if(control.progress && i%128==0)control.progress(MotionSearchStage::Footage,i,signatures.size());
        const auto& a=signatures[i];
        for(const bool mirror:{false,true}) {
            std::array<double,27> lower{},upper{};
            std::size_t best=0,begin=0,end=signatures.size();
            for(std::size_t d=0;d<27;++d) {
                const bool flip=mirror && (d%9<3 || d%9>=6);
                const double value=a.features[d]*(flip?-1:1);
                lower[d]=std::min(value/.80,value/1.25)-margin;
                upper[d]=std::max(value/.80,value/1.25)+margin;
                const auto& order=orders[d];
                const auto lo=std::lower_bound(order.begin(),order.end(),lower[d],[&](auto id,double v){return signatures[id].features[d]<v;});
                const auto hi=std::upper_bound(lo,order.end(),upper[d],[&](double v,auto id){return v<signatures[id].features[d];});
                if(static_cast<std::size_t>(hi-lo)<end-begin) {best=d;begin=lo-order.begin();end=hi-order.begin();}
                if(begin==end)break;
            }
            for(std::size_t entry=begin;entry<end;++entry) {
                if(entry%256==0 && control.cancelled && control.cancelled())return {};
                const auto& b=signatures[orders[best][entry]];
                if(a.shot>=b.shot || windows[representatives[a.shot]].sourceId==windows[representatives[b.shot]].sourceId)continue;
                const auto key=(static_cast<std::uint64_t>(a.shot)<<32U)|b.shot;
                if(pairs.contains(key))continue;
                bool possible=true;
                for(std::size_t d=0;d<27;++d)if(b.features[d]<lower[d] || b.features[d]>upper[d]) {possible=false;break;}
                if(possible)pairs.insert(key);
            }
        }
    }
    std::vector<std::uint64_t> result(pairs.begin(),pairs.end());std::sort(result.begin(),result.end());return result;
}

// Tracker IDs are local to one source and scene cuts can restart a tracker.
// A different ID is therefore not identity proof in either direction; it
// needs multi-crop body-ReID confirmation before the temporal matcher sees it.
bool differentTrackSegment(const MotionWindow& left, const MotionWindow& right)
{
    if (left.trackId != right.trackId) return true;
    return left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex != right.sceneIndex;
}

struct IdentityEvidence { bool available = false; bool verified = false; bool face = false; double score = 0; };

// Overlapping motion/static chunks retain the same identity prototypes.
// Intern only exactly equal usable descriptors, not tracks or approximate
// identities. Provenance, gaps, posture and shot quotas still use window IDs.
std::vector<std::size_t> identityPrototypeIds(const std::vector<MotionWindow>& windows,
                                            double minimumEvidence)
{
    struct Prototype { std::size_t window; bool face; bool body; };
    std::vector<Prototype> prototypes;
    std::unordered_map<std::size_t, std::vector<std::size_t>> buckets;
    std::vector<std::size_t> ids;
    ids.reserve(windows.size());
    const auto combine = [](std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    };
    const auto hashVector = [&](std::size_t& hash, const std::vector<float>& vector) {
        combine(hash, vector.size());
        for (const float value : vector) combine(hash, std::hash<float>{}(value));
    };
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& window = windows[i];
        const bool face = !window.faceEmbedding.empty() && window.faceConfidence >= minimumEvidence;
        const bool body = !window.appearanceEmbedding.empty() && window.appearanceConfidence >= minimumEvidence;
        std::size_t hash = static_cast<std::size_t>(face) | (static_cast<std::size_t>(body) << 1U);
        if (face) hashVector(hash, window.faceEmbedding);
        if (body) hashVector(hash, window.appearanceEmbedding);
        auto& bucket = buckets[hash];
        const auto found = std::find_if(bucket.begin(), bucket.end(), [&](std::size_t id) {
            const auto& prototype = prototypes[id];
            const auto& previous = windows[prototype.window];
            return prototype.face == face && prototype.body == body
                && (!face || previous.faceEmbedding == window.faceEmbedding)
                && (!body || previous.appearanceEmbedding == window.appearanceEmbedding);
        });
        if (found != bucket.end()) ids.push_back(*found);
        else {
            ids.push_back(prototypes.size());
            bucket.push_back(prototypes.size());
            prototypes.push_back({i, face, body});
        }
    }
    return ids;
}

bool observedBody(const MotionWindow& window, double confidence = kMinimumKeypointConfidence)
{
    constexpr std::size_t chains[4][3] = {{5, 7, 9}, {6, 8, 10}, {11, 13, 15}, {12, 14, 16}};
    std::size_t supported = 0;
    for (const auto& frame : window.frames) {
        if (frame.keypoints.size() != 17) return true; // other skeleton contracts
        if (std::any_of(std::begin(chains), std::end(chains), [&](const auto& chain) {
            return std::all_of(std::begin(chain), std::end(chain), [&](std::size_t i) {
                const auto& p = frame.keypoints[i];
                return p.confidence >= confidence
                    && std::isfinite(p.x) && std::isfinite(p.y);
            });
        })) ++supported;
    }
    return !window.frames.empty() && supported * 2 >= window.frames.size();
}

IdentityEvidence identityEvidence(const MotionWindow& left, const MotionWindow& right,
                                  const MotionMatcherParams& params)
{
    const bool face = !left.faceEmbedding.empty() && !right.faceEmbedding.empty()
        && left.faceConfidence >= params.minAppearanceEvidence
        && right.faceConfidence >= params.minAppearanceEvidence;
    if (face) {
        const double score = appearanceCosine(left.faceEmbedding, right.faceEmbedding);
        // Reliable face disagreement vetoes body resemblance, even when
        // uniforms/clothing make body descriptors nearly identical.
        return {true, score >= params.minFaceSimilarity, true, score};
    }
    const bool body = !left.appearanceEmbedding.empty() && !right.appearanceEmbedding.empty()
        && left.appearanceConfidence >= params.minAppearanceEvidence
        && right.appearanceConfidence >= params.minAppearanceEvidence;
    const double score = body ? appearanceCosine(left.appearanceEmbedding, right.appearanceEmbedding) : 0;
    return {body, body && score >= params.minAppearanceSimilarity, false, score};
}

bool separateVerifiedShots(const MotionWindow& left, const MotionWindow& right,
                           const MotionMatcherParams& params, const IdentityEvidence* knownIdentity = nullptr)
{
    if (left.sourceId != right.sourceId || !left.hasSceneIndex || !right.hasSceneIndex
        || left.sceneIndex == right.sceneIndex) return false;
    const auto validBounds = [](const MotionWindow& window) {
        return !window.frames.empty() && std::isfinite(window.sceneStartSeconds)
            && std::isfinite(window.sceneEndSeconds) && window.sceneStartSeconds >= 0.0
            && window.sceneEndSeconds > window.sceneStartSeconds
            && window.frames.front().timestampSeconds >= window.sceneStartSeconds
            && window.frames.back().timestampSeconds < window.sceneEndSeconds;
    };
    return validBounds(left) && validBounds(right)
        && (left.sceneEndSeconds <= right.sceneStartSeconds
            || right.sceneEndSeconds <= left.sceneStartSeconds)
        && (knownIdentity ? *knownIdentity : identityEvidence(left, right, params)).verified;
}

std::vector<NormalizedPose> normalizePoses(const MotionWindow& window,
                                           bool normalizeSize, bool torsoLength = false)
{
    std::vector<NormalizedPose> normalized;
    if(window.frames.size()>MotionMatcherParams::maximumWindowFrames)return normalized;
    normalized.reserve(window.frames.size());
    // COCO pose models share shoulder indices 5/6. A centre computed from
    // whichever joints happen to be visible jumps when a wrist disappears.
    // Anchor these models to the torso and use one robust scale per window.
    std::vector<double> torsoScales;
    for (const auto& frame : window.frames) {
        if (frame.keypoints.size() != 17) continue;
        const auto& a = frame.keypoints[5];
        const auto& b = frame.keypoints[6];
        if (a.confidence >= kMinimumKeypointConfidence
            && b.confidence >= kMinimumKeypointConfidence) {
            const auto& l=frame.keypoints[11];const auto& r=frame.keypoints[12];
            if(torsoLength && (l.confidence<.5 || r.confidence<.5 || a.confidence<.5 || b.confidence<.5))continue;
            const double scale = torsoLength
                ? std::hypot((a.x+b.x-l.x-r.x)*.5,(a.y+b.y-l.y-r.y)*.5)
                : std::hypot(a.x - b.x, a.y - b.y);
            if (std::isfinite(scale) && scale > 1e-6) torsoScales.push_back(scale);
        }
    }
    double torsoScale = 0.0;
    if(torsoLength && torsoScales.size()*4 < window.frames.size()*3)return {};
    if (torsoScales.size() * 2 >= window.frames.size() && !torsoScales.empty()) {
        const auto middle = torsoScales.begin() + torsoScales.size() / 2;
        std::nth_element(torsoScales.begin(), middle, torsoScales.end());
        torsoScale = *middle;
    }
    for (const PoseFrame& frame : window.frames) {
        if (frame.keypoints.empty()) {
            normalized.emplace_back();
            continue;
        }
        double cx = 0.0, cy = 0.0, weight = 0.0;
        for (const auto& point : frame.keypoints) {
            if (!(point.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(point.x) || !std::isfinite(point.y)) continue;
            const double w = std::max(0.0, point.confidence);
            cx += point.x * w;
            cy += point.y * w;
            weight += w;
        }
        // A crop with no reliable joints cannot establish pose similarity.
        // Preserve it as invalid instead of turning zero-confidence values
        // into a plausible artificial skeleton.
        if (weight <= 1e-9) {
            normalized.emplace_back();
            continue;
        }
        cx /= weight;
        cy /= weight;
        if (torsoScale > 0.0 && frame.keypoints.size() == 17) {
            const auto& a = frame.keypoints[5];
            const auto& b = frame.keypoints[6];
            if (!(a.confidence >= kMinimumKeypointConfidence)
                || !(b.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(a.x + a.y + b.x + b.y)) {
                normalized.emplace_back();
                continue;
            }
            cx = (a.x + b.x) * 0.5;
            cy = (a.y + b.y) * 0.5;
        }
        double scale = 1.0;
        if (normalizeSize) {
            scale = 0.0;
            for (const auto& point : frame.keypoints) {
                if (point.confidence < kMinimumKeypointConfidence) continue;
                scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
            }
            if (scale <= 1e-9) scale = 1.0;
            if (torsoScale > 0.0) scale = torsoScale;
        }
        NormalizedPose pose;
        pose.reserve(frame.keypoints.size());
        for (const auto& point : frame.keypoints) {
            if (!(point.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(point.x) || !std::isfinite(point.y)) {
                const double invalid = std::numeric_limits<double>::quiet_NaN();
                pose.emplace_back(invalid, invalid);
            } else {
                pose.emplace_back((point.x - cx) / scale, (point.y - cy) / scale);
            }
        }
        normalized.push_back(std::move(pose));
    }
    return normalized;
}

struct MotionActivity {
    double meanDelta = 0.0;
    double activeTransitionRatio = 0.0;
    double trajectoryRange = 0.0;
};

double activeJointMean(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end(), std::greater<double>());
    // A head turn or one moving arm should not be diluted by stationary
    // hips/legs. Still require a group of joints rather than one outlier.
    const auto count = std::min(values.size(), std::max<std::size_t>(3,
        (values.size() + 3) / 4));
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) sum += values[i];
    return sum / count;
}

struct FrameRoot {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

FrameRoot frameRoot(const PoseFrame& frame)
{
    if (frame.keypoints.empty()) return {};
    if (frame.keypoints.size() == 17) {
        const auto& a = frame.keypoints[5];
        const auto& b = frame.keypoints[6];
        if (a.confidence >= kMinimumKeypointConfidence
            && b.confidence >= kMinimumKeypointConfidence
            && std::isfinite(a.x + a.y + b.x + b.y))
            return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5,
                    std::max(1e-6, std::hypot(a.x - b.x, a.y - b.y))};
    }
    double cx = 0.0;
    double cy = 0.0;
    double weight = 0.0;
    for (const auto& point : frame.keypoints) {
        if (point.confidence < kMinimumKeypointConfidence) continue;
        const double confidence = std::max(0.0, point.confidence);
        cx += point.x * confidence;
        cy += point.y * confidence;
        weight += confidence;
    }
    if (weight <= 1e-9) return {};
    cx /= weight;
    cy /= weight;
    double scale = 0.0;
    for (const auto& point : frame.keypoints) {
        if (point.confidence < kMinimumKeypointConfidence) continue;
        scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
    }
    return {cx, cy, std::max(scale, 1e-6)};
}

MotionActivity motionActivity(const MotionWindow& window,
                              const std::vector<NormalizedPose>& poses)
{
    if (poses.size() < 2) return {};
    double total = 0.0;
    std::size_t transitions = 0;
    std::size_t activeTransitions = 0;
    std::vector<double> ranges;
    if (!poses.empty() && !poses.front().empty()) {
        const std::size_t pointCount = poses.front().size();
        for (std::size_t point = 0; point < pointCount; ++point) {
            bool hasPoint = false;
            double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
            for (const auto& pose : poses) {
                if (pose.size() <= point || !validPoint(pose[point])) continue;
                if (!hasPoint) {
                    minX = maxX = pose[point].first;
                    minY = maxY = pose[point].second;
                    hasPoint = true;
                    continue;
                }
                minX = std::min(minX, pose[point].first);
                maxX = std::max(maxX, pose[point].first);
                minY = std::min(minY, pose[point].second);
                maxY = std::max(maxY, pose[point].second);
            }
            if (!hasPoint) continue;
            ranges.push_back(std::hypot(maxX - minX, maxY - minY));
        }
    }
    // A pose normalizer intentionally removes translation and scale. Keep a
    // separate relative root trajectory for the motion gate so a person
    // walking through the frame is not mistaken for a static pose.
    const FrameRoot baseRoot = frameRoot(window.frames.front());
    double rootMinX = 0.0, rootMaxX = 0.0, rootMinY = 0.0, rootMaxY = 0.0;
    double rootMinScale = 0.0, rootMaxScale = 0.0;
    if (!window.frames.empty()) {
        const double baseScale = std::max(baseRoot.scale, 1e-6);
        for (const auto& frame : window.frames) {
            const FrameRoot root = frameRoot(frame);
            const double relativeX = (root.x - baseRoot.x) / baseScale;
            const double relativeY = (root.y - baseRoot.y) / baseScale;
            const double relativeScale = std::log(std::max(root.scale, 1e-6) / baseScale);
            rootMinX = std::min(rootMinX, relativeX);
            rootMaxX = std::max(rootMaxX, relativeX);
            rootMinY = std::min(rootMinY, relativeY);
            rootMaxY = std::max(rootMaxY, relativeY);
            rootMinScale = std::min(rootMinScale, relativeScale);
            rootMaxScale = std::max(rootMaxScale, relativeScale);
        }
        const double rootRange = std::hypot(rootMaxX - rootMinX, rootMaxY - rootMinY)
            + 0.5 * std::abs(rootMaxScale - rootMinScale);
        ranges.push_back(rootRange);
    }
    for (std::size_t frame = 1; frame < poses.size(); ++frame) {
        const auto& previous = poses[frame - 1];
        const auto& current = poses[frame];
        if (previous.empty() || current.empty() || previous.size() != current.size()) continue;
        double delta = 0.0;
        std::size_t valid = 0;
        for (std::size_t point = 0; point < current.size(); ++point) {
            if (window.frames[frame - 1].keypoints[point].confidence < kMinimumKeypointConfidence
                || window.frames[frame].keypoints[point].confidence < kMinimumKeypointConfidence
                || !validPoint(previous[point]) || !validPoint(current[point])) {
                continue;
            }
            delta += std::hypot(current[point].first - previous[point].first,
                                current[point].second - previous[point].second);
            ++valid;
        }
        if (valid == 0) continue;
        // Normalize the requested sum by the number of observed joints.  This
        // is the same delta-K signal, without making the threshold model-size
        // dependent.
        const FrameRoot previousRoot = frameRoot(window.frames[frame - 1]);
        const FrameRoot currentRoot = frameRoot(window.frames[frame]);
        const double rootScale = std::max({baseRoot.scale, previousRoot.scale,
                                           currentRoot.scale, 1e-6});
        const double rootDelta = std::hypot(currentRoot.x - previousRoot.x,
                                            currentRoot.y - previousRoot.y) / rootScale
            + 0.5 * std::abs(std::log(std::max(currentRoot.scale, 1e-6)
                                      / std::max(previousRoot.scale, 1e-6)));
        const double mean = delta / static_cast<double>(valid) + rootDelta;
        total += mean;
        // Detector jitter is normally a few thousandths of a normalized
        // body unit. Require materially larger transitions and a sustained
        // ratio of them before calling a window a movement.
        if (mean >= 0.008) ++activeTransitions;
        ++transitions;
    }
    if (transitions == 0) return {};
    return {total / static_cast<double>(transitions),
            static_cast<double>(activeTransitions) / static_cast<double>(transitions),
            activeJointMean(std::move(ranges))};
}

MotionWindow mirroredWindow(MotionWindow window)
{
    for (auto& frame : window.frames) {
        for (auto& point : frame.keypoints) {
            if (std::isfinite(point.x)) point.x = -point.x;
        }
        // COCO-17 left/right semantic joints must be swapped as well as X.
        if (frame.keypoints.size() == 17) {
            for (const auto [left, right] : {std::pair<std::size_t, std::size_t>{1, 2},
                                             {3, 4}, {5, 6}, {7, 8}, {9, 10},
                                             {11, 12}, {13, 14}, {15, 16}}) {
                std::swap(frame.keypoints[left], frame.keypoints[right]);
            }
        }
    }
    return window;
}

std::vector<Descriptor> describe(const std::vector<NormalizedPose>& normalized,
                                 const MotionWindow& window)
{
    std::vector<Descriptor> result;
    result.reserve(normalized.size());
    for (std::size_t frameIndex = 0; frameIndex < normalized.size(); ++frameIndex) {
        const auto& pose = normalized[frameIndex];
        if (pose.empty()) { result.emplace_back(); continue; }
        if (window.staticFrameSet) {
            Descriptor descriptor;
            for (const auto& point : pose) {
                descriptor.insert(descriptor.end(), {point.first, point.second, 0.0, 0.0});
            }
            result.push_back(std::move(descriptor));
            continue;
        }
        // The retrieval descriptor must describe *what the person does*, not
        // where they happen to stand in a shot. Keeping normalized x/y here
        // made the nearest-neighbour stage a pose/composition search: a
        // stationary close-up of the same actor beat a matching gesture from
        // a different scene. Store locally fitted joint velocities instead;
        // DTW can now align an arm raise, turn or step at different moments
        // and speeds without treating a held pose as a parallel.
        Descriptor descriptor;
        // Keep the actor's root trajectory as one additional pseudo-joint.
        // Normalized keypoints intentionally remove translation; without this
        // channel a walking/approach movement whose body shape stays rigid is
        // indistinguishable from a static pose.  The root is normalized by
        // the initial body scale, so it remains comparable across resolutions.
        descriptor.reserve(pose.size() * 4 + 4);
        for (std::size_t pointIndex = 0; pointIndex < pose.size(); ++pointIndex) {
            double velocityX = std::numeric_limits<double>::quiet_NaN();
            double velocityY = velocityX;
            double accelerationX = velocityX;
            double accelerationY = velocityX;
            // Fit velocity over a short time neighbourhood. Differencing raw
            // detections twice magnified pixel jitter and missing joints into
            // clipped accelerations, making even repeated gestures disagree.
            // Missing observations stay missing; only observed joints enter
            // the fit and no gap longer than 250 ms is bridged.
            if (validPoint(pose[pointIndex])) {
                double sumT = 0, sumTT = 0, sumX = 0, sumY = 0, sumTX = 0, sumTY = 0;
                std::size_t count = 0;
                const auto begin = frameIndex > 2 ? frameIndex - 2 : 0;
                const auto end = std::min(normalized.size(), frameIndex + 3);
                for (std::size_t sample = begin; sample < end; ++sample) {
                    if (normalized[sample].size() != pose.size()
                        || !validPoint(normalized[sample][pointIndex])) continue;
                    const double t = window.frames[sample].timestampSeconds
                        - window.frames[frameIndex].timestampSeconds;
                    if (std::abs(t) > 0.25) continue;
                    const auto& p = normalized[sample][pointIndex];
                    sumT += t; sumTT += t * t;
                    sumX += p.first; sumY += p.second;
                    sumTX += t * p.first; sumTY += t * p.second;
                    ++count;
                }
                const double denominator = count * sumTT - sumT * sumT;
                if (count >= 2 && denominator > 1e-9) {
                    velocityX = (count * sumTX - sumT * sumX) / denominator;
                    velocityY = (count * sumTY - sumT * sumY) / denominator;
                } else {
                    velocityX = velocityY = std::numeric_limits<double>::quiet_NaN();
                }
                accelerationX = accelerationY = 0.0;
            }
            descriptor.push_back(std::isfinite(velocityX) ? std::clamp(velocityX, -4.0, 4.0)
                                                          : velocityX);
            descriptor.push_back(std::isfinite(velocityY) ? std::clamp(velocityY, -4.0, 4.0)
                                                          : velocityY);
            descriptor.push_back(std::isfinite(accelerationX) ? std::clamp(accelerationX, -8.0, 8.0)
                                                              : accelerationX);
            descriptor.push_back(std::isfinite(accelerationY) ? std::clamp(accelerationY, -8.0, 8.0)
                                                              : accelerationY);
        }
        const FrameRoot currentRoot = frameRoot(window.frames[frameIndex]);
        const FrameRoot previousRoot = frameIndex > 0
            ? frameRoot(window.frames[frameIndex - 1]) : currentRoot;
        const double rootScale = std::max({frameRoot(window.frames.front()).scale,
                                           currentRoot.scale, previousRoot.scale, 1e-6});
        const double rootDt = frameIndex > 0
            ? std::max(1e-3, window.frames[frameIndex].timestampSeconds
                - window.frames[frameIndex - 1].timestampSeconds) : 1.0;
        const double rootVelocityX = frameIndex > 0
            ? (currentRoot.x - previousRoot.x) / rootScale / rootDt : 0.0;
        const double rootVelocityY = frameIndex > 0
            ? (currentRoot.y - previousRoot.y) / rootScale / rootDt : 0.0;
        const bool validRoot = frameIndex > 0 && !normalized[frameIndex - 1].empty();
        descriptor.push_back(validRoot ? std::clamp(rootVelocityX, -4.0, 4.0) : 0.0);
        descriptor.push_back(validRoot ? std::clamp(rootVelocityY, -4.0, 4.0) : 0.0);
        descriptor.push_back(0.0);
        descriptor.push_back(0.0);
        result.push_back(std::move(descriptor));
    }
    for(auto& descriptor:result) {
        for(std::size_t offset=0;offset+1<descriptor.size();offset+=4) {
            if(!std::isfinite(descriptor[offset]) || !std::isfinite(descriptor[offset+1]))continue;
            descriptor.maximumVelocity=std::max(descriptor.maximumVelocity,
                std::hypot(descriptor[offset],descriptor[offset+1]));
        }
    }
    return result;
}

double frameDistance(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()
        || left.size() % 4U != 0U) return 1.0;

    // Descriptors are interleaved x/y velocity and x/y acceleration for each
    // joint. This makes the distance a comparison of trajectories rather
    // than of the static body layout in a particular camera angle.
    double velocityError = 0.0;
    double accelerationError = 0.0;
    std::size_t comparableJoints = 0;
    std::size_t comparableVelocities = 0;
    const std::size_t joints = left.size() / 4U;
    if (joints == 18) { // COCO body motion plus the root-translation channel
        constexpr std::size_t chains[4][3] = {{5, 7, 9}, {6, 8, 10},
                                              {11, 13, 15}, {12, 14, 16}};
        const auto observes = [](const Descriptor& descriptor, const auto& chain) {
            return std::all_of(std::begin(chain), std::end(chain), [&](std::size_t joint) {
                const auto offset = joint * 4;
                return std::isfinite(descriptor[offset]) && std::isfinite(descriptor[offset + 1]);
            });
        };
        const bool sharedLimb = std::any_of(std::begin(chains), std::end(chains), [&](const auto& chain) {
            return observes(left, chain) && observes(right, chain);
        });
        // Head/shoulders plus camera drift are not evidence of an arm/body
        // gesture. Never fill an occluded limb with a fictitious agreement.
        // Symmetric head-only trajectories retain their existing checks.
        const bool leftBody = std::any_of(std::begin(chains), std::end(chains), [&](const auto& c) { return observes(left, c); });
        const bool rightBody = std::any_of(std::begin(chains), std::end(chains), [&](const auto& c) { return observes(right, c); });
        if ((leftBody || rightBody) && !sharedLimb) return 1.0;
    }
    for (std::size_t joint = 0; joint < joints; ++joint) {
        const std::size_t offset = joint * 4U;
        if (!std::isfinite(left[offset]) || !std::isfinite(left[offset + 1U])
            || !std::isfinite(right[offset]) || !std::isfinite(right[offset + 1U])) {
            continue;
        }
        // Velocities are expressed in normalized body units per second.  A
        // divisor of 8 made ordinary 0.1–0.3 units/s movements almost
        // indistinguishable (frameSimilarity stayed near 1.0 for unrelated
        // scenes).  Keep a moderate robust scale instead: small detector
        // jitter is still cheap, while the direction/amplitude of a real
        // gesture contributes materially to the distance.
        velocityError += std::hypot(left[offset] - right[offset],
                                    left[offset + 1U] - right[offset + 1U]) / 2.0;
        ++comparableJoints;
        if (std::isfinite(left[offset + 2U]) && std::isfinite(left[offset + 3U])
            && std::isfinite(right[offset + 2U]) && std::isfinite(right[offset + 3U])) {
            accelerationError += std::hypot(left[offset + 2U] - right[offset + 2U],
                                            left[offset + 3U] - right[offset + 3U]) / 4.0;
            ++comparableVelocities;
        }
    }
    // A head/shoulder crop lacks the body information needed to call two
    // scenes a matching motion. Failing closed beats inventing a percentage.
    const std::size_t minimumComparable = std::min(kMinimumComparableJoints, joints);
    if (comparableJoints < minimumComparable) return 1.0;
    const double velocity = velocityError / static_cast<double>(comparableJoints);
    const double acceleration = comparableVelocities == 0 ? 0.0
        : accelerationError / static_cast<double>(comparableVelocities);
    return std::clamp(0.75 * velocity + 0.25 * acceleration, 0.0, 1.0);
}

double frameSimilarity(const Descriptor& left, const Descriptor& right, double& distance)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    // Stationary observations cannot establish a motion run even when their
    // descriptors agree perfectly. Static mode has its own pose descriptor.
    if (left.maximumVelocity < 1e-4 || right.maximumVelocity < 1e-4) return 0.0;
    distance = frameDistance(left, right);
    if (!std::isfinite(distance)) return 0.0;
    // Agreement is measured on motion, not appearance or body proportions.
    return std::clamp(std::exp(-4.0 * distance), 0.0, 1.0);
}

double noiseFloor(const std::vector<Descriptor>& descriptors)
{
    if (descriptors.size() < 2) return 0.0;
    std::vector<double> deltas;
    deltas.reserve(descriptors.size() - 1);
    for (std::size_t i = 1; i < descriptors.size(); ++i) {
        if (descriptors[i - 1].empty() || descriptors[i].empty()
            || descriptors[i - 1].size() != descriptors[i].size()) {
            continue;
        }
        deltas.push_back(frameDistance(descriptors[i - 1], descriptors[i]));
    }
    if (deltas.empty()) return 0.0;
    const auto middle = deltas.begin() + static_cast<std::ptrdiff_t>(deltas.size() / 2);
    std::nth_element(deltas.begin(), middle, deltas.end());
    return *middle;
}

double coarseSimilarity(const std::vector<Descriptor>& left,
                        const std::vector<Descriptor>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    // Four endpoint samples made a long window look like a single-frame
    // comparison. Use a denser temporal sketch before the expensive DTW pass.
    const std::size_t samples = std::min<std::size_t>(16, std::min(left.size(), right.size()));
    if (samples == 0) return 0.0;
    double distance = 0.0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < samples; ++i) {
        const std::size_t li = (i * (left.size() - 1)) / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (i * (right.size() - 1)) / std::max<std::size_t>(1, samples - 1);
        const double cost = frameDistance(left[li], right[ri]);
        if (cost < 1.0 || (!left[li].empty() && !right[ri].empty())) {
            distance += cost;
            ++used;
        }
    }
    return used == 0 ? 0.0 : std::clamp(std::exp(-3.0 * distance
                                                        / static_cast<double>(used)), 0.0, 1.0);
}

double shapeSimilarity(const std::vector<NormalizedPose>& left,
                       const std::vector<NormalizedPose>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    const std::size_t samples = std::min<std::size_t>(8, std::min(left.size(), right.size()));
    double score = 0.0;
    std::size_t used = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t li = (sample * (left.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (sample * (right.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const auto& a = left[li];
        const auto& b = right[ri];
        if (a.empty() || b.empty()) continue;

        // Joint order is the topology contract, not visibility. Two close-ups
        // can agree on every observed joint while both omit the legs. Penalize
        // asymmetric visibility, never joints absent from both observations.
        const std::size_t points = std::min(a.size(), b.size());
        std::vector<std::size_t> valid;
        valid.reserve(points);
        for (std::size_t i = 0; i < points; ++i) {
            if (validPoint(a[i]) && validPoint(b[i])) valid.push_back(i);
        }
        const std::size_t minimumComparable = std::min(kMinimumComparableJoints, points);
        if (valid.size() < minimumComparable) continue;
        std::size_t observed = 0;
        for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
            if ((i < a.size() && validPoint(a[i]))
                || (i < b.size() && validPoint(b[i]))) ++observed;
        }
        const double topology = static_cast<double>(valid.size())
            / static_cast<double>(std::max<std::size_t>(1, observed));
        double error = 0.0;
        std::size_t pairs = 0;
        for (std::size_t leftIndex = 0; leftIndex < valid.size(); ++leftIndex) {
            for (std::size_t rightIndex = leftIndex + 1; rightIndex < valid.size(); ++rightIndex) {
                const std::size_t i = valid[leftIndex];
                const std::size_t j = valid[rightIndex];
                const double leftDistance = std::hypot(a[i].first - a[j].first,
                                                       a[i].second - a[j].second);
                const double rightDistance = std::hypot(b[i].first - b[j].first,
                                                        b[i].second - b[j].second);
                error += std::abs(leftDistance - rightDistance);
                ++pairs;
            }
        }
        const double proportionScore = pairs == 0
            ? 1.0
            : std::exp(-3.0 * error / static_cast<double>(pairs));
        score += topology * proportionScore;
        ++used;
    }
    return used == 0 ? 0.0 : std::clamp(score / static_cast<double>(used), 0.0, 1.0);
}

// Joint articulation complements image-plane distances for foreshortened
// views. It is not an action classifier: only observed, homologous chains
// are compared, after the identity gate, and only in static-pose mode.
std::vector<NormalizedPose> articulationPoses(const MotionWindow& window)
{
    std::vector<NormalizedPose> result;
    if (!window.staticFrameSet) return result;
    for (const auto& frame : window.frames) {
        NormalizedPose pose;
        for (const auto& point : frame.keypoints) {
            // Angles amplify errors at a poorly localized elbow. Such a
            // chain is unknown, not contradictory or perfectly straight.
            const double missing = std::numeric_limits<double>::quiet_NaN();
            const bool observed = std::isfinite(point.confidence) && point.confidence >= 0.5;
            pose.emplace_back(observed ? point.x : missing, observed ? point.y : missing);
        }
        result.push_back(std::move(pose));
    }
    return result;
}

double articulationSimilarity(const std::vector<NormalizedPose>& left,
                              const std::vector<NormalizedPose>& right,
                              const std::vector<NormalizedPose>& leftContext,
                              const std::vector<NormalizedPose>& rightContext)
{
    if (left.empty() || right.empty()) return 0.0;
    constexpr std::size_t chains[4][4] = {
        {5, 7, 9, 6}, {6, 8, 10, 5}, {11, 13, 15, 12}, {12, 14, 16, 11}
    };
    const auto features = [](const NormalizedPose& pose, const auto& chain,
                             double (&out)[3]) {
        if (pose.size() != 17 || !validPoint(pose[0])) return false;
        for (const auto joint : chain) if (!validPoint(pose[joint])) return false;
        const auto vector = [&](std::size_t a, std::size_t b) {
            return std::pair{pose[b].first - pose[a].first,
                             pose[b].second - pose[a].second};
        };
        const auto dot = [](auto a, auto b) { return a.first*b.first + a.second*b.second; };
        const auto cross = [](auto a, auto b) { return a.first*b.second - a.second*b.first; };
        const auto across = vector(chain[0], chain[3]);
        const auto upper = vector(chain[0], chain[1]);
        const auto lower = vector(chain[1], chain[2]);
        const auto head = vector(chain[0], 0);
        const auto reach = vector(0, chain[2]);
        const double acrossLength = std::hypot(across.first, across.second);
        const double upperLength = std::hypot(upper.first, upper.second);
        const double lowerLength = std::hypot(lower.first, lower.second);
        const double reachLength = std::hypot(reach.first, reach.second);
        if (std::min({acrossLength, upperLength, lowerLength}) < 1e-6)
            return false;
        // The head supplies the up/down half-plane; reflection must not turn
        // a lowered arm into a raised arm with the same unsigned angle.
        const double up = cross(across, head);
        if (std::abs(up) < 1e-6 * acrossLength) return false;
        const double down = up > 0.0 ? -1.0 : 1.0;
        out[0] = std::atan2(down * cross(across, upper), dot(across, upper));
        out[1] = std::atan2(down * cross(upper, lower), dot(upper, lower));
        // A wrist touching the face is valid contrary evidence, not a
        // missing chain. Clamp the dimensionless ratio only for log(0).
        out[2] = std::log(std::max(reachLength / (upperLength + lowerLength), 1e-6));
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    };
    const std::size_t samples = std::min<std::size_t>(8, std::min(left.size(), right.size()));
    std::vector<double> agreements;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto& a = left[sample * (left.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& b = right[sample * (right.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& contextA = leftContext[sample * (leftContext.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& contextB = rightContext[sample * (rightContext.size()-1) / std::max<std::size_t>(1, samples-1)];
        double worst = 0.0;
        std::size_t observed = 0;
        for (const auto& chain : chains) {
            double x[3], y[3];
            if (!features(a, chain, x) || !features(b, chain, y)) continue;
            const auto angularDistance = [](double angle) {
                return std::atan2(std::sin(angle), std::cos(angle));
            };
            const double shoulder = angularDistance(x[0]-y[0]);
            const double elbow = angularDistance(x[1]-y[1]);
            const double reach = x[2]-y[2];
            // Missing bilateral support may withhold a positive vote, but
            // must never erase disagreement in an already observed limb.
            worst = std::max(worst,
                0.5*(shoulder*shoulder + elbow*elbow) + 0.15*reach*reach);
            double relationError = 0.0;
            if (chain[0] == 5 || chain[0] == 6) {
                // Similar elbow flexion can mean aiming OR folded arms.
                // Require bilateral evidence and compare each elbow's
                // distance to the opposite wrist, in shoulder-width units.
                const std::size_t oppositeWrist = chain[0] == 5 ? 10 : 9;
                const auto relation = [&](const NormalizedPose& pose) {
                    if (pose.size() != 17 || !validPoint(pose[oppositeWrist])
                        || !validPoint(pose[chain[1]]) || !validPoint(pose[5])
                        || !validPoint(pose[6])) return std::numeric_limits<double>::quiet_NaN();
                    const auto distance = [&](std::size_t i, std::size_t j) {
                        return std::hypot(pose[i].first-pose[j].first, pose[i].second-pose[j].second);
                    };
                    const double scale = distance(5, 6);
                    if (scale < 1e-6) return std::numeric_limits<double>::quiet_NaN();
                    return std::log(std::max(distance(chain[1], oppositeWrist) / scale, 1e-6));
                };
                const double difference = relation(contextA) - relation(contextB);
                if (!std::isfinite(difference)) continue;
                relationError = 0.5 * difference * difference;
            }
            // Do not average away a contradictory second arm or leg.
            worst = std::max(worst, relationError);
            ++observed;
        }
        if (observed) agreements.push_back(std::exp(-2.0 * worst));
    }
    // A lucky single-frame detection is not sustained pose evidence.
    if (agreements.size() < MotionMatcherParams::minimumStaticSamples
        || agreements.size() * 4 < samples * 3) return 0.0;
    // Use the lower quartile, not a best frame or an average dominated by
    // one confidently mislocalized wrist. At least 75% must support it.
    std::sort(agreements.begin(), agreements.end());
    return agreements[(agreements.size() - 1) / 4];
}

struct TemporalAlignment {
    std::size_t run = 0;
    double averageSimilarity = 0.0;
    std::size_t startLeft = 0;
    std::size_t endLeft = 0;
    std::size_t startRight = 0;
    std::size_t endRight = 0;
    std::vector<std::pair<std::size_t, std::size_t>> path;
    std::vector<double> distances;
    std::size_t columns = 0;
};

TemporalAlignment alignTemporal(const std::vector<Descriptor>& left,
                                 const std::vector<Descriptor>& right,
                                 double similarityThreshold)
{
    TemporalAlignment result;
    if (left.size() < 3 || right.size() < 3
        || left.size()>MotionMatcherParams::maximumWindowFrames
        || right.size()>MotionMatcherParams::maximumWindowFrames) return result;

    // A gesture can start at a different point inside two overlapping
    // windows, and one export may be sampled faster than the other.  Find the
    // longest monotonic run with a bounded one-to-many step instead of forcing
    // both windows onto the same diagonal.  Window sizes are small (normally
    // 12–30 samples), so O(n*m*4) is negligible next to DTW/inference.
    // Both axes may advance by two observations. This symmetric constraint
    // tolerates isolated detector dropouts and up to 2x tempo changes without
    // reusing one observation as evidence for a complete gesture.
    struct Cell { std::size_t run = 0; double score = 0; std::size_t parent = 0; };
    const std::size_t cols = right.size();
    std::vector<Cell> cells(left.size() * cols);
    result.columns=cols;
    result.distances.assign(left.size()*cols,std::numeric_limits<double>::infinity());
    std::size_t bestCell = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        for (std::size_t j = 0; j < right.size(); ++j) {
            const double similarity = frameSimilarity(left[i], right[j],result.distances[i*cols+j]);
            if (similarity < similarityThreshold) continue;
            auto& cell = cells[i * cols + j];
            cell = {1, similarity, i * cols + j};
            for (std::size_t di = 1; di <= 2 && di <= i; ++di) {
                for (std::size_t dj = 1; dj <= 2 && dj <= j; ++dj) {
                    const auto previousIndex = (i - di) * cols + j - dj;
                    const auto& previous = cells[previousIndex];
                    if (!previous.run) continue;
                    const double score = previous.score + similarity;
                    if (previous.run + 1 > cell.run
                        || (previous.run + 1 == cell.run && score > cell.score))
                        cell = {previous.run + 1, score, previousIndex};
                }
            }
            const auto& best = cells[bestCell];
            if (cell.run > best.run || (cell.run == best.run && cell.score > best.score))
                bestCell = i * cols + j;
        }
    }
    if (!cells[bestCell].run) return result;
    result.run = cells[bestCell].run;
    result.averageSimilarity = cells[bestCell].score / result.run;
    for (std::size_t index = bestCell;; index = cells[index].parent) {
        result.path.emplace_back(index / cols, index % cols);
        if (cells[index].parent == index) break;
    }
    std::reverse(result.path.begin(), result.path.end());
    result.startLeft = result.path.front().first;
    result.startRight = result.path.front().second;
    result.endLeft = result.path.back().first;
    result.endRight = result.path.back().second;
    return result;
}


double velocityDirectionScore(const std::vector<Descriptor>& left,
                              const std::vector<Descriptor>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    const std::size_t samples = std::min(left.size(), right.size());
    double score = 0.0;
    std::size_t used = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t li = (sample * (left.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (sample * (right.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const auto& a = left[li];
        const auto& b = right[ri];
        if (a.empty() || b.empty() || a.size() != b.size()) continue;
        double dot = 0.0;
        double normA = 0.0;
        double normB = 0.0;
        // In COCO descriptors the last channel is global root translation.
        // A shared camera pan must not turn opposite head/limb trajectories
        // into the same gesture. Prefer anatomical direction when both
        // observations contain measurable articulation; retain the root for
        // rigid locomotion where normalized body joints barely move.
        const bool coco = a.size() == 72;
        const std::size_t bodyEnd = coco ? a.size() - 4U : a.size();
        for (std::size_t point = 0; point + 3U < bodyEnd; point += 4U) {
            if (!std::isfinite(a[point]) || !std::isfinite(a[point + 1U])
                || !std::isfinite(b[point]) || !std::isfinite(b[point + 1U])) continue;
            dot += a[point] * b[point] + a[point + 1U] * b[point + 1U];
            normA += a[point] * a[point] + a[point + 1U] * a[point + 1U];
            normB += b[point] * b[point] + b[point + 1U] * b[point + 1U];
        }
        if (coco && (normA <= 1e-4 || normB <= 1e-4)) {
            const auto root = bodyEnd;
            if (std::isfinite(a[root]) && std::isfinite(a[root + 1U])
                && std::isfinite(b[root]) && std::isfinite(b[root + 1U])) {
                dot += a[root] * b[root] + a[root + 1U] * b[root + 1U];
                normA += a[root] * a[root] + a[root + 1U] * a[root + 1U];
                normB += b[root] * b[root] + b[root + 1U] * b[root + 1U];
            }
        }
        if (normA <= 1e-8 || normB <= 1e-8) continue;
        score += std::clamp(dot / std::sqrt(normA * normB), -1.0, 1.0);
        ++used;
    }
    // Orthogonal directions are not "50% the same movement". Shifting the
    // cosine by +0.5 previously inflated weak directional agreement enough
    // to pass quick search when low-amplitude DTW distances looked good.
    // Opposite/orthogonal evidence contributes zero; only positive alignment
    // contributes to the score. Identical directions remain one.
    return used == 0 ? 0.0 : std::clamp(score / static_cast<double>(used), 0.0, 1.0);
}

double duration(const MotionWindow& window);

double activeBodyAgreement(const std::vector<Descriptor>& left,
                           const std::vector<Descriptor>& right)
{
    // COCO's 17 anatomical joints plus the root-translation pseudo-joint.
    // Shared camera/root motion must not make a hand gesture match a step.
    if (left.empty() || right.empty() || left.front().size() != 72 || right.front().size() != 72)
        return 1.0;
    const auto energy = [](const std::vector<Descriptor>& samples, const std::vector<Descriptor>& other) {
        std::vector<double> result(17, 0.0);
        for (std::size_t frame = 0; frame < std::min(samples.size(), other.size()); ++frame) {
            const auto& sample = samples[frame];
            if (sample.size() != 72 || other[frame].size() != 72) continue;
            for (std::size_t joint = 0; joint < 17; ++joint) {
                const auto offset = joint * 4;
                if (!std::isfinite(sample[offset]) || !std::isfinite(sample[offset + 1])) continue;
                // An unobserved limb is unknown, not stationary.
                if (!std::isfinite(other[frame][offset]) || !std::isfinite(other[frame][offset + 1])) continue;
                result[joint] += std::max(0.0, std::hypot(sample[offset], sample[offset + 1]) - 0.01);
            }
        }
        return result;
    };
    const auto a = energy(left, right), b = energy(right, left);
    double dot = 0.0, normA = 0.0, normB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i]; normA += a[i] * a[i]; normB += b[i] * b[i];
    }
    if (normA <= 1e-8 && normB <= 1e-8) return 1.0; // rigid-body locomotion
    if (normA <= 1e-8 || normB <= 1e-8) return 0.0;
    return std::clamp(dot / std::sqrt(normA * normB), 0.0, 1.0);
}

bool hasTemporalSupport(const MotionWindow& window,
                        const MotionMatcherParams& params)
{
    if (window.frames.size() < 3) return false;
    const double span = duration(window);
    if (window.staticFrameSet)
        return window.frames.size() >= MotionMatcherParams::minimumStaticSamples
            && span + 1e-9 >= MotionMatcherParams::minimumStaticSpanSeconds;
    return window.frames.size() >= params.minTemporalFrames
        || (span + 1e-9 >= std::max(params.minTemporalDurationSec, params.minMotionSpanSec)
            && window.frames.size() >= 6);
}

bool hasDistinctTemporalSamples(const MotionWindow& window,
                                const std::vector<Descriptor>& descriptors,
                                const MotionMatcherParams& params)
{
    // A candidate is never allowed to collapse to one reused frame. Count the
    // actual timestamped samples that reached the descriptor stage; this also
    // works for static-frame mode where pose motion is intentionally optional.
    const auto minimumSamples = window.staticFrameSet
        ? MotionMatcherParams::minimumStaticSamples : std::size_t{6};
    if (descriptors.size() < minimumSamples || window.frames.size() < minimumSamples) return false;
    std::size_t usable = 0;
    double previousTimestamp = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < descriptors.size()
         && index < window.frames.size(); ++index) {
        if (descriptors[index].empty()) continue;
        const double timestamp = window.frames[index].timestampSeconds;
        if (!std::isfinite(timestamp) || timestamp <= previousTimestamp) continue;
        previousTimestamp = timestamp;
        ++usable;
    }
    if (window.staticFrameSet)
        return usable >= minimumSamples
            && duration(window) + 1e-9 >= MotionMatcherParams::minimumStaticSpanSeconds;
    return usable >= params.minTemporalFrames
        || (usable >= 6 && duration(window) + 1e-9
            >= std::max(params.minTemporalDurationSec, params.minMotionSpanSec));
}

bool hasTemporalDiversity(const std::vector<Descriptor>& descriptors)
{
    // Counting timestamps alone is not enough: a detector can copy one pose
    // into dozens of frames. Require several genuinely new descriptor states
    // across the window. The threshold is below a meaningful body movement
    // but above ordinary one-frame keypoint jitter.
    if (descriptors.size() < 6) return false;
    constexpr double noveltyThreshold = 0.015;
    const Descriptor* anchor = nullptr;
    std::size_t distinctStates = 0;
    for (const auto& descriptor : descriptors) {
        if (descriptor.empty()) continue;
        if (!anchor || frameDistance(*anchor, descriptor) >= noveltyThreshold) {
            anchor = &descriptor;
            ++distinctStates;
        }
    }
    return distinctStates >= 4;
}

bool hasTemporalRun(const std::vector<Descriptor>& left,
                   const std::vector<Descriptor>& right,
                   const MotionWindow& leftWindow,
                   const MotionWindow& rightWindow,
                   const MotionMatcherParams& params,
                   const TemporalAlignment& alignment)
{
    const double runDuration = alignment.run == 0 ? 0.0 : std::min(
        leftWindow.frames[std::min(alignment.endLeft, leftWindow.frames.size() - 1)].timestampSeconds
            - leftWindow.frames[std::min(alignment.startLeft, leftWindow.frames.size() - 1)].timestampSeconds,
        rightWindow.frames[std::min(alignment.endRight, rightWindow.frames.size() - 1)].timestampSeconds
            - rightWindow.frames[std::min(alignment.startRight, rightWindow.frames.size() - 1)].timestampSeconds);
    // Require a real run, but do not force the entire 2.5 s analysis window
    // to align. Edits often share only one short gesture; six consecutive
    // observations is the hard floor and the quarter-window term scales it
    // for denser sampling.
    const std::size_t requiredFrames = std::max<std::size_t>(
        std::size_t{6},
        static_cast<std::size_t>(std::ceil(0.25
            * static_cast<double>(std::min(left.size(), right.size())))));
    const bool enoughFrames = alignment.run >= requiredFrames
        && runDuration + 1e-9 >= params.minTemporalDurationSec;
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr, "PF_DEBUG_MATCHER temporal max=%.3f bestRun=%zu duration=%.3f threshold=%.3f\n",
                     alignment.averageSimilarity, alignment.run, runDuration,
                     params.temporalSimilarityThreshold);
    }
    return enoughFrames;
}

double duration(const MotionWindow& window)
{
    if (window.frames.size() < 2) return 0.0;
    return std::max(0.0, window.frames.back().timestampSeconds - window.frames.front().timestampSeconds);
}

std::vector<double> embedding(const std::vector<Descriptor>& descriptors,
                              std::size_t dimension)
{
    constexpr std::size_t frameSamples = 8;
    std::vector<double> result(frameSamples * dimension, 0.0);
    if (descriptors.empty() || dimension == 0) return result;
    for (std::size_t sample = 0; sample < frameSamples; ++sample) {
        const std::size_t frame = (sample * (descriptors.size() - 1))
            / std::max<std::size_t>(1, frameSamples - 1);
        const auto& descriptor = descriptors[frame];
        const std::size_t copyCount = std::min(dimension, descriptor.size());
        // Missing joints are NaN in the exact matcher. HNSW distances and
        // heap ordering require finite values; preserve missing-data checks
        // in verification and use a neutral value only for coarse retrieval.
        for (std::size_t i = 0; i < copyCount; ++i)
            result[sample * dimension + i] = std::isfinite(descriptor[i]) ? descriptor[i] : 0.0;
    }
    return result;
}

struct PreparedWindow {
    double noise = 0.0;
    double mirroredNoise = 0.0;
    std::vector<NormalizedPose> poses;
    std::vector<NormalizedPose> articulation;
    std::vector<NormalizedPose> mirroredArticulation;
    std::vector<Descriptor> descriptors;
    std::vector<double> embedding;
    std::vector<NormalizedPose> mirroredPoses;
    std::vector<Descriptor> mirroredDescriptors;
    std::vector<double> mirroredEmbedding;
    double motionDelta = 0.0;
    double activeTransitionRatio = 0.0;
    double trajectoryRange = 0.0;
    bool closeup = false;
    bool headRegion = false;
    bool bodyObserved = false;
    bool confidentBodyObserved = false;
    bool samplesSupported = false;
    bool mirroredSamplesSupported = false;
    bool temporalSupported = false;
    MotionWindow headSource;
    std::unique_ptr<PreparedWindow> head;
    std::unique_ptr<PreparedWindow> torso;
};

void prepareWindowFacts(PreparedWindow& prepared, const MotionWindow& window,
                        const MotionMatcherParams& params)
{
    prepared.bodyObserved = observedBody(window);
    prepared.confidentBodyObserved = observedBody(window, .5);
    prepared.samplesSupported = hasDistinctTemporalSamples(window, prepared.descriptors, params);
    prepared.mirroredSamplesSupported = hasDistinctTemporalSamples(window, prepared.mirroredDescriptors, params);
    prepared.temporalSupported = hasTemporalSupport(window, params);
}

void prepareCloseup(PreparedWindow& prepared, const MotionWindow& window,
                    const MotionMatcherParams& params)
{
    if (!window.staticFrameSet || window.faceEmbedding.empty() || window.frames.empty()
        || prepared.bodyObserved) return;
    std::size_t closeups = 0;
    for (const auto& frame : window.frames) {
        if (frame.keypoints.size() != 17) return;
        const auto visible = std::count_if(frame.keypoints.begin(), frame.keypoints.end(), [](const auto& p) {
            return p.confidence >= kMinimumKeypointConfidence && std::isfinite(p.x) && std::isfinite(p.y);
        });
        if (visible < static_cast<int>(kMinimumComparableJoints)) ++closeups;
    }
    // Only switch normalization when an actual face-only crop needs it.
    // Two face-and-shoulder views retain their measured neck/torso anchor;
    // throwing it away made unlike portraits look almost identical.
    prepared.closeup = closeups * 2 >= window.frames.size();
    prepared.headSource = window;
    for (auto& frame : prepared.headSource.frames) frame.keypoints.resize(5);
    prepared.head = std::make_unique<PreparedWindow>();
    auto& head = *prepared.head;
    head.headRegion = true;
    head.poses = normalizePoses(prepared.headSource, params.normalizeSize);
    head.articulation = articulationPoses(prepared.headSource);
    head.descriptors = describe(head.poses, prepared.headSource);
    head.noise = noiseFloor(head.descriptors);
    if (params.mirrorInvariant) {
        const auto mirrored = mirroredWindow(prepared.headSource);
        head.mirroredPoses = normalizePoses(mirrored, params.normalizeSize);
        head.mirroredArticulation = articulationPoses(mirrored);
        head.mirroredDescriptors = describe(head.mirroredPoses, mirrored);
        head.mirroredNoise = noiseFloor(head.mirroredDescriptors);
    }
    prepareWindowFacts(head, prepared.headSource, params);
}

void prepareTorso(PreparedWindow& prepared, const MotionWindow& window,
                  const MotionMatcherParams& params)
{
    if(!params.bodyScaleInvariant || !params.normalizeSize || window.frames.empty())return;
    // A measured trunk supplies the scale even when the shot crops the legs.
    // Require a complete leg or both elbows in addition to both hips.
    std::size_t supported=0;
    for(const auto& frame:window.frames) {
        if(frame.keypoints.size()!=17)continue;
        const auto visible=[&](std::size_t i) {const auto& p=frame.keypoints[i];
            return p.confidence>=.5 && std::isfinite(p.x+p.y);};
        if(visible(5) && visible(6) && visible(11) && visible(12)
            && ((visible(13) && visible(15)) || (visible(14) && visible(16))
                || (visible(7) && visible(8))))++supported;
    }
    if(supported<MotionMatcherParams::minimumStaticSamples || supported*4<window.frames.size()*3)return;
    auto alternative=std::make_unique<PreparedWindow>();auto& p=*alternative;
    p.poses=normalizePoses(window,true,true);
    if(p.poses.empty())return;
    p.articulation=prepared.articulation;p.descriptors=describe(p.poses,window);p.noise=noiseFloor(p.descriptors);
    const auto activity=motionActivity(window,p.poses);
    p.motionDelta=activity.meanDelta;p.activeTransitionRatio=activity.activeTransitionRatio;p.trajectoryRange=activity.trajectoryRange;
    if(params.mirrorInvariant) {
        const auto mirrored=mirroredWindow(window);p.mirroredPoses=normalizePoses(mirrored,true,true);
        p.mirroredArticulation=articulationPoses(mirrored);p.mirroredDescriptors=describe(p.mirroredPoses,mirrored);
        p.mirroredNoise=noiseFloor(p.mirroredDescriptors);
    }
    prepareWindowFacts(p,window,params);prepared.torso=std::move(alternative);
}

MotionMatch comparePrepared(const MotionWindow& left, const MotionWindow& right,
                            const PreparedWindow& leftPrepared,
                            const PreparedWindow& rightPrepared,
                            const MotionMatcherParams& params,
                            std::size_t leftIndex, std::size_t rightIndex, bool mirrorRight = false,
                            const IdentityEvidence* knownIdentity = nullptr)
{
    const auto& a = leftPrepared.descriptors;
    const auto& b = mirrorRight ? rightPrepared.mirroredDescriptors : rightPrepared.descriptors;
    const auto& rightPoses = mirrorRight ? rightPrepared.mirroredPoses : rightPrepared.poses;
    const auto& rightArticulation = mirrorRight ? rightPrepared.mirroredArticulation : rightPrepared.articulation;
    MotionMatch result;
    result.leftIndex = leftIndex;
    result.rightIndex = rightIndex;
    result.leftSourceId = left.sourceId;
    result.rightSourceId = right.sourceId;
    result.dtwDistance = std::numeric_limits<double>::infinity();
    result.durationSeconds = std::min(duration(left), duration(right));
    result.leftStartSeconds = left.frames.empty() ? 0.0 : left.frames.front().timestampSeconds;
    result.leftEndSeconds = left.frames.empty() ? 0.0 : left.frames.back().timestampSeconds;
    result.rightStartSeconds = right.frames.empty() ? 0.0 : right.frames.front().timestampSeconds;
    result.rightEndSeconds = right.frames.empty() ? 0.0 : right.frames.back().timestampSeconds;
    result.leftSceneStartSeconds = left.sceneStartSeconds >= 0.0
        ? left.sceneStartSeconds : result.leftStartSeconds;
    result.leftSceneEndSeconds = left.sceneEndSeconds > result.leftSceneStartSeconds
        ? left.sceneEndSeconds : result.leftEndSeconds;
    result.rightSceneStartSeconds = right.sceneStartSeconds >= 0.0
        ? right.sceneStartSeconds : result.rightStartSeconds;
    result.rightSceneEndSeconds = right.sceneEndSeconds > result.rightSceneStartSeconds
        ? right.sceneEndSeconds : result.rightEndSeconds;
    // A shot is half-open: its boundary frame belongs to the NEXT shot.
    // Never score a cached/synthetic window that crosses known shot bounds.
    const auto crossesShot = [](const MotionWindow& window) {
        return window.hasSceneIndex && std::isfinite(window.sceneStartSeconds)
            && std::isfinite(window.sceneEndSeconds) && window.sceneStartSeconds >= 0
            && window.sceneEndSeconds > window.sceneStartSeconds
            && std::any_of(window.frames.begin(), window.frames.end(), [&](const PoseFrame& frame) {
                return frame.timestampSeconds < window.sceneStartSeconds
                    || frame.timestampSeconds >= window.sceneEndSeconds;
            });
    };
    if (crossesShot(left) || crossesShot(right)) return result;
    if (left.sourceId == right.sourceId
        && std::max(result.leftStartSeconds, result.rightStartSeconds)
            < std::min(result.leftEndSeconds, result.rightEndSeconds)) return result;
    if (left.sourceId == right.sourceId
        && left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex == right.sceneIndex) {
        result.similarity = 0.0;
        return result;
    }
    const auto identity = knownIdentity ? *knownIdentity : identityEvidence(left, right, params);
    const double sceneSimilarity = sceneContextSimilarity(left, right);
    const bool independentGesture = separateVerifiedShots(left, right, params, &identity)
        && (!left.staticFrameSet || (leftPrepared.bodyObserved && rightPrepared.bodyObserved));
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds) <= params.sameSceneContextGapSec
        && sceneSimilarity >= params.sameSceneContextThreshold
        && !independentGesture) return result;
    result.appearanceSimilarity = identity.score;
    result.sceneSimilarity = sceneSimilarity;
    result.appearanceVerified = identity.verified;
    result.faceVerified = identity.verified && identity.face;
    if (left.staticFrameSet && right.staticFrameSet && result.faceVerified
        && !leftPrepared.bodyObserved && !rightPrepared.bodyObserved) {
        result.headOnlyComparison = true;
        if (identity.score < params.minHeadFaceSimilarity) return result;
        // A few facial points are more ambiguous than an observed limb
        // gesture. Do not cherry-pick three momentary head positions: require
        // a sustained portrait on each side, without interpolating samples.
        if (left.frames.size() < 6 || right.frames.size() < 6
            || duration(left) < .75 || duration(right) < .75) return result;
    }
    // Close-up comparison has its own observable region. Do not normalize a
    // five-landmark head crop by its head radius and the other shot by torso
    // width. This fallback is pose-only, requires independent face identity,
    // and never promotes a held head pose to a repeated body movement.
    if (left.staticFrameSet && right.staticFrameSet && result.faceVerified) {
        // A face crop can still match a face-and-shoulders view. But an
        // observed body gesture must not be reduced to a face to match a
        // hidden, potentially contradictory arm pose in the other shot.
        if ((leftPrepared.closeup || rightPrepared.closeup) && leftPrepared.head && rightPrepared.head) {
            auto headMatch = comparePrepared(leftPrepared.headSource, rightPrepared.headSource,
                *leftPrepared.head, *rightPrepared.head, params, leftIndex, rightIndex, false, &identity);
            const double directScore = headMatch.similarity;
            if (params.mirrorInvariant) {
                const auto mirrored = comparePrepared(leftPrepared.headSource, rightPrepared.headSource,
                    *leftPrepared.head, *rightPrepared.head, params, leftIndex, rightIndex, true, &identity);
                if (mirrored.similarity > headMatch.similarity) headMatch = mirrored;
            }
            headMatch.headOnlyComparison = true;
            headMatch.unmirroredSimilarity = directScore;
            return headMatch;
        }
    }
    const bool crossTrack = left.sourceId == right.sourceId
        && params.requireSameTrackWithinSource && left.trackId != 0 && right.trackId != 0
        && differentTrackSegment(left, right);
    // A required identity check must fail closed: missing/weak evidence is
    // not proof that two motions belong to the same person. Body appearance
    // can supply evidence, but must not silently bypass this gate.
    if ((crossTrack || params.requireAppearance) && !identity.verified)
        return result;
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds)
            < (separateVerifiedShots(left, right, params, &identity)
                ? params.sameSourceGapFloorSec
                : std::max({params.sameSourceGapFloorSec, params.sameFileGapSec,
                            params.minRepeatGapSec}))) {
        result.similarity = 0.0;
        return result;
    }
    const bool staticPair = left.staticFrameSet || right.staticFrameSet;
    if (staticPair && std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        auto mask = [](const std::vector<NormalizedPose>& poses) {
            std::uint32_t bits = 0;
            if (!poses.empty()) for (std::size_t i = 0; i < poses.front().size() && i < 32; ++i)
                if (validPoint(poses.front()[i])) bits |= (1U << i);
            return bits;
        };
        std::fprintf(stderr, "PF_DEBUG_MATCHER pose-support a=%zu b=%zu mask=%x/%x\n",
                     leftIndex, rightIndex, mask(leftPrepared.poses), mask(rightPoses));
    }
    const bool leftSamples = leftPrepared.samplesSupported;
    const bool rightSamples = mirrorRight ? rightPrepared.mirroredSamplesSupported : rightPrepared.samplesSupported;
    const bool leftSupport = leftPrepared.temporalSupported;
    const bool rightSupport = rightPrepared.temporalSupported;
    // Constant velocity is a valid motion. Descriptor novelty would reject
    // it precisely because successive velocity estimates correctly agree.
    // Actual displacement is checked by the trajectory-range gate instead.
    const bool leftDiversity = true;
    const bool rightDiversity = true;
    const auto alignment = staticPair ? TemporalAlignment{}
        : alignTemporal(a, b, params.temporalSimilarityThreshold);
    const bool temporalRun = staticPair || hasTemporalRun(a, b, left, right, params, alignment);
    const bool motionGateFails = leftPrepared.motionDelta < params.motionDeltaThreshold
        || rightPrepared.motionDelta < params.motionDeltaThreshold
        || leftPrepared.activeTransitionRatio < params.minActiveTransitionRatio
        || rightPrepared.activeTransitionRatio < params.minActiveTransitionRatio
        || leftPrepared.trajectoryRange < params.minMotionRange
        || rightPrepared.trajectoryRange < params.minMotionRange
        || !temporalRun;
    if (left.staticFrameSet != right.staticFrameSet
        || (staticPair && !params.allowStaticFrames)
        || a.empty() || b.empty()
        || !leftSamples || !rightSamples
        || !leftSupport || !rightSupport
        || !leftDiversity || !rightDiversity
        || (!staticPair && motionGateFails)) {
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
            std::fprintf(stderr,
                         "PF_DEBUG_MATCHER reject a=%zu b=%zu samples=%d/%d support=%d/%d diversity=%d/%d motion=%.4f/%.4f range=%.4f/%.4f active=%.3f/%.3f run=%d\n",
                         leftIndex, rightIndex, leftSamples ? 1 : 0, rightSamples ? 1 : 0,
                         leftSupport ? 1 : 0, rightSupport ? 1 : 0,
                         leftDiversity ? 1 : 0, rightDiversity ? 1 : 0,
                         leftPrepared.motionDelta, rightPrepared.motionDelta,
                         leftPrepared.trajectoryRange, rightPrepared.trajectoryRange,
                         leftPrepared.activeTransitionRatio,
                         rightPrepared.activeTransitionRatio, temporalRun ? 1 : 0);
        }
        result.similarity = 0.0;
        return result;
    }
    std::vector<Descriptor> alignedA, alignedB;
    for (const auto& [i, j] : alignment.path) {
        alignedA.push_back(a[i]); alignedB.push_back(b[j]);
    }
    // Score the supported trajectory, not the original window diagonal:
    // alignment has already found where the repeated gesture occurs.
    if (!staticPair && left.frames.front().keypoints.size() == 17
        && right.frames.front().keypoints.size() == 17) {
        const bool leftBody = leftPrepared.confidentBodyObserved, rightBody = rightPrepared.confidentBodyObserved;
        // Weak inferred elbows must not turn a head/camera trajectory into
        // the same body action. Both sides need confident homologous limbs.
        if (leftBody != rightBody) return result;
        if (leftBody && rightBody) {
            constexpr std::size_t chains[4][3] = {{5,7,9},{6,8,10},{11,13,15},{12,14,16}};
            constexpr std::size_t reflection[17] = {0,2,1,4,3,6,5,8,7,10,9,12,11,14,13,16,15};
            std::size_t supported = 0;
            for (const auto& [i, j] : alignment.path) {
                const auto& a = left.frames[i].keypoints;
                const auto& b = right.frames[j].keypoints;
                if (a.size() != 17 || b.size() != 17) continue;
                const auto visible = [](const auto& p) {
                    return std::isfinite(p.x) && std::isfinite(p.y)
                        && std::isfinite(p.confidence) && p.confidence >= .5;
                };
                if (std::any_of(std::begin(chains), std::end(chains), [&](const auto& chain) {
                    return std::all_of(std::begin(chain), std::end(chain), [&](std::size_t joint) {
                        return visible(a[joint]) && visible(b[mirrorRight ? reflection[joint] : joint]);
                    });
                })) ++supported;
            }
            if (supported * 4 < alignment.path.size() * 3) return result;
        } else if (identity.face) {
            result.headOnlyComparison = true;
            if (identity.score < params.minHeadFaceSimilarity) return result;
        }
    }
    const auto& scoreA = staticPair ? a : alignedA;
    const auto& scoreB = staticPair ? b : alignedB;
    if (!staticPair && activeBodyAgreement(scoreA, scoreB) < 0.35) return result;
    const std::size_t rows = scoreA.size(), cols = scoreB.size();
    const double noise = params.noiseFactor * std::max(leftPrepared.noise,
        mirrorRight ? rightPrepared.mirroredNoise : rightPrepared.noise);
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> previous(cols + 1, inf), current(cols + 1, inf);
    std::vector<std::size_t> previousSteps(cols + 1, 0), currentSteps(cols + 1, 0);
    previous[0] = 0.0;
    const std::size_t ratioBand = std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(
        static_cast<double>(std::max(rows, cols)) * params.sakoeChibaRatio)));
    const std::size_t band = std::max({params.dtwBand, ratioBand,
                                       rows > cols ? rows - cols : cols - rows});
    for (std::size_t i = 1; i <= rows; ++i) {
        std::fill(current.begin(), current.end(), inf);
        std::fill(currentSteps.begin(), currentSteps.end(), 0);
        const std::size_t begin = i > band ? i - band : 1;
        const std::size_t end = std::min(cols, i + band);
        for (std::size_t j = begin; j <= end; ++j) {
            // Alignment already measured these immutable descriptor pairs.
            // Reuse their exact distances instead of repeating all joint work.
            const double cached=staticPair ? std::numeric_limits<double>::infinity()
                : alignment.distances[alignment.path[i-1].first*alignment.columns+alignment.path[j-1].second];
            const double distance=std::isfinite(cached) ? cached : frameDistance(scoreA[i-1],scoreB[j-1]);
            const double cost = std::max(0.0, distance - std::min(noise, 0.02));
            double best = previous[j - 1];
            std::size_t bestSteps = previousSteps[j - 1];
            if (previous[j] < best) {
                best = previous[j];
                bestSteps = previousSteps[j];
            }
            if (current[j - 1] < best) {
                best = current[j - 1];
                bestSteps = currentSteps[j - 1];
            }
            if (std::isfinite(best)) {
                current[j] = cost + best;
                currentSteps[j] = bestSteps + 1;
            }
        }
        previous.swap(current);
        previousSteps.swap(currentSteps);
    }
    if (!std::isfinite(previous[cols]) || previousSteps[cols] == 0) return result;
    result.dtwDistance = previous[cols] / static_cast<double>(previousSteps[cols]);
    if (!staticPair && !alignment.path.empty()) {
        result.leftStartSeconds = left.frames[alignment.startLeft].timestampSeconds;
        result.leftEndSeconds = left.frames[alignment.endLeft].timestampSeconds;
        result.rightStartSeconds = right.frames[alignment.startRight].timestampSeconds;
        result.rightEndSeconds = right.frames[alignment.endRight].timestampSeconds;
        result.durationSeconds = std::min(result.leftEndSeconds - result.leftStartSeconds,
                                          result.rightEndSeconds - result.rightStartSeconds);
    }
    // Temporal alignment can move the representative starts closer together.
    // Enforce the same floor on the final pair, not only on input windows.
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds) < params.sameSourceGapFloorSec)
        return result;
    const double leftDuration = result.leftEndSeconds - result.leftStartSeconds;
    const double rightDuration = result.rightEndSeconds - result.rightStartSeconds;
    const double durationDenominator = std::max({leftDuration, rightDuration, 1e-9});
    const double timePenalty = params.timeWeight
        * std::abs(leftDuration - rightDuration) / durationDenominator;
    const double dtwScore = std::clamp(std::exp(-4.0 * result.dtwDistance), 0.0, 1.0);
    const double temporalScore = staticPair ? dtwScore : alignment.averageSimilarity;
    const double directionScore = staticPair ? 1.0 : velocityDirectionScore(alignedA, alignedB);
    // Opposite-direction trajectories can have an excellent DTW distance
    // because their amplitudes are similar. Require directional agreement
    // before accepting the movement as the same gesture.
    if (!staticPair && directionScore < 0.45) {
        result.similarity = 0.0;
        return result;
    }
    const double anatomyScore = shapeSimilarity(leftPrepared.poses, rightPoses);
    // Context is a sanity gate, not a replacement for motion. It suppresses
    // the common false-positive class where the same/front-facing actor is
    // present but the actual shot composition and trajectory do not agree.
    if (!staticPair && result.sceneSimilarity < 0.16
        && (anatomyScore < 0.72 || temporalScore < 0.72 || dtwScore < 0.72)) {
        result.similarity = 0.0;
        return result;
    }
    if (staticPair && std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr, "PF_DEBUG_MATCHER static-score a=%zu b=%zu dtw=%.4f anatomy=%.4f\n",
                     leftIndex, rightIndex, dtwScore, anatomyScore);
    }
    if (staticPair) {
        // Five facial landmarks contain much less information than a limb
        // skeleton. Require stronger actual landmark alignment, separately
        // from the final score; face identity/shoulder resemblance cannot
        // turn a different head position into a body-pose parallel.
        if (leftPrepared.headRegion && (dtwScore < std::max(.90, params.staticPoseSimilarityThreshold)))
            return result;
        // Pose agreement and proportions are complementary observations of
        // the same geometry, not independent probabilities. Use their
        // geometric mean; do not add a fictitious perfect direction score.
        double poseScore = std::sqrt(dtwScore * anatomyScore);
        const double articulation = identity.verified
            ? articulationSimilarity(leftPrepared.articulation, rightArticulation,
                                     leftPrepared.poses, rightPoses) : 0.0;
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr)
            std::fprintf(stderr, "PF_DEBUG_MATCHER articulation a=%zu b=%zu score=%.4f\n",
                leftIndex, rightIndex, articulation);
        poseScore = std::max(poseScore, articulation);
        // Background colour is displayed separately. Different lighting or
        // locations cannot make an independently verified pose less similar.
        // Holding the same pose for a different length is not a different
        // pose. Temporal support was already verified above; duration/speed
        // penalties are meaningful only for moving trajectories.
        result.similarity = (poseScore >= params.staticPoseSimilarityThreshold
            || articulation >= params.staticArticulationSimilarityThreshold)
            ? std::clamp(poseScore, 0.0, 0.994) : 0.0;
        if (result.faceVerified && !leftPrepared.bodyObserved && !rightPrepared.bodyObserved)
            result.headOnlyComparison = true;
        return result;
    }
    // The number shown to the editor is *motion* similarity. Body-ReID is a
    // required identity filter, not an extra 40 points for "same actor".
    // A weak component must visibly lower the score rather than being hidden
    // by two stronger averages.
    // Anatomy is deliberately not part of the primary score: the same
    // gesture can start from different neutral poses. It remains a small
    // sanity penalty below, while DTW and frame-wise trajectory agreement
    // decide whether the movement itself repeats.
    const double weightedMotion = 0.60 * dtwScore + 0.25 * temporalScore
        + 0.15 * directionScore;
    const double weakestMotionSignal = std::min({dtwScore, temporalScore, directionScore});
    double calibrated = 0.60 * weakestMotionSignal + 0.40 * weightedMotion;
    calibrated -= 0.10 * std::max(0.0, 0.65 - anatomyScore);
    if (result.sceneSimilarity < 0.35)
        calibrated -= 0.22 * (0.35 - result.sceneSimilarity) / 0.35;
    calibrated -= timePenalty;
    // Reserve the 95%+ band for agreement across all three signals.  A pair
    // with a good average but a weak temporal or anatomical component is a
    // candidate, never an "almost identical" movement.
    if (calibrated > 0.95
        && (dtwScore < 0.96 || temporalScore < 0.96 || anatomyScore < 0.96)) {
        calibrated = 0.949;
    }
    result.similarity = std::clamp(calibrated, 0.0, 0.994);
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr && result.similarity >= params.similarityThreshold)
        std::fprintf(stderr, "PF_DEBUG_MOTION_SCORE a=%zu b=%zu dtw=%.4f temporal=%.4f direction=%.4f anatomy=%.4f active=%.4f score=%.4f\n",
            leftIndex, rightIndex, dtwScore, temporalScore, directionScore, anatomyScore,
            activeBodyAgreement(scoreA, scoreB), result.similarity);
    return result;
}

} // namespace

struct MotionSearchReuse::Data {
    std::size_t budget;
    std::optional<std::array<std::uint8_t,32>> fingerprint;
    MotionMatcherParams params;
    std::vector<CompactComparison> raw;
    bool ready=false;
    std::unordered_map<std::uint64_t,bool> cameras;
    std::unique_ptr<MotionSearchReuse> recovery;
    std::array<std::size_t,7> counters{};
    explicit Data(std::size_t bytes):budget(std::min<std::size_t>(bytes,256U*1024U*1024U)) {}
};
MotionSearchReuse::MotionSearchReuse(std::size_t bytes):data_(std::make_unique<Data>(bytes)) {}
MotionSearchReuse::~MotionSearchReuse()=default;

MotionMatcher::MotionMatcher(MotionMatcherParams params) : params_(params) { setParams(params); }

void MotionMatcher::setParams(MotionMatcherParams params)
{
    if (!(params.staticArticulationSimilarityThreshold > 0.0 && params.staticArticulationSimilarityThreshold <= 1.0)
        || !(params.similarityThreshold >= 0.0 && params.similarityThreshold <= 1.0)
        || !(params.candidateThreshold >= 0.0 && params.candidateThreshold <= 1.0)
        || params.maxUniqueResults == 0 || params.maxComparisonThreads > 32
        || params.dtwBand == 0 || params.noiseFactor < 0.0)
        throw std::invalid_argument("MotionMatcher: invalid parameters");
    if (!(params.sakoeChibaRatio >= 0.0 && params.sakoeChibaRatio <= 1.0)
        || !(params.minRepeatGapSec >= 0.0 && params.sameFileGapSec >= 0.0)
        || !(params.crossFileGapSec >= 0.0 && params.duplicateWindowSec >= 0.0)
        || !(params.timeWeight >= 0.0 && params.timeWeight <= 1.0)
        || !(params.motionDeltaThreshold >= 0.0 && params.motionDeltaThreshold <= 1.0)
        || !(params.minActiveTransitionRatio >= 0.0 && params.minActiveTransitionRatio <= 1.0)
        || params.minMotionRange < 0.0
        || params.minMotionSpanSec < 0.0
        || !(params.temporalSimilarityThreshold >= 0.0
             && params.temporalSimilarityThreshold <= 1.0)
        || !(params.staticPoseSimilarityThreshold >= 0.0
             && params.staticPoseSimilarityThreshold <= 1.0)
        || params.minTemporalFrames < 3
        || params.minTemporalDurationSec < 0.0
        || params.sameSourceGapFloorSec < 0.0
        || !std::isfinite(params.sameSceneContextGapSec) || params.sameSceneContextGapSec < 0.0
        || !(params.sameSceneContextThreshold > 0.0 && params.sameSceneContextThreshold <= 1.0)
        || !(params.nmsOverlapThreshold >= 0.0 && params.nmsOverlapThreshold <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid temporal parameters");
    if (!(params.minAppearanceSimilarity >= -1.0
          && params.minAppearanceSimilarity <= 1.0)
        || !(params.appearanceWeight >= 0.0 && params.appearanceWeight <= 1.0)
        || !(params.minAppearanceEvidence >= 0.0 && params.minAppearanceEvidence <= 1.0)
        || !(params.minFaceSimilarity >= 0.0 && params.minFaceSimilarity <= 1.0)
        || !(params.minHeadFaceSimilarity >= 0.0 && params.minHeadFaceSimilarity <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid appearance parameters");
    params_ = params;
}

MotionMatch MotionMatcher::compare(const MotionWindow& left, const MotionWindow& right,
                                   std::size_t leftIndex, std::size_t rightIndex) const
{
    const auto leftPoses = normalizePoses(left, params_.normalizeSize);
    const auto rightPoses = normalizePoses(right, params_.normalizeSize);
    const MotionActivity leftActivity = motionActivity(left, leftPoses);
    const MotionActivity rightActivity = motionActivity(right, rightPoses);
    PreparedWindow leftPrepared;
    leftPrepared.articulation = articulationPoses(left);
    leftPrepared.poses = leftPoses;
    leftPrepared.descriptors = describe(leftPoses, left);
    leftPrepared.noise = noiseFloor(leftPrepared.descriptors);
    leftPrepared.motionDelta = leftActivity.meanDelta;
    leftPrepared.activeTransitionRatio = leftActivity.activeTransitionRatio;
    leftPrepared.trajectoryRange = leftActivity.trajectoryRange;
    prepareWindowFacts(leftPrepared, left, params_);
    prepareTorso(leftPrepared, left, params_);
    prepareCloseup(leftPrepared, left, params_);
    PreparedWindow rightPrepared;
    rightPrepared.articulation = articulationPoses(right);
    rightPrepared.poses = rightPoses;
    rightPrepared.descriptors = describe(rightPoses, right);
    rightPrepared.noise = noiseFloor(rightPrepared.descriptors);
    rightPrepared.motionDelta = rightActivity.meanDelta;
    rightPrepared.activeTransitionRatio = rightActivity.activeTransitionRatio;
    rightPrepared.trajectoryRange = rightActivity.trajectoryRange;
    prepareWindowFacts(rightPrepared, right, params_);
    prepareTorso(rightPrepared, right, params_);
    prepareCloseup(rightPrepared, right, params_);

    MotionMatch best = comparePrepared(left, right, leftPrepared, rightPrepared,
                                       params_, leftIndex, rightIndex);
    const bool headMirrorsAlreadyChecked = left.staticFrameSet && best.headOnlyComparison
        && (leftPrepared.closeup || rightPrepared.closeup);
    double directScore = best.unmirroredSimilarity >= 0 ? best.unmirroredSimilarity : best.similarity;
    if (params_.mirrorInvariant && !headMirrorsAlreadyChecked) {
        const MotionWindow mirroredSource = mirroredWindow(right);
        rightPrepared.mirroredPoses = normalizePoses(mirroredSource, params_.normalizeSize);
        rightPrepared.mirroredDescriptors = describe(rightPrepared.mirroredPoses, mirroredSource);
        rightPrepared.mirroredNoise = noiseFloor(rightPrepared.mirroredDescriptors);
        rightPrepared.mirroredArticulation = articulationPoses(mirroredSource);
        rightPrepared.mirroredSamplesSupported = hasDistinctTemporalSamples(right, rightPrepared.mirroredDescriptors, params_);
        const MotionMatch mirrored = comparePrepared(left, right, leftPrepared, rightPrepared,
                                                     params_, leftIndex, rightIndex, true);
        if (mirrored.similarity > best.similarity) best = mirrored;
    }
    if(best.similarity<params_.similarityThreshold && leftPrepared.torso && rightPrepared.torso) {
        auto alternative=comparePrepared(left,right,*leftPrepared.torso,*rightPrepared.torso,params_,leftIndex,rightIndex);
        const double alternativeDirect=alternative.similarity;
        if(params_.mirrorInvariant) {
            auto mirrored=comparePrepared(left,right,*leftPrepared.torso,*rightPrepared.torso,params_,leftIndex,rightIndex,true);
            if(mirrored.similarity>alternative.similarity)alternative=mirrored;
        }
        if(alternative.similarity>best.similarity) {
            best=alternative;
            directScore=std::max(directScore,alternativeDirect);
        }
    }
    best.unmirroredSimilarity = directScore;
    if (!measuredIdentityCompatible(left,best.leftStartSeconds,best.leftEndSeconds,left.staticFrameSet && best.headOnlyComparison)
        || !measuredIdentityCompatible(right,best.rightStartSeconds,best.rightEndSeconds,right.staticFrameSet && best.headOnlyComparison))
        best.similarity=best.rankScore=0;
    if (params_.individualPairs && (copiedScene(left,right)
        || (left.staticFrameSet && right.staticFrameSet && best.headOnlyComparison
            && copiedScene(left,right,true)))) best.similarity=best.rankScore=0;
    if (params_.bodyMotionOnly && !left.staticFrameSet && best.headOnlyComparison) best.similarity=best.rankScore=0;
    if (params_.individualPairs && left.staticFrameSet && right.staticFrameSet
        && sameCameraView(left, right)
        && best.headOnlyComparison)
        best.similarity = best.rankScore = 0;
    if(params_.individualPairs && left.staticFrameSet && right.staticFrameSet && !best.headOnlyComparison
        && left.sourceId==right.sourceId
        && copiedScene(left,right,true))
        best.similarity=best.rankScore=0;
    return best;
}

std::vector<MotionMatch> MotionMatcher::findAllPairs(const std::vector<MotionWindow>& windows,
                                                       const MotionSearchControl& control) const
{
    const bool profile = std::getenv("PF_DEBUG_ANALYSIS") != nullptr;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
    const auto report = [&](MotionSearchStage stage,std::size_t done,std::size_t total) {
        if(control.progress)control.progress(stage,done,total);
    };
    if(cancelled())return {};
    report(MotionSearchStage::Prepare,0,windows.size());
    std::vector<MotionMatch> matches;
    if (windows.size() < 2) return matches;
    matches.reserve(std::min(params_.maxUniqueResults, windows.size()));
    auto* reuse=control.reuse ? control.reuse->data_.get() : nullptr;
    if(reuse) {
        const auto fingerprint=geometryFingerprint(windows,control.cancelled);
        if(!fingerprint || reuse->fingerprint!=fingerprint || reuse->params!=params_) {
            std::vector<CompactComparison>().swap(reuse->raw);
            reuse->cameras.clear();reuse->ready=false;reuse->counters={};
            reuse->recovery.reset();
            reuse->fingerprint=fingerprint;reuse->params=params_;
        }
        if(!fingerprint)reuse=nullptr;
    }
    if(cancelled())return {};

    std::vector<PreparedWindow> prepared(windows.size());
    std::size_t preparedCount = 0;
    std::size_t preparedMotion = 0;
    std::size_t preparedStatic = 0;
    std::size_t embeddingDimension = 0;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if(cancelled())return {};
        if(index%64==0)report(MotionSearchStage::Prepare,index,windows.size());
        if (windows[index].frames.empty()) continue;
        const auto poses = normalizePoses(windows[index], params_.normalizeSize);
        prepared[index].poses = poses;
        prepared[index].articulation = articulationPoses(windows[index]);
        prepared[index].descriptors = describe(poses, windows[index]);
        prepared[index].noise = noiseFloor(prepared[index].descriptors);
        if (params_.mirrorInvariant) {
            const MotionWindow mirroredSource = mirroredWindow(windows[index]);
            prepared[index].mirroredPoses = normalizePoses(mirroredSource, params_.normalizeSize);
            prepared[index].mirroredArticulation = articulationPoses(mirroredSource);
            prepared[index].mirroredDescriptors = describe(prepared[index].mirroredPoses, mirroredSource);
            prepared[index].mirroredNoise = noiseFloor(prepared[index].mirroredDescriptors);
        }
        const MotionActivity activity = motionActivity(windows[index], poses);
        prepared[index].motionDelta = activity.meanDelta;
        prepared[index].activeTransitionRatio = activity.activeTransitionRatio;
        prepared[index].trajectoryRange = activity.trajectoryRange;
        prepareWindowFacts(prepared[index], windows[index], params_);
        prepareTorso(prepared[index], windows[index], params_);
        const bool staticWindow = windows[index].staticFrameSet;
        const auto supported = [&](const PreparedWindow& p) {
            return p.samplesSupported && p.temporalSupported
                && (staticWindow || (p.motionDelta >= params_.motionDeltaThreshold
                    && p.activeTransitionRatio >= params_.minActiveTransitionRatio
                    && p.trajectoryRange >= params_.minMotionRange));
        };
        if ((staticWindow && !params_.allowStaticFrames)
            || (!supported(prepared[index])
                && (!prepared[index].torso || !supported(*prepared[index].torso)))) {
            if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
                std::fprintf(stderr,
                    "PF_DEBUG_MATCHER filtered-window id=%zu t=%.3f frames=%zu reid=%.3f samples=%d diversity=%d delta=%.4f range=%.4f active=%.3f\n",
                    index, windows[index].frames.front().timestampSeconds, windows[index].frames.size(),
                    windows[index].appearanceConfidence,
                    prepared[index].samplesSupported ? 1 : 0,
                    hasTemporalDiversity(prepared[index].descriptors) ? 1 : 0,
                    activity.meanDelta, activity.trajectoryRange, activity.activeTransitionRatio);
            }
            prepared[index].descriptors.clear();
            continue;
        }
        ++preparedCount;
        prepareCloseup(prepared[index], windows[index], params_);
        if (staticWindow) ++preparedStatic;
        else {
            ++preparedMotion;
            if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
                std::fprintf(stderr,
                             "PF_DEBUG_MATCHER motion-window id=%zu t=%.3f track=%zu scene=%zu reid=%.3f delta=%.4f range=%.4f active=%.3f\n",
                             index,
                             windows[index].frames.empty() ? -1.0
                                 : windows[index].frames.front().timestampSeconds,
                             windows[index].trackId, windows[index].sceneIndex,
                             windows[index].appearanceConfidence,
                             prepared[index].motionDelta, prepared[index].trajectoryRange,
                             prepared[index].activeTransitionRatio);
            }
        }
        for (const auto& descriptor : prepared[index].descriptors)
            embeddingDimension = std::max(embeddingDimension, descriptor.size());
        for (const auto& descriptor : prepared[index].mirroredDescriptors)
            embeddingDimension = std::max(embeddingDimension, descriptor.size());
    }
    if (embeddingDimension == 0) return matches;
    const double prepareMs = profile ? elapsed() : 0;
    double indexBuildMs = 0, poseQueryMs = 0, appearanceBuildMs = 0, appearanceQueryMs = 0;
    // Identity descriptors do not change during this search. Reuse exact
    // prototype-pair evidence across overlapping chunks and mirror passes.
    // The bound prevents quadratic memory growth on very long sources.
    const auto identityPrototypes = identityPrototypeIds(windows, params_.minAppearanceEvidence);
    std::unordered_map<std::uint64_t, IdentityEvidence> identityMemo;
    constexpr std::size_t identityMemoLimit = 262144;
    identityMemo.reserve(std::min(identityMemoLimit, preparedCount * 64U));
    const auto evidence = [&](std::size_t i, std::size_t j) {
        const auto a = identityPrototypes[i], b = identityPrototypes[j];
        const auto key = (static_cast<std::uint64_t>(std::min(a,b)) << 32U) | std::max(a,b);
        if (const auto found = identityMemo.find(key); found != identityMemo.end()) return found->second;
        const auto value = identityEvidence(windows[i], windows[j], params_);
        if (identityMemo.size() < identityMemoLimit) identityMemo.emplace(key, value);
        return value;
    };
    std::unordered_map<std::string, std::size_t> shotIds;
    std::vector<std::size_t> shotOf(windows.size());
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto key = windows[i].sourceId + "\n" + (windows[i].hasSceneIndex
            ? std::to_string(windows[i].sceneIndex) : "unknown-" + std::to_string(i));
        shotOf[i] = shotIds.try_emplace(key, shotIds.size()).first->second;
    }
    const auto originalShotOf=shotOf;
    std::unordered_set<std::uint64_t> copiedShotPairs;
    std::unordered_set<std::uint64_t> returningHeadViews;
    const auto shotPairKey=[](std::size_t a,std::size_t b) {
        return (static_cast<std::uint64_t>(std::min(a,b))<<32U)|std::max(a,b);
    };
    if (params_.individualPairs) {
        std::vector<std::size_t> representatives(shotIds.size(),windows.size());
        for (std::size_t i=0;i<windows.size();++i)
            if (windows[i].hasSceneIndex && (representatives[shotOf[i]]==windows.size()
                || (!windows[i].sceneSequence.empty() && windows[representatives[shotOf[i]]].sceneSequence.empty())))
                representatives[shotOf[i]]=i;
        struct FootageSnapshot {
            std::string source;std::size_t scene=0;bool known=false;
            std::vector<float> context,pixels;std::vector<double> times;
            bool operator==(const FootageSnapshot&) const = default;
        };
        struct FootageMemo {
            std::vector<FootageSnapshot> inputs;
            std::unordered_set<std::uint64_t> copies;
        };
        // Face verification changes observations, not measured source pixels.
        // Reuse an EXACT picture/context/PTS snapshot during refinement; no
        // approximate hash, inherited identity, or stale source cache key.
        static thread_local std::vector<FootageMemo> footageMemo;
        std::vector<FootageSnapshot> snapshots;
        for(const auto i:representatives) {
            if(i==windows.size()) {snapshots.emplace_back();continue;}
            const auto& w=windows[i];snapshots.push_back({w.sourceId,w.sceneIndex,w.hasSceneIndex,w.sceneContext,w.sceneSequence,w.sceneSequenceTimes});
        }
        const auto cached=std::find_if(footageMemo.begin(),footageMemo.end(),[&](const auto& m){return m.inputs==snapshots;});
        if(cached!=footageMemo.end()) {
            copiedShotPairs=cached->copies;
            // Refinement alternates the full input with changing unused shots.
            // Keep the reused full snapshot instead of evicting it on the next
            // recovery miss and measuring the same camera pairs again.
            std::rotate(cached,std::next(cached),footageMemo.end());
        }
        else {
        report(MotionSearchStage::Footage,0,representatives.size());
        // Exact necessary bounds prune impossible copies without dropping any
        // passing pixel proof. Assign verification work dynamically by pair.
        const auto footageCandidates=copiedFootageCandidates(windows,representatives,control);
        if(cancelled())return {};
        if(profile)std::fprintf(stderr,"PF_DEBUG_FOOTAGE candidate_shot_pairs=%zu total_shots=%zu\n",footageCandidates.size(),representatives.size());
        std::atomic_size_t nextRow{0}, completedRows{0};
        const auto scanCopies=[&] {
            std::vector<std::uint64_t> copies;
            for (;;) {
                const auto entry=nextRow.fetch_add(1);
                if(entry>=footageCandidates.size() || cancelled())break;
                const auto key=footageCandidates[entry];
                const auto a=static_cast<std::size_t>(key>>32U),b=static_cast<std::size_t>(key&0xffffffffULL);
                if(copiedScene(windows[representatives[a]],windows[representatives[b]],false,control.cancelled))
                    copies.push_back(key);
                ++completedRows;
            }
            return copies;
        };
        const auto cpu=std::max(1U,std::thread::hardware_concurrency());
        const auto workers=std::min<std::size_t>(footageCandidates.size(),representatives.size()<64 ? 1U
            : std::min<std::size_t>(params_.maxComparisonThreads ? params_.maxComparisonThreads : cpu,32));
        std::vector<std::future<std::vector<std::uint64_t>>> scans;
        for(std::size_t worker=0;worker<workers;++worker)scans.push_back(std::async(std::launch::async,scanCopies));
        for(auto& scan:scans) {
            while(scan.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)
                report(MotionSearchStage::Footage,completedRows.load(),footageCandidates.size());
            const auto copies=scan.get();copiedShotPairs.insert(copies.begin(),copies.end());
        }
        if(cancelled())return {};
        footageMemo.push_back({std::move(snapshots),copiedShotPairs});
        if(footageMemo.size()>2)footageMemo.erase(footageMemo.begin());
        }
        std::vector<std::size_t> canonical(shotIds.size());std::iota(canonical.begin(),canonical.end(),0);
        const auto root=[&](std::size_t shot) {
            while (canonical[shot]!=shot) shot=canonical[shot];
            return shot;
        };
        // A long measured shot can overlap two disjoint shorter edits. All
        // edits share its one-shot quota, even when the short edits do not
        // overlap each other. This concerns copied pixels, never face identity.
        for (const auto edge:copiedShotPairs) {
            const auto a=root(edge>>32U),b=root(edge&0xffffffffULL);
            canonical[std::max(a,b)]=std::min(a,b);
        }
        std::size_t copies=0;
        for (std::size_t shot=0;shot<canonical.size();++shot) {canonical[shot]=root(shot);copies+=canonical[shot]!=shot;}
        for (auto& shot:shotOf) shot=canonical[shot];
        if (profile) std::fprintf(stderr,"PF_DEBUG_FOOTAGE copied_shots=%zu\n",copies);
    }

    std::vector<std::size_t> cameraRepresentatives(shotIds.size(),windows.size());
    for(std::size_t i=0;i<windows.size();++i)
        if(windows[i].hasSceneIndex && (cameraRepresentatives[originalShotOf[i]]==windows.size()
            || (!windows[i].sceneSequence.empty() && windows[cameraRepresentatives[originalShotOf[i]]].sceneSequence.empty())))
            cameraRepresentatives[originalShotOf[i]]=i;
    FootageStatsCache cameraStatistics;
    std::unordered_set<std::uint64_t> checkedHeadPairs;
    const auto returningHeadView=[&](std::size_t a,std::size_t b) {
        const auto key=shotPairKey(a,b);
        if(reuse)if(const auto entry=reuse->cameras.find(key);entry!=reuse->cameras.end()) {
            if(entry->second)returningHeadViews.insert(key);
            return entry->second;
        }
        if(returningHeadViews.contains(key))return true;
        if(a==b || copiedShotPairs.contains(key) || !checkedHeadPairs.insert(key).second)return false;
        const auto i=cameraRepresentatives[a],j=cameraRepresentatives[b];
        if(i!=windows.size() && j!=windows.size()
            && copiedScene(windows[i],windows[j],true,control.cancelled,&cameraStatistics)) {
            if(!cancelled() && reuse && reuse->cameras.size()<reuse->budget/(4*64))reuse->cameras.emplace(key,true);
            returningHeadViews.insert(key);return true;
        }
        if(!cancelled() && reuse && reuse->cameras.size()<reuse->budget/(4*64))reuse->cameras.emplace(key,false);
        return false;
    };
    report(MotionSearchStage::Retrieval,0,preparedCount);

    // HNSW returns a bounded candidate set. The union of both query
    // directions keeps the all-pairs semantics while making DTW the expensive
    // second pass rather than the first operation on every pair. Do not put
    // static-pose and motion windows in the same nearest-neighbour index:
    // near-static silhouettes otherwise crowd the bounded list and prevent
    // motion candidates from ever reaching the temporal matcher.
    std::unordered_set<std::uint64_t> candidatePairs;
    std::size_t appearanceCandidatePairs=0,gapPassed=0,coarsePassed=0,compared=0;
    std::size_t trackRejected=0,sceneRejected=0,staticRejected=0,candidateSummary=0;
    double comparedMs=0;
    const bool reused= reuse && reuse->ready;
    if(reused) {
        matches.reserve(reuse->raw.size());
        for(const auto& m:reuse->raw) {
            if(cancelled())return {};
            matches.push_back(m.restore(windows));
        }
        candidateSummary=reuse->counters[0];compared=reuse->counters[1];
        appearanceCandidatePairs=reuse->counters[2];gapPassed=reuse->counters[3];
        coarsePassed=reuse->counters[4];trackRejected=reuse->counters[5];sceneRejected=reuse->counters[6];
        report(MotionSearchStage::Compare,compared,compared);
    } else {
    candidatePairs.reserve(preparedCount * 24U);
    const auto sameKnownShot = [&](std::size_t i, std::size_t j) {
        return windows[i].hasSceneIndex && windows[j].hasSceneIndex
            && (shotOf[i]==shotOf[j] || copiedShotPairs.contains(shotPairKey(originalShotOf[i],originalShotOf[j])));
    };
    const auto needsIdentity = [&](std::size_t i, std::size_t j) {
        const bool crossTrack = windows[i].sourceId == windows[j].sourceId
            && params_.requireSameTrackWithinSource
            && windows[i].trackId != 0 && windows[j].trackId != 0
            && differentTrackSegment(windows[i], windows[j]);
        // Apply exactly the identity gate of comparePrepared before counting
        // ANN slots. Contradictory people must not hide a valid other shot.
        // Optional identity matching remains optional when that gate is off.
        return crossTrack || params_.requireAppearance;
    };
    const bool wideRetrieval = params_.expandedSearch || params_.individualPairs;
    // Individual diversity already broadens the default search. Repeat search
    // must still add an independent retrieval tier, including motion-only
    // analysis where there are no short static windows to derive.
    const std::size_t poseNeighbourBudget = params_.expandedSearch ? 384 : wideRetrieval ? 192 : 96;
    const std::size_t identityNeighbourBudget = params_.expandedSearch ? 256 : wideRetrieval ? 128 : 64;
    const unsigned neighboursPerShot = params_.expandedSearch ? 12U : wideRetrieval ? 8U : 4U;
    if (profile)
        std::fprintf(stderr, "PF_DEBUG_SEARCH expanded=%d pose_neighbours=%zu identity_neighbours=%zu neighbours_per_shot=%u\n",
            params_.expandedSearch ? 1 : 0, poseNeighbourBudget, identityNeighbourBudget, neighboursPerShot);
    std::atomic_size_t retrieved{0};
    for (const bool staticWindow : {false, true}) {
        std::vector<std::size_t> group;
        group.reserve(preparedCount);
        MotionIndex index;
        MotionIndex torsoIndex;
        std::size_t torsoCount = 0;
        for (std::size_t windowIndex = 0; windowIndex < prepared.size(); ++windowIndex) {
            auto& item = prepared[windowIndex];
            if (item.descriptors.empty() || windows[windowIndex].staticFrameSet != staticWindow)
                continue;
            item.embedding = embedding(item.descriptors, embeddingDimension);
            if (params_.mirrorInvariant)
                item.mirroredEmbedding = embedding(item.mirroredDescriptors, embeddingDimension);
            index.add(windowIndex, item.embedding);
            if(item.torso) {
                auto& p=*item.torso;
                p.embedding=embedding(p.descriptors,embeddingDimension);
                if(params_.mirrorInvariant)
                    p.mirroredEmbedding=embedding(p.mirroredDescriptors,embeddingDimension);
                torsoIndex.add(windowIndex,p.embedding);
                ++torsoCount;
            }
            group.push_back(windowIndex);
        }
        if (group.size() < 2) continue;
        auto stageStart = profile ? elapsed() : 0;
        index.build();
        if(torsoCount>1)torsoIndex.build();
        if (profile) indexBuildMs += elapsed() - stageStart;
        stageStart = profile ? elapsed() : 0;
        const std::size_t candidateCount = std::min<std::size_t>(group.size(),
            std::max<std::size_t>(24, std::min<std::size_t>(poseNeighbourBudget, group.size())));
        const double retrievalThreshold = params_.candidateThreshold * 0.65;
        const auto retrieveRange = [&](std::size_t begin,std::size_t end,bool serial) {
          std::unordered_set<std::uint64_t> foundPairs;
          std::unordered_map<std::uint64_t,IdentityEvidence> localIdentity;
          constexpr std::size_t localMemoLimit=8192;
          const auto allowed = [&](std::size_t i,std::size_t j) {
            if(!needsIdentity(i,j))return true;
            if(serial)return evidence(i,j).verified;
            const auto a=identityPrototypes[i],b=identityPrototypes[j];
            const auto key=(static_cast<std::uint64_t>(std::min(a,b))<<32U)|std::max(a,b);
            if(const auto f=localIdentity.find(key);f!=localIdentity.end())return f->second.verified;
            const auto value=identityEvidence(windows[i],windows[j],params_);
            if(localIdentity.size()<localMemoLimit)localIdentity.emplace(key,value);
            return value.verified;
          };
          for(auto position=begin;position<end;++position) {
            if(cancelled())break;
            const auto i=group[position];
            const auto collect = [&](const MotionIndex& queryIndex,const std::vector<double>& queryEmbedding) {
                // Sliding windows from one long shot can occupy all 96 ANN
                // slots. They are rejected later, hiding a valid other shot.
                // Grow retrieval only when needed, counting usable neighbours
                // rather than same-shot or disallowed-identity observations.
                // Keep expensive DTW bounded.
                const auto limit = std::min(group.size(), candidateCount * (wideRetrieval ? 16 : 8));
                MotionIndex::QueryWorkspace queryWorkspace;
                for (auto requested = candidateCount;; requested = std::min(limit, requested * 2)) {
                    if(cancelled())return;
                    std::size_t usable = 0;
                    std::unordered_map<std::size_t, std::size_t> shotSlots;
                    for (const auto neighbour : queryIndex.query(queryEmbedding, requested, requested * 2, queryWorkspace)) {
                        if (neighbour.id == i || sameKnownShot(i, neighbour.id)
                            || neighbour.similarity + 1e-9 < retrievalThreshold) continue;
                        if (!allowed(i, neighbour.id)) continue;
                        // One other long shot must not fill every ANN slot.
                        // Keep several temporal alternatives, then retrieve
                        // deeper for independently edited shots.
                        if (windows[neighbour.id].hasSceneIndex && shotSlots[shotOf[neighbour.id]]++ >= neighboursPerShot) continue;
                        const auto left = std::min(i, neighbour.id);
                        const auto right = std::max(i, neighbour.id);
                        foundPairs.insert((static_cast<std::uint64_t>(left) << 32U)
                                              | static_cast<std::uint64_t>(right));
                        if (++usable >= candidateCount) break;
                    }
                    if (usable >= candidateCount || requested >= limit) break;
                }
            };
            collect(index,prepared[i].embedding);
            if (params_.mirrorInvariant && !prepared[i].mirroredEmbedding.empty())
                collect(index,prepared[i].mirroredEmbedding);
            if(torsoCount>1 && prepared[i].torso) {
                collect(torsoIndex,prepared[i].torso->embedding);
                if(params_.mirrorInvariant)collect(torsoIndex,prepared[i].torso->mirroredEmbedding);
            }
            ++retrieved;
            if(serial && control.progress)report(MotionSearchStage::Retrieval,retrieved.load(),preparedCount);
          }
          return foundPairs;
        };
        const auto retrievalThreads=group.size()<128 ? 1U : std::clamp<std::size_t>(
            params_.maxComparisonThreads ? params_.maxComparisonThreads : std::thread::hardware_concurrency(),1,24);
        if(retrievalThreads==1) {
            auto pairs=retrieveRange(0,group.size(),true);
            candidatePairs.insert(pairs.begin(),pairs.end());
        } else {
            std::vector<std::future<std::unordered_set<std::uint64_t>>> workers;
            for(std::size_t worker=0;worker<retrievalThreads;++worker) {
                const auto begin=group.size()*worker/retrievalThreads,end=group.size()*(worker+1)/retrievalThreads;
                workers.push_back(std::async(std::launch::async,retrieveRange,begin,end,false));
            }
            for(auto& worker:workers) {
                while(worker.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)
                    report(MotionSearchStage::Retrieval,retrieved.load(),preparedCount);
                const auto pairs=worker.get();
                candidatePairs.insert(pairs.begin(),pairs.end());
            }
        }
        report(MotionSearchStage::Retrieval,retrieved.load(),preparedCount);
        if(cancelled())return {};
        if (profile) poseQueryMs += elapsed() - stageStart;
    }

    // Pose retrieval is intentionally broad, but it still misses the same
    // actor when a cut changes camera angle or gesture. Use bounded ANN
    // retrieval for body/face embeddings as a second channel. Embeddings are
    // L2-normalized by their estimators, so Euclidean ANN preserves cosine
    // neighbourhood ordering without an O(N^2) scan.
    const std::size_t maxAppearanceNeighbours = identityNeighbourBudget;
    auto addAppearanceCandidates = [&](bool useFace, bool staticWindow) {
        MotionIndex identityIndex;
        std::vector<std::size_t> group;
        for (std::size_t i = 0; i < prepared.size(); ++i) {
            if (prepared[i].descriptors.empty() || windows[i].staticFrameSet != staticWindow) continue;
            const auto& source = useFace ? windows[i].faceEmbedding : windows[i].appearanceEmbedding;
            const double confidence = useFace ? windows[i].faceConfidence : windows[i].appearanceConfidence;
            if (source.empty() || confidence < params_.minAppearanceEvidence) continue;
            std::vector<double> vector(source.begin(), source.end());
            identityIndex.add(i, std::move(vector));
            group.push_back(i);
        }
        if (group.size() < 2) return;
        auto stageStart = profile ? elapsed() : 0;
        identityIndex.build();
        if (profile) appearanceBuildMs += elapsed() - stageStart;
        stageStart = profile ? elapsed() : 0;
        const std::size_t count = std::min<std::size_t>(maxAppearanceNeighbours + 1, group.size());
        // Identical prototypes and query widths have identical ordered ANN
        // results. Cache that raw traversal, then apply every window's own
        // shot/identity filters. Never cache a filtered candidate list.
        // Both caches are bounded, independently of source duration.
        const std::size_t maxPrototypeWorkspaces = (2U * 1024U * 1024U) / identityIndex.size();
        std::unordered_map<std::size_t, MotionIndex::QueryWorkspace> prototypeWorkspaces;
        std::unordered_map<std::uint64_t,std::vector<MotionIndex::Neighbor>> queryMemo;
        std::size_t queryMemoBytes=0;
        constexpr std::size_t queryMemoLimit=16U*1024U*1024U;
        for (const std::size_t i : group) {
            if(cancelled())return;
            const auto& source = useFace ? windows[i].faceEmbedding : windows[i].appearanceEmbedding;
            std::vector<double> query(source.begin(), source.end());
            const auto limit = std::min(group.size(), count * 8);
            MotionIndex::QueryWorkspace localWorkspace;
            auto foundWorkspace = prototypeWorkspaces.find(identityPrototypes[i]);
            if (foundWorkspace == prototypeWorkspaces.end()
                && prototypeWorkspaces.size() < maxPrototypeWorkspaces)
                foundWorkspace = prototypeWorkspaces.try_emplace(identityPrototypes[i]).first;
            auto& queryWorkspace = foundWorkspace == prototypeWorkspaces.end()
                ? localWorkspace : foundWorkspace->second;
            for (auto requested = count;; requested = std::min(limit, requested * 2)) {
                std::size_t usable = 0;
                std::unordered_map<std::size_t, std::size_t> shotSlots;
                if(cancelled())return;
                const auto key=(static_cast<std::uint64_t>(identityPrototypes[i])<<32U)|requested;
                const auto memo=queryMemo.find(key);
                std::vector<MotionIndex::Neighbor> queried;
                if(memo==queryMemo.end())queried=identityIndex.query(query,requested,requested*2,queryWorkspace);
                const auto& neighbours=memo==queryMemo.end() ? queried : memo->second;
                for (const auto neighbour : neighbours) {
                    const std::size_t j = neighbour.id;
                    if (i == j || j >= windows.size() || sameKnownShot(i, j)) continue;
                    if (!evidence(i,j).verified) continue;
                    if (windows[j].hasSceneIndex && shotSlots[shotOf[j]]++ >= neighboursPerShot) continue;
                    const auto left = std::min(i, j);
                    const auto right = std::max(i, j);
                    if (candidatePairs.insert((static_cast<std::uint64_t>(left) << 32U)
                                              | static_cast<std::uint64_t>(right)).second)
                        ++appearanceCandidatePairs;
                    if (++usable >= maxAppearanceNeighbours) break;
                }
                if(memo==queryMemo.end() && queryMemoBytes+queried.size()*sizeof(MotionIndex::Neighbor)<=queryMemoLimit) {
                    queryMemoBytes+=queried.size()*sizeof(MotionIndex::Neighbor);
                    queryMemo.emplace(key,std::move(queried));
                }
                if (usable >= maxAppearanceNeighbours || requested >= limit) break;
            }
        }
        if (profile) appearanceQueryMs += elapsed() - stageStart;
    };
    for (const bool staticWindow : {false, true}) {
        addAppearanceCandidates(false, staticWindow);
        addAppearanceCandidates(true, staticWindow);
    }

    const double compareStart = profile ? elapsed() : 0;
    struct ComparisonTask { std::size_t i, j; IdentityEvidence identity; };
    std::vector<ComparisonTask> comparisonTasks;
    comparisonTasks.reserve(candidatePairs.size());
    for (const std::uint64_t key : candidatePairs) {
        if(cancelled())return {};
        const std::size_t i = static_cast<std::size_t>(key >> 32U);
        const std::size_t j = static_cast<std::size_t>(key & 0xffffffffULL);
        if (i >= windows.size() || j >= windows.size() || i >= j) continue;
        if (params_.individualPairs && sameKnownShot(i,j)) {++sceneRejected;continue;}
        const bool sameSource = windows[i].sourceId == windows[j].sourceId;
        if (windows[i].staticFrameSet != windows[j].staticFrameSet) { ++staticRejected; continue; }
        if (sameSource && params_.requireSameTrackWithinSource
            && windows[i].trackId != 0 && windows[j].trackId != 0
            && differentTrackSegment(windows[i], windows[j])) {
            const bool sameAppearance = evidence(i,j).verified;
            if (!sameAppearance) {
                if (std::getenv("PF_DEBUG_MATCHER") != nullptr && !windows[i].staticFrameSet) {
                    std::fprintf(stderr,
                                 "PF_DEBUG_MATCHER reject-motion-identity a=%zu b=%zu t=%.3f/%.3f reid=%.3f/%.3f cosine=%.3f\n",
                                 i, j,
                                 windows[i].frames.empty() ? -1.0 : windows[i].frames.front().timestampSeconds,
                                 windows[j].frames.empty() ? -1.0 : windows[j].frames.front().timestampSeconds,
                                 windows[i].appearanceConfidence, windows[j].appearanceConfidence,
                                 appearanceCosine(windows[i].appearanceEmbedding,
                                                  windows[j].appearanceEmbedding));
                }
                ++trackRejected;
                continue;
            }
        }
        if (sameSource && windows[i].hasSceneIndex && windows[j].hasSceneIndex
            && windows[i].sceneIndex == windows[j].sceneIndex) {
            ++sceneRejected;
            continue;
        }
        const double leftStart = windows[i].frames.front().timestampSeconds;
        const double rightStart = windows[j].frames.front().timestampSeconds;
        const double gap = std::abs(leftStart - rightStart);
        const auto pairIdentity = evidence(i,j);
        const double requiredGap = sameSource
            ? (separateVerifiedShots(windows[i], windows[j], params_, &pairIdentity) ? params_.sameSourceGapFloorSec
               : std::max({params_.sameSourceGapFloorSec, params_.sameFileGapSec,
                           params_.minRepeatGapSec}))
            : params_.crossFileGapSec;
        if (gap < requiredGap) continue;
        ++gapPassed;
        // The index is a retrieval stage, not the score shown to the user.
        // Keep its recall broad, then let DTW plus the continuous temporal
        // run make the final acceptance decision.
        const bool identityCandidate = pairIdentity.verified;
        if (!identityCandidate) {
            double coarse = coarseSimilarity(prepared[i].descriptors, prepared[j].descriptors);
            if (params_.mirrorInvariant && !prepared[j].mirroredDescriptors.empty())
                coarse = std::max(coarse, coarseSimilarity(prepared[i].descriptors,
                                                           prepared[j].mirroredDescriptors));
            if (coarse < params_.candidateThreshold * 0.65) continue;
        }
        ++coarsePassed;
        ++compared;
        comparisonTasks.push_back({i, j, pairIdentity});
    }
    candidateSummary=candidatePairs.size();
    // The ordered work list now owns every candidate and identity fact.
    // A long input can have millions of hash nodes: release that duplicate
    // storage before allocating the workers' accepted results.
    std::unordered_set<std::uint64_t>().swap(candidatePairs);
    // Identity memo insertions and filtering finish before workers start.
    // Prepared geometry, model outputs and identity facts are immutable;
    // workers own their DTW scratch and accepted-result vectors. Merge in
    // original candidate order before deterministic NMS/shot selection.
    std::atomic_size_t completedComparisons{0};
    report(MotionSearchStage::Compare,0,comparisonTasks.size());
    const auto compareRange = [&](std::size_t begin, std::size_t end) {
        std::vector<MotionMatch> accepted;
        for (std::size_t taskIndex = begin; taskIndex < end; ++taskIndex) {
        if(cancelled())break;
        const auto done=completedComparisons.load();
        struct Completed {std::atomic_size_t& count;~Completed(){++count;}} completion{completedComparisons};
        // Serial searches have no future-polling loop to publish progress.
        if(comparisonTasks.size()<4096 || params_.maxComparisonThreads==1)
            if(done%64==0)report(MotionSearchStage::Compare,done,comparisonTasks.size());
        const auto& task = comparisonTasks[taskIndex];
        const auto i = task.i, j = task.j;
        const auto& pairIdentity = task.identity;
        const bool sameSource = windows[i].sourceId == windows[j].sourceId;
        const double gap = std::abs(windows[i].frames.front().timestampSeconds
                                     - windows[j].frames.front().timestampSeconds);
        MotionMatch candidate = comparePrepared(windows[i], windows[j], prepared[i], prepared[j],
                                                params_, i, j, false, &pairIdentity);
        double directScore = candidate.unmirroredSimilarity >= 0
            ? candidate.unmirroredSimilarity : candidate.similarity;
        const bool headMirrorsAlreadyChecked = windows[i].staticFrameSet && candidate.headOnlyComparison
            && (prepared[i].closeup || prepared[j].closeup);
        if (params_.mirrorInvariant && !headMirrorsAlreadyChecked && !prepared[j].mirroredDescriptors.empty()) {
            const MotionMatch mirroredCandidate = comparePrepared(windows[i], windows[j],
                                                                  prepared[i], prepared[j], params_, i, j, true, &pairIdentity);
            if (mirroredCandidate.similarity > candidate.similarity) candidate = mirroredCandidate;
        }
        if(candidate.similarity<params_.similarityThreshold && prepared[i].torso && prepared[j].torso) {
            auto alternative=comparePrepared(windows[i],windows[j],*prepared[i].torso,*prepared[j].torso,
                params_,i,j,false,&pairIdentity);
            const double alternativeDirect=alternative.similarity;
            if(params_.mirrorInvariant) {
                auto mirrored=comparePrepared(windows[i],windows[j],*prepared[i].torso,*prepared[j].torso,
                    params_,i,j,true,&pairIdentity);
                if(mirrored.similarity>alternative.similarity)alternative=mirrored;
            }
            if(alternative.similarity>candidate.similarity) {
                candidate=alternative;
                directScore=std::max(directScore,alternativeDirect);
            }
        }
        candidate.unmirroredSimilarity = directScore;
        // Defer recurring portraits until established body/motion coverage is
        // frozen below; deleting these edges here can change augmenting paths
        // and replace an unrelated, useful body pair.
        if (params_.bodyMotionOnly && !windows[i].staticFrameSet && candidate.headOnlyComparison) continue;
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
            std::fprintf(stderr, "PF_DEBUG_MATCHER compared a=%zu b=%zu t=%.3f/%.3f static=%d/%d similarity=%.3f dtw=%.3f appearance=%.3f\n",
                         i, j,
                         windows[i].frames.empty() ? -1.0 : windows[i].frames.front().timestampSeconds,
                         windows[j].frames.empty() ? -1.0 : windows[j].frames.front().timestampSeconds,
                         windows[i].staticFrameSet ? 1 : 0, windows[j].staticFrameSet ? 1 : 0,
                         candidate.similarity, candidate.dtwDistance,
                         candidate.appearanceSimilarity);
        }
        if (candidate.similarity < params_.similarityThreshold) continue;
        // Repeated reverse cuts of the same held pose in a conversation are
        // not independent montage material. Require both near-identical pose
        // AND measured visual context; context alone cannot reject a gesture.
        // Distant scenes and motion trajectories remain eligible.
        const bool repeatedView = windows[i].staticFrameSet && sameSource
            && !windows[i].sceneContext.empty() && !windows[j].sceneContext.empty()
            && gap <= 180.0 && candidate.sceneSimilarity >= 0.97
            && shapeSimilarity(prepared[i].poses, prepared[j].poses)
                >= ((!prepared[i].bodyObserved && !prepared[j].bodyObserved) ? 0.98 : 0.86);
        if (!repeatedView && candidate.similarity >= params_.similarityThreshold)
            accepted.push_back(std::move(candidate));
        }
        return accepted;
    };
    const auto automaticThreads = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 32);
    const auto comparisonThreads = comparisonTasks.size() < 4096 ? 1U
        : std::clamp<std::size_t>(params_.maxComparisonThreads ? params_.maxComparisonThreads : automaticThreads, 1, 32);
    if (comparisonThreads == 1) matches = compareRange(0, comparisonTasks.size());
    else {
        std::vector<std::future<std::vector<MotionMatch>>> workers;
        workers.reserve(comparisonThreads);
        for (std::size_t worker = 0; worker < comparisonThreads; ++worker) {
            const auto begin = comparisonTasks.size() * worker / comparisonThreads;
            const auto end = comparisonTasks.size() * (worker + 1) / comparisonThreads;
            workers.push_back(std::async(std::launch::async, compareRange, begin, end));
        }
        for (auto& worker : workers) {
            while(worker.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)
                report(MotionSearchStage::Compare,completedComparisons.load(),comparisonTasks.size());
            auto accepted = worker.get();
            matches.insert(matches.end(), std::make_move_iterator(accepted.begin()),
                            std::make_move_iterator(accepted.end()));
        }
    }
    report(MotionSearchStage::Compare,completedComparisons.load(),comparisonTasks.size());
    // No worker borrows these tasks after all futures have joined. Reuse and
    // selection need only accepted hypotheses, not millions of rejected tasks.
    std::vector<ComparisonTask>().swap(comparisonTasks);
    comparedMs = profile ? elapsed() - compareStart : 0;
    if(reuse && !cancelled() && matches.size()<=reuse->budget*3/4/sizeof(CompactComparison)
        && std::all_of(matches.begin(),matches.end(),[](const auto& m){return m.directionLabel.empty() && m.gestureLabel.empty();})) {
        reuse->raw.reserve(matches.size());
        for(const auto& m:matches)reuse->raw.emplace_back(m);
        reuse->ready=true;
        reuse->counters={candidateSummary,compared,appearanceCandidatePairs,gapPassed,coarsePassed,trackRejected,sceneRejected};
    }
    }
    if(profile && reuse)std::fprintf(stderr,"PF_DEBUG_REUSE comparison_hit=%d raw_cached=%zu camera_cached=%zu budget_bytes=%zu\n",
        reused?1:0,reuse->raw.size(),reuse->cameras.size(),reuse->budget);
    if(cancelled())return {};
    report(MotionSearchStage::Select,0,matches.size());
    const double selectionStart = profile ? elapsed() : 0;
    const std::size_t rawAccepted = matches.size();
    if(params_.individualPairs) {
        // Several body windows can represent the same shot pair. Prove its
        // returning-camera layout once, using shared immutable frame statistics
        // and bounded workers; head/expression noise must not bypass this proof.
        std::unordered_set<std::uint64_t> bodyKeys;
        for(const auto& m:matches) {
            if(cancelled())return {};
            const auto& a=windows[m.leftIndex];const auto& b=windows[m.rightIndex];
            if(!a.staticFrameSet || m.headOnlyComparison || a.sourceId!=b.sourceId)continue;
            const auto key=shotPairKey(originalShotOf[m.leftIndex],originalShotOf[m.rightIndex]);
            if(reuse && reuse->cameras.contains(key))continue;
            bodyKeys.insert(key);
        }
        std::vector<std::uint64_t> tasks(bodyKeys.begin(),bodyKeys.end());
        std::unordered_set<std::uint64_t>().swap(bodyKeys);
        std::vector<std::uint8_t> decisions(tasks.size());
        std::atomic_size_t next{0},done{0};
        const auto scan=[&] {
            for(;;) {
                const auto n=next.fetch_add(1);
                if(n>=tasks.size() || cancelled())return;
                const auto a=static_cast<std::size_t>(tasks[n]>>32U),b=static_cast<std::size_t>(tasks[n]&0xffffffffULL);
                const auto i=cameraRepresentatives[a],j=cameraRepresentatives[b];
                decisions[n]=i!=windows.size() && j!=windows.size()
                    && copiedScene(windows[i],windows[j],true,control.cancelled,&cameraStatistics);
                ++done;
            }
        };
        const auto workers=std::min<std::size_t>(tasks.size(),tasks.size()<64 ? 1U
            : std::clamp<std::size_t>(params_.maxComparisonThreads ? params_.maxComparisonThreads
                : std::thread::hardware_concurrency(),1,8));
        if(!tasks.empty())report(MotionSearchStage::Camera,0,tasks.size());
        std::vector<std::future<void>> jobs;
        for(std::size_t n=0;n<workers;++n)jobs.push_back(std::async(std::launch::async,scan));
        for(auto& job:jobs) {
            while(job.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)
                report(MotionSearchStage::Camera,done.load(),tasks.size());
            job.get();
        }
        if(cancelled())return {};
        for(std::size_t n=0;n<tasks.size();++n) {
            checkedHeadPairs.insert(tasks[n]);
            if(decisions[n])returningHeadViews.insert(tasks[n]);
            if(reuse && reuse->cameras.size()<reuse->budget/(4*64))reuse->cameras.emplace(tasks[n],decisions[n]!=0);
        }
        if(!tasks.empty())report(MotionSearchStage::Camera,tasks.size(),tasks.size());
    }
    // A held body pose in the same returning camera is also a duplicate.
    // Require three matching decoded source-frame layouts. A head/expression
    // change cannot give the same held body/camera
    // a new exclusive-pose card. Moving gestures retain their lane;
    // different backgrounds cannot be rejected by pose resemblance alone.
    std::erase_if(matches,[&](const auto& m) {
        if(cancelled())return false;
        const auto& a=windows[m.leftIndex];const auto& b=windows[m.rightIndex];
        return params_.individualPairs && a.staticFrameSet && !m.headOnlyComparison
            && a.sourceId==b.sourceId
            && returningHeadView(originalShotOf[m.leftIndex],originalShotOf[m.rightIndex]);
    });
    if(cancelled())return {};
    const auto strongestFirst = [](const auto& a, const auto& b) {
        if (std::abs(a.similarity - b.similarity) > 1e-12) return a.similarity > b.similarity;
        if (a.leftIndex != b.leftIndex) return a.leftIndex < b.leftIndex;
        return a.rightIndex < b.rightIndex;
    };
    const auto selectionFirst = [&](const auto& a, const auto& b) {
        if (params_.individualPairs && a.headOnlyComparison != b.headOnlyComparison)
            return !a.headOnlyComparison;
        if (params_.individualPairs && a.headOnlyComparison && b.headOnlyComparison) {
            const bool directA = a.unmirroredSimilarity >= params_.similarityThreshold;
            const bool directB = b.unmirroredSimilarity >= params_.similarityThreshold;
            if (directA != directB) return directA;
            if (directA && std::abs(a.unmirroredSimilarity - b.unmirroredSimilarity) > 1e-12)
                return a.unmirroredSimilarity > b.unmirroredSimilarity;
        }
        return strongestFirst(a, b);
    };
    std::sort(matches.begin(), matches.end(), selectionFirst);
    // Keep the strongest result for overlapping windows.  A window may still
    // participate in multiple independent pairs; only near-identical pairs
    // are removed, which is the semantics of duplicateWindowSec.
    std::vector<MotionMatch> unique;
    unique.reserve(matches.size());
    // Recurring reverse cuts can have different shot IDs but the same camera
    // layout. Complete-link groups avoid chaining gradually different views
    // together through a permissive intermediate. Missing evidence stays unique.
    std::vector<std::size_t> representative(shotIds.size(), windows.size());
    for (std::size_t i = 0; i < windows.size(); ++i)
        if (windows[i].hasSceneIndex && windows[i].staticFrameSet
            && representative[shotOf[i]] == windows.size()) representative[shotOf[i]] = i;
    // Camera equivalence is checked on demand for selected portraits. Do not
    // inspect all shot pairs before knowing whether they can produce a result.
    const auto sharedView = [&](const MotionMatch& a, const MotionMatch& b) {
        for (const auto left : {a.leftIndex, a.rightIndex}) for (const auto right : {b.leftIndex, b.rightIndex})
            if (windows[left].hasSceneIndex && windows[right].hasSceneIndex
                && (shotOf[left] == shotOf[right]
                    || returningHeadView(originalShotOf[left],originalShotOf[right])
                    || sameCameraView(windows[left], windows[right])
                    || (representative[shotOf[left]]!=windows.size() && representative[shotOf[right]]!=windows.size()
                        && sameCameraView(windows[representative[shotOf[left]]],windows[representative[shotOf[right]]])))) return true;
        return false;
    };
    const auto portrait = [&](const MotionMatch& match) {
        return windows[match.leftIndex].staticFrameSet && match.headOnlyComparison;
    };
    const auto viewConflict = [&](const MotionMatch& candidate, std::size_t excluded = std::numeric_limits<std::size_t>::max()) {
        if (!params_.individualPairs || !portrait(candidate)) return false;
        for (std::size_t i = 0; i < unique.size(); ++i)
            if (i != excluded && sharedView(candidate, unique[i])) return true;
        return false;
    };
    std::size_t motionResults = 0;
    std::size_t staticResults = 0;
    // Head geometry and body articulation have different score distributions.
    // Preserve both observable regions within the SAME static result budget:
    // balance accepted (not merely visited) results, lending unused slots to
    // the other region. Thresholds, identity gates and motion budget stay intact.
    std::vector<const MotionMatch*> heads, bodies;
    for (const auto& match : matches) {
        if (windows[match.leftIndex].staticFrameSet)
            (match.headOnlyComparison ? heads : bodies).push_back(&match);
    }
    std::size_t nextHead = 0, nextBody = 0, keptHeads = 0, keptBodies = 0;
    // A mirror hypothesis must not crowd out direct head-angle evidence.
    // Keep mirror-only discoveries eligible after the direct candidates;
    // body poses retain the normal strongest-score ordering.
    std::stable_sort(heads.begin(), heads.end(), [&](const auto* a, const auto* b) {
        const bool directA = a->unmirroredSimilarity >= params_.similarityThreshold;
        const bool directB = b->unmirroredSimilarity >= params_.similarityThreshold;
        if (directA != directB) return directA;
        if (directA && std::abs(a->unmirroredSimilarity - b->unmirroredSimilarity) > 1e-12)
            return a->unmirroredSimilarity > b->unmirroredSimilarity;
        return strongestFirst(*a, *b);
    });
    // First cover unused shots, then lend the remaining budget to repeats.
    // Head/body and motion retain their independent result budgets, but a
    // shot has ONE shared reuse quota across all result types.
    std::vector<std::size_t> shotUseCounts(shotIds.size());
    const auto seedBudget=params_.coverageSeedLimit==0 ? params_.maxUniqueResults
        : std::min(params_.maxUniqueResults,params_.coverageSeedLimit);
    const auto shotLimit = params_.individualPairs ? 1U : params_.maxResultsPerShot;
    const auto rounds = shotLimit == 0 ? 1U : shotLimit;
    std::size_t selectedCandidates=0;
    for (std::size_t round = 0; round < rounds; ++round) {
    nextHead = nextBody = 0;
    for (const MotionMatch& ranked : matches) {
        if(cancelled())return {};
        if(selectedCandidates%64==0)report(MotionSearchStage::Select,selectedCandidates,matches.size()*rounds);
        ++selectedCandidates;
        const MotionMatch* next = &ranked;
        if (!params_.individualPairs && windows[ranked.leftIndex].staticFrameSet) {
            const bool takeHead = nextHead < heads.size()
                && (nextBody == bodies.size() || keptHeads < keptBodies
                    || (keptHeads == keptBodies
                        && strongestFirst(*heads[nextHead], *bodies[nextBody])));
            next = takeHead ? heads[nextHead++] : bodies[nextBody++];
        }
        const MotionMatch& candidate = *next;
        const bool staticCandidate = windows[candidate.leftIndex].staticFrameSet;
        if (params_.individualPairs && !staticCandidate) {
            const auto classification = MovementClassifier().classify(
                windows[candidate.leftIndex], windows[candidate.rightIndex]);
            // MotionRanker removes these results anyway. Apply the identical
            // semantic guard before reserving shots, so rejected jitter cannot
            // hide an independently supported pose or another real gesture.
            if (classification.direction == MovementDirection::Static
                && classification.gesture == GestureClass::Static) continue;
        }
        auto& typeCount = staticCandidate ? staticResults : motionResults;
        if (std::getenv("PF_DEBUG_SELECTION") != nullptr) {
            std::fprintf(stderr, "PF_SELECTION t=%.3f/%.3f score=%.6f static=%d head=%d used=%zu limit=%zu\n",
                candidate.leftStartSeconds, candidate.rightStartSeconds, candidate.similarity,
                staticCandidate ? 1 : 0, candidate.headOnlyComparison ? 1 : 0,
                typeCount, params_.maxUniqueResults);
        }
        if (typeCount >= seedBudget) continue;
        const auto shotUses = [&](std::size_t index) { return windows[index].hasSceneIndex ? shotUseCounts[shotOf[index]] : 0U; };
        if (shotLimit > 0
            && (shotUses(candidate.leftIndex) > round || shotUses(candidate.rightIndex) > round)) continue;
        const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const MotionMatch& kept) {
            // A pose result is not a duplicate of a repeated movement. Their
            // scores measure different things and cannot compete in NMS.
            if (staticCandidate != windows[kept.leftIndex].staticFrameSet) return false;
            const bool sameOrientation = windows[candidate.leftIndex].sourceId == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.rightIndex].sourceId;
            const bool swappedOrientation = windows[candidate.leftIndex].sourceId == windows[kept.rightIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.leftIndex].sourceId;
            if (!sameOrientation && !swappedOrientation) return false;
            const auto identitiesCompatible = [&](std::size_t candidateIndex, std::size_t keptIndex) {
                const auto identity = evidence(candidateIndex, keptIndex);
                // A track change alone is inconclusive. Independent, confident
                // appearance disagreement means these are different people,
                // even when a scene index and timestamps happen to coincide.
                return !identity.available || identity.verified;
            };
            const bool compatible = sameOrientation
                && identitiesCompatible(candidate.leftIndex, kept.leftIndex)
                && identitiesCompatible(candidate.rightIndex, kept.rightIndex);
            const bool reverseCompatible = swappedOrientation
                && identitiesCompatible(candidate.leftIndex, kept.rightIndex)
                && identitiesCompatible(candidate.rightIndex, kept.leftIndex);
            if (!compatible && !reverseCompatible) return false;
            const bool knownScenes = windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex;
            if (knownScenes) {
                const bool directScenes = sameOrientation
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.leftIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.rightIndex].sceneIndex;
                const bool reverseScenes = swappedOrientation
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.rightIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.leftIndex].sceneIndex;
                // Distinct edited shots can be only a second apart. A/B and
                // A/C are different pairs; timestamp proximity must not erase
                // C merely because the montage has rapid cuts.
                if (!directScenes && !reverseScenes) return false;
            }
            const double leftDelta = std::abs(candidate.leftStartSeconds - kept.leftStartSeconds);
            const double rightDelta = std::abs(candidate.rightStartSeconds - kept.rightStartSeconds);
            const auto overlapRatio = [](double firstStart, double firstEnd,
                                         double secondStart, double secondEnd) {
                const double overlap = std::max(0.0, std::min(firstEnd, secondEnd)
                    - std::max(firstStart, secondStart));
                const double shorter = std::min(std::max(0.0, firstEnd - firstStart),
                                                std::max(0.0, secondEnd - secondStart));
                return shorter <= 1e-9 ? 0.0 : overlap / shorter;
            };
            const bool overlapping = overlapRatio(candidate.leftStartSeconds,
                                                  candidate.leftEndSeconds,
                                                  kept.leftStartSeconds,
                                                  kept.leftEndSeconds)
                >= params_.nmsOverlapThreshold
                && overlapRatio(candidate.rightStartSeconds, candidate.rightEndSeconds,
                                kept.rightStartSeconds, kept.rightEndSeconds)
                    >= params_.nmsOverlapThreshold;
            if (swappedOrientation) {
                const bool reverseOverlap = overlapRatio(candidate.leftStartSeconds, candidate.leftEndSeconds,
                    kept.rightStartSeconds, kept.rightEndSeconds) >= params_.nmsOverlapThreshold
                    && overlapRatio(candidate.rightStartSeconds, candidate.rightEndSeconds,
                        kept.leftStartSeconds, kept.leftEndSeconds) >= params_.nmsOverlapThreshold;
                const bool reverseNear = std::abs(candidate.leftStartSeconds - kept.rightStartSeconds) <= params_.duplicateWindowSec
                    && std::abs(candidate.rightStartSeconds - kept.leftStartSeconds) <= params_.duplicateWindowSec;
                const bool reverseScene = windows[candidate.leftIndex].hasSceneIndex
                    && windows[candidate.rightIndex].hasSceneIndex
                    && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.rightIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.leftIndex].sceneIndex;
                if (reverseOverlap || reverseNear || reverseScene) return true;
            }
            // Without aligned source IDs, direct timestamps are not comparable.
            if (!sameOrientation) return false;
            const bool sameTrackAndScene = windows[candidate.leftIndex].trackId != 0
                && windows[candidate.leftIndex].trackId == windows[kept.leftIndex].trackId
                && windows[candidate.rightIndex].trackId != 0
                && windows[candidate.rightIndex].trackId == windows[kept.rightIndex].trackId
                && windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                && windows[candidate.leftIndex].sceneIndex == windows[kept.leftIndex].sceneIndex
                && windows[candidate.rightIndex].sceneIndex == windows[kept.rightIndex].sceneIndex;
            // Track IDs can restart after an occlusion. Scene provenance is
            // the stable unit represented by a result card, so collapse
            // windows from the same source/scene pair even when IDs differ.
            const bool sameSourceAndScene = windows[candidate.leftIndex].sourceId
                    == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId
                    == windows[kept.rightIndex].sourceId
                && windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                && windows[candidate.leftIndex].sceneIndex
                    == windows[kept.leftIndex].sceneIndex
                && windows[candidate.rightIndex].sceneIndex
                    == windows[kept.rightIndex].sceneIndex;
            // A scene is the unit shown in the results rail. Sliding motion
            // windows inside the same tracked shot must not produce a second
            // card for that shot pair (the old 1.25 s bound let exactly this
            // duplicate through when the windows were five seconds apart).
            // Keep the strongest window selected by the sort above.
            const bool closeInTime = leftDelta <= std::max(2.0, params_.duplicateWindowSec * 2.0)
                && rightDelta <= std::max(2.0, params_.duplicateWindowSec * 2.0);
            const bool sameSceneNeighborhood = windows[candidate.leftIndex].sourceId
                    == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId
                    == windows[kept.rightIndex].sourceId
                && std::abs(candidate.leftSceneStartSeconds - kept.leftSceneStartSeconds) <= 2.0
                && std::abs(candidate.rightSceneStartSeconds - kept.rightSceneStartSeconds) <= 2.0
                && sceneContextSimilarity(windows[candidate.leftIndex], windows[kept.leftIndex]) >= 0.84
                && sceneContextSimilarity(windows[candidate.rightIndex], windows[kept.rightIndex]) >= 0.84;
            return overlapping || (leftDelta <= params_.duplicateWindowSec
                                   && rightDelta <= params_.duplicateWindowSec)
                || sameTrackAndScene || sameSourceAndScene
                || (closeInTime && sameSceneNeighborhood);
        });
        if (!duplicate) {
            unique.push_back(candidate);
            ++shotUseCounts[shotOf[candidate.leftIndex]];
            ++shotUseCounts[shotOf[candidate.rightIndex]];
            ++typeCount;
            if (staticCandidate) ++(candidate.headOnlyComparison ? keptHeads : keptBodies);
        }
    }
    }
    report(MotionSearchStage::Select,selectedCandidates,matches.size()*rounds);
    if (params_.individualPairs) {
        // A greedy strongest-first choice A/B can leave C and D unused even
        // when verified A/C and B/D exist. Augment that path into two disjoint
        // parallels, without weakening comparison or spending another shot.
        // Collapse sliding hypotheses into their strongest shot-pair edge.
        std::vector<std::vector<const MotionMatch*>> adjacency(shotIds.size());
        std::unordered_set<std::uint64_t> edges;
        for (const auto& match : matches) {
            if (!windows[match.leftIndex].hasSceneIndex || !windows[match.rightIndex].hasSceneIndex) continue;
            if (!windows[match.leftIndex].staticFrameSet) {
                const auto classification = MovementClassifier().classify(
                    windows[match.leftIndex], windows[match.rightIndex]);
                if (classification.direction == MovementDirection::Static
                    && classification.gesture == GestureClass::Static) continue;
            }
            const auto a = shotOf[match.leftIndex], b = shotOf[match.rightIndex];
            const auto key = (static_cast<std::uint64_t>(std::min(a,b)) << 32U) | std::max(a,b);
            if (a == b || !edges.insert(key).second) continue;
            adjacency[a].push_back(&match); adjacency[b].push_back(&match);
        }
        const auto otherShot = [&](const MotionMatch& match, std::size_t shot) {
            const auto a = shotOf[match.leftIndex], b = shotOf[match.rightIndex];
            return a == shot ? b : a;
        };
        const auto informationPreserved = [&](const MotionMatch& replacement, const MotionMatch& original) {
            if (!original.headOnlyComparison && replacement.headOnlyComparison) return false;
            if (original.headOnlyComparison && replacement.headOnlyComparison
                && original.unmirroredSimilarity >= params_.similarityThreshold
                && replacement.unmirroredSimilarity < params_.similarityThreshold) return false;
            return true;
        };
        bool augmented = true;
        while (augmented) {
            if(cancelled())return {};
            augmented = false;
            for (std::size_t keptIndex = 0; keptIndex < unique.size() && !augmented; ++keptIndex) {
                const auto original = unique[keptIndex];
                if (!windows[original.leftIndex].hasSceneIndex || !windows[original.rightIndex].hasSceneIndex) continue;
                const auto a = shotOf[original.leftIndex], b = shotOf[original.rightIndex];
                for (const auto* ac : adjacency[a]) {
                    const auto c = otherShot(*ac,a);
                    if (shotUseCounts[c] || !informationPreserved(*ac,original)) continue;
                    for (const auto* bd : adjacency[b]) {
                        const auto d = otherShot(*bd,b);
                        if (c == d || shotUseCounts[d] || !informationPreserved(*bd,original)) continue;
                        const auto staticEdge = [&](const MotionMatch& edge) {
                            return windows[edge.leftIndex].staticFrameSet ? 1U : 0U;
                        };
                        const auto newStatics = staticResults - staticEdge(original) + staticEdge(*ac) + staticEdge(*bd);
                        const auto newMotions = motionResults - (1U-staticEdge(original))
                            + (1U-staticEdge(*ac)) + (1U-staticEdge(*bd));
                        if (newStatics > seedBudget || newMotions > seedBudget) continue;
                        unique[keptIndex] = *ac;
                        unique.push_back(*bd);
                        ++shotUseCounts[c]; ++shotUseCounts[d];
                        staticResults = newStatics; motionResults = newMotions;
                        augmented = true;
                        break;
                    }
                    if (augmented) break;
                }
            }
        }
    }
    if (params_.individualPairs) {
        // Freeze the established body/motion coverage before reducing camera
        // repeats. Filtering portrait edges earlier can change augmenting
        // paths and accidentally lose an unrelated, useful body parallel.
        auto covered = std::move(unique);
        unique.clear();
        std::fill(shotUseCounts.begin(), shotUseCounts.end(), 0);
        staticResults = motionResults = 0;
        const auto append = [&](const MotionMatch& match) {
            const bool head=portrait(match);
            if (!measuredIdentityCompatible(windows[match.leftIndex],match.leftStartSeconds,match.leftEndSeconds,head)
                || !measuredIdentityCompatible(windows[match.rightIndex],match.rightStartSeconds,match.rightEndSeconds,head)) return;
            const auto a = shotOf[match.leftIndex], b = shotOf[match.rightIndex];
            auto& count = windows[match.leftIndex].staticFrameSet ? staticResults : motionResults;
            if (a == b || shotUseCounts[a] || shotUseCounts[b] || count >= params_.maxUniqueResults) return;
            if (!windows[match.leftIndex].staticFrameSet) {
                const auto movement=MovementClassifier().classify(windows[match.leftIndex],windows[match.rightIndex]);
                if (movement.direction==MovementDirection::Static && movement.gesture==GestureClass::Static) return;
            }
            if (portrait(match) && (sameCameraView(windows[match.leftIndex], windows[match.rightIndex])
                || returningHeadView(originalShotOf[match.leftIndex],originalShotOf[match.rightIndex])
                || viewConflict(match))) return;
            unique.push_back(match);
            ++shotUseCounts[a]; ++shotUseCounts[b]; ++count;
        };
        for (const auto& match : covered) if (!portrait(match)) append(match);
        // Camera filtering can free shots after initial NMS/augmentation.
        // Refill them with already accepted body/gesture edges before generic
        // portraits. Established body/motion pairs and all gates stay intact.
        for (const auto& match : matches) { if(cancelled())return {}; if (!match.headOnlyComparison) append(match); }
        // Preserve direct head orientation preference when choosing a single
        // useful portrait from a returning camera.
        const auto headPriority = [&](const MotionMatch& a, const MotionMatch& b) {
            const bool directA = a.unmirroredSimilarity >= params_.similarityThreshold;
            const bool directB = b.unmirroredSimilarity >= params_.similarityThreshold;
            if (directA != directB) return directA;
            return strongestFirst(a,b);
        };
        std::sort(covered.begin(), covered.end(), headPriority);
        const auto cameraTotal=std::count_if(covered.begin(),covered.end(),portrait)
            +std::count_if(matches.begin(),matches.end(),portrait);
        std::size_t cameraDone=0;report(MotionSearchStage::Camera,0,cameraTotal);
        for (const auto& match : covered) if (portrait(match)) {
            if(cancelled())return {};
            append(match);report(MotionSearchStage::Camera,++cameraDone,cameraTotal);
        }
        // Freed portrait shots can support other, genuinely different views.
        std::vector<MotionMatch> alternatives;
        for (const auto& match : matches) if (portrait(match)) alternatives.push_back(match);
        std::sort(alternatives.begin(), alternatives.end(), headPriority);
        for (const auto& match : alternatives) {
            if(cancelled())return {};append(match);report(MotionSearchStage::Camera,++cameraDone,cameraTotal);
        }
    }
    if(cancelled())return {};
    if (params_.individualPairs && params_.recoverUnusedShots
        && (staticResults < params_.maxUniqueResults || motionResults < params_.maxUniqueResults)) {
        std::vector<MotionWindow> unused;
        std::vector<std::size_t> originalIndices;
        std::unordered_set<std::string> ranges;
        std::unordered_map<std::string,std::size_t> footage;
        std::unordered_set<std::string> suppliedFootage;
        for (std::size_t i=0;i<windows.size();++i) if (!windows[i].sceneSequence.empty())
            footage.try_emplace(windows[i].sourceId+"|"+std::to_string(windows[i].sceneIndex),i);
        const auto appendWindow=[&](MotionWindow w,std::size_t original) {
            const auto key=w.sourceId+"|"+std::to_string(w.trackId)+"|"+std::to_string(w.sceneIndex)+"|"
                +std::to_string(w.staticFrameSet)+"|"+std::to_string(std::llround(w.frames.front().timestampSeconds*1e6))
                +"|"+std::to_string(std::llround(w.frames.back().timestampSeconds*1e6));
            if (!ranges.insert(key).second) return;
            const auto shot=w.sourceId+"|"+std::to_string(w.sceneIndex);
            if (suppliedFootage.insert(shot).second) {
                if (const auto found=footage.find(shot);found!=footage.end()) {
                    w.sceneSequence=windows[found->second].sceneSequence;
                    w.sceneSequenceTimes=windows[found->second].sceneSequenceTimes;
                }
            } else {w.sceneSequence.clear();w.sceneSequenceTimes.clear();}
            unused.push_back(std::move(w));originalIndices.push_back(original);
        };
        for (std::size_t i=0;i<windows.size();++i) {
            if (!windows[i].hasSceneIndex || shotUseCounts[shotOf[i]] || windows[i].frames.empty()) continue;
            if (windows[i].staticFrameSet ? !params_.allowStaticFrames : prepared[i].descriptors.empty()) continue;
            if ((windows[i].staticFrameSet ? staticResults : motionResults) >= params_.maxUniqueResults) continue;
            appendWindow(windows[i],i);
            if(cancelled())return {};
            for (auto& shorter:shortPoseWindows(windows[i])) {
                appendWindow(std::move(shorter),i);
            }
        }
        if (unused.size()>=2) {
            if(cancelled())return {};
            report(MotionSearchStage::Recovery,0,unused.size());
            auto recovery=params_;recovery.recoverUnusedShots=false;recovery.expandedSearch=true;
            recovery.coverageSeedLimit=0;recovery.bodyMotionOnly=true;
            auto recoveryControl=control;
            recoveryControl.reuse=nullptr;
            if(reuse) {
                if(!reuse->recovery)reuse->recovery=std::make_unique<MotionSearchReuse>(reuse->budget/2);
                recoveryControl.reuse=reuse->recovery.get();
            }
            recoveryControl.progress=[&](MotionSearchStage,std::size_t done,std::size_t total) {
                report(MotionSearchStage::Recovery,done,total);
            };
            const auto additions=MotionMatcher(recovery).findAllPairs(unused,recoveryControl);
            if(cancelled())return {};
            std::size_t recovered=0;
            // The secondary pass cannot replace an existing edge or borrow
            // its shot. Each result still passes the same temporal/identity
            // gates, visual-family conflict check and shared diversity quota.
            for (auto match:additions) {
                match.leftIndex=originalIndices[match.leftIndex];match.rightIndex=originalIndices[match.rightIndex];
                const auto a=shotOf[match.leftIndex],b=shotOf[match.rightIndex];
                auto& count=windows[match.leftIndex].staticFrameSet ? staticResults : motionResults;
                if (a==b || shotUseCounts[a] || shotUseCounts[b] || count>=params_.maxUniqueResults) continue;
                if (portrait(match) && (sameCameraView(windows[match.leftIndex],windows[match.rightIndex])
                    || returningHeadView(originalShotOf[match.leftIndex],originalShotOf[match.rightIndex])
                    || viewConflict(match))) continue;
                unique.push_back(std::move(match));++shotUseCounts[a];++shotUseCounts[b];++count;++recovered;
            }
            if (profile) std::fprintf(stderr,"PF_DEBUG_RECOVERY unused_windows=%zu recovered_pairs=%zu\n",unused.size(),recovered);
        }
    }
    std::erase_if(unique,[&](const auto& match) {
        const bool head=portrait(match);
        return !measuredIdentityCompatible(windows[match.leftIndex],match.leftStartSeconds,match.leftEndSeconds,head)
            || !measuredIdentityCompatible(windows[match.rightIndex],match.rightStartSeconds,match.rightEndSeconds,head);
    });
    if (params_.individualPairs && params_.recoverUnusedShots) {
        // Recovery runs after portrait selection. A newly supported body pose
        // can share a camera with an earlier portrait; apply body priority
        // once more across the final set, without replacing any strong edge.
        auto selected=std::move(unique);unique.clear();
        for (const auto& match:selected) if (!portrait(match)) unique.push_back(match);
        std::sort(selected.begin(),selected.end(),[&](const auto& a,const auto& b) {
            const bool directA=a.unmirroredSimilarity>=params_.similarityThreshold;
            const bool directB=b.unmirroredSimilarity>=params_.similarityThreshold;
            if (directA!=directB) return directA;
            return strongestFirst(a,b);
        });
        for (const auto& match:selected) if (portrait(match) && !viewConflict(match)) unique.push_back(match);
    }
    // Retrieval policy must not change the public descending-score ordering.
    std::sort(unique.begin(), unique.end(), strongestFirst);
    matches = std::move(unique);
    if (profile)
        std::fprintf(stderr, "PF_DEBUG_TIMING matcher_prepare_ms=%.1f pose_index_ms=%.1f pose_query_ms=%.1f identity_index_ms=%.1f identity_query_ms=%.1f exact_compare_ms=%.1f selection_ms=%.1f windows=%zu prepared=%zu candidates=%zu compared=%zu raw_accepted=%zu identity_memo=%zu\n",
            prepareMs, indexBuildMs, poseQueryMs, appearanceBuildMs, appearanceQueryMs,
            comparedMs, elapsed() - selectionStart, windows.size(), preparedCount,
            candidateSummary, compared, rawAccepted, identityMemo.size());
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr,
                     "PF_DEBUG_MATCHER windows=%zu prepared=%zu motion=%zu static=%zu candidates=%zu appearanceCandidates=%zu trackRejected=%zu sceneRejected=%zu staticRejected=%zu gapPassed=%zu coarsePassed=%zu compared=%zu accepted=%zu\n",
                     windows.size(), preparedCount, preparedMotion, preparedStatic, candidateSummary, appearanceCandidatePairs, trackRejected,
                     sceneRejected, staticRejected, gapPassed, coarsePassed, compared,
                     matches.size());
    }
    if(cancelled())return {};
    return matches;
}

bool observedIdentityAllows(const MotionWindow& window,double start,double end,bool portrait)
{
    return measuredIdentityCompatible(window,start,end,portrait);
}

} // namespace pfcore
