#include <pfcore/FaceAssociation.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace pfcore {
void enforceUniqueFaceOwnership(std::vector<PersonDetection>& people,std::span<const ObservedFaceLandmarks> faces) {
    if(faces.size()!=people.size()) {for(auto& person:people)person.faceEmbedding.clear();return;}
    const auto valid=[&](std::size_t i) {
        return faces[i].observed&&!people[i].faceEmbedding.empty()
            &&std::all_of(faces[i].points.begin(),faces[i].points.end(),[](double v){return std::isfinite(v);});
    };
    const auto span=[&](std::size_t i) {const auto& p=faces[i].points;return std::hypot(p[0]-p[2],p[1]-p[3]);};
    const auto same=[&](std::size_t a,std::size_t b) {
        if(!valid(a)||!valid(b))return false;
        const double scale=std::min(span(a),span(b));if(scale<1)return false;
        for(int j=0;j<5;++j)if(std::hypot(faces[a].points[j*2]-faces[b].points[j*2],
            faces[a].points[j*2+1]-faces[b].points[j*2+1])>scale*.18)return false;
        return true;
    };
    const auto score=[&](std::size_t i) {
        const auto& person=people[i];const auto& face=faces[i].points;const double scale=span(i);
        if(person.keypoints.size()<3||scale<1)return std::numeric_limits<double>::infinity();
        const auto& nose=person.keypoints[0];
        if(nose.confidence<.5||!std::isfinite(nose.x)||!std::isfinite(nose.y))return std::numeric_limits<double>::infinity();
        double result=std::hypot(nose.x-face[4],nose.y-face[5])/scale;
        const auto& a=person.keypoints[1];const auto& b=person.keypoints[2];
        if(a.confidence>=.5&&b.confidence>=.5) {
            const auto error=[&](int x,int y) {return std::hypot(a.x-face[x],a.y-face[x+1])+std::hypot(b.x-face[y],b.y-face[y+1]);};
            result+=std::min(error(0,2),error(2,0))/(2*scale);
        }
        return result;
    };
    // A single detected face can still belong to a neighbour. Check geometry
    // before resolving duplicates, rather than trusting uncontested ownership.
    for(std::size_t i=0;i<people.size();++i) {
        if(!valid(i))continue;
        const double residual=score(i);
        if(!std::isfinite(residual)||residual>.8)people[i].faceEmbedding.clear();
    }
    std::vector<bool> visited(people.size());
    for(std::size_t i=0;i<people.size();++i) {
        if(visited[i]||!valid(i))continue;
        std::vector<std::size_t> group{i};visited[i]=true;
        // Duplicate landmarks can form a chain: A matches B and B matches C,
        // even if A and C differ slightly too much to match directly. Resolve
        // the whole connected group before clearing any embeddings; otherwise
        // the same face may be assigned to more than one person depending on
        // the detection order.
        for(std::size_t member=0;member<group.size();++member)
            for(std::size_t j=0;j<people.size();++j)
                if(!visited[j]&&same(group[member],j)) {group.push_back(j);visited[j]=true;}
        if(group.size()<2)continue;
        std::stable_sort(group.begin(),group.end(),[&](auto a,auto b){return score(a)<score(b);});
        const double best=score(group[0]),runner=score(group[1]);
        const bool clearAll=!std::isfinite(best)||best>.8||runner-best<.08;
        for(std::size_t j=clearAll?0:1;j<group.size();++j)people[group[j]].faceEmbedding.clear();
    }
}
}
