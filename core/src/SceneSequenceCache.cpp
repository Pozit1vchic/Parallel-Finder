#include <pfcore/SceneSequenceCache.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <limits>

namespace pfcore {
namespace {
constexpr std::uint64_t magic=0x3151455343465050ULL;
template<class T> void append(std::vector<std::uint8_t>& bytes,T value) {
    auto data=std::bit_cast<std::array<std::uint8_t,sizeof(T)>>(value);
    if constexpr(std::endian::native==std::endian::big)std::reverse(data.begin(),data.end());
    bytes.insert(bytes.end(),data.begin(),data.end());
}
template<class T> bool read(std::span<const std::uint8_t> bytes,std::size_t& offset,T& value) {
    if(offset>bytes.size() || bytes.size()-offset<sizeof(T))return false;
    std::array<std::uint8_t,sizeof(T)> data{};
    std::copy_n(bytes.begin()+offset,sizeof(T),data.begin());offset+=sizeof(T);
    if constexpr(std::endian::native==std::endian::big)std::reverse(data.begin(),data.end());
    value=std::bit_cast<T>(data);return true;
}
bool valid(const CachedSceneSequence& s) {
    return std::isfinite(s.start) && std::isfinite(s.end) && s.start>=0 && s.end>=s.start
        && (s.pixels.empty() || (s.times.size()>=3 && s.pixels.size()%432==0))
        && s.times.size()==s.pixels.size()/432
        && std::all_of(s.pixels.begin(),s.pixels.end(),[](float p){return std::isfinite(p) && p>=0 && p<=1;})
        && std::all_of(s.times.begin(),s.times.end(),[&](double t){return std::isfinite(t) && t>=s.start && t<s.end;})
        && std::is_sorted(s.times.begin(),s.times.end());
}
}
std::vector<std::uint8_t> encodeSceneSequences(std::span<const CachedSceneSequence> scenes) {
    std::vector<std::uint8_t> bytes;std::set<std::uint64_t> ids;
    std::size_t size=16;
    for(const auto& s:scenes) {
        if(!valid(s) || !ids.insert(s.scene).second)throw std::invalid_argument("Invalid scene sequence");
        const auto added=32+s.pixels.size()*sizeof(float)+s.times.size()*sizeof(double);
        if(added>std::numeric_limits<std::size_t>::max()-size)throw std::length_error("Scene sequence overflow");
        size+=added;
    }
    bytes.reserve(size);append(bytes,magic);append(bytes,static_cast<std::uint64_t>(scenes.size()));
    for(const auto& s:scenes) {
        append(bytes,s.scene);append(bytes,s.start);append(bytes,s.end);append(bytes,static_cast<std::uint64_t>(s.times.size()));
        for(const auto p:s.pixels)append(bytes,p);
        for(const auto t:s.times)append(bytes,t);
    }
    return bytes;
}
std::optional<std::vector<CachedSceneSequence>> decodeSceneSequences(std::span<const std::uint8_t> bytes) {
    std::size_t offset=0;std::uint64_t header=0,count=0;
    if(!read(bytes,offset,header) || header!=magic || !read(bytes,offset,count) || count>(bytes.size()-offset)/32)return {};
    std::vector<CachedSceneSequence> scenes;scenes.reserve(count);std::set<std::uint64_t> ids;
    for(std::uint64_t i=0;i<count;++i) {
        CachedSceneSequence s;std::uint64_t frames=0;
        if(!read(bytes,offset,s.scene) || !read(bytes,offset,s.start) || !read(bytes,offset,s.end)
            || !read(bytes,offset,frames) || frames>(bytes.size()-offset)/(432*sizeof(float)+sizeof(double)))return {};
        s.pixels.resize(frames*432);s.times.resize(frames);
        for(auto& p:s.pixels)if(!read(bytes,offset,p))return {};
        for(auto& t:s.times)if(!read(bytes,offset,t))return {};
        if(!valid(s) || !ids.insert(s.scene).second)return {};
        scenes.push_back(std::move(s));
    }
    if(offset!=bytes.size())return {};
    return scenes;
}
}
