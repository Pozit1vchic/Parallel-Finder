#include "pfcore/MotionIndex.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace pfcore {
namespace {

// Small scene packs are cheap to search exactly. A pruned ANN graph can lose
// entire equal-distance clusters (common with repeated poses), so bounded
// exact retrieval is preferable here to silently sacrificing recall.
constexpr std::size_t kExactSearchLimit = 512;

std::uint64_t nextRevision()
{
    // A revision identifies graph contents, including after default copy/move
    // assignment. Instance-local counters can collide on assignment and let
    // a caller-owned workspace reuse distances from different contents.
    static std::atomic<std::uint64_t> revision{0};
    return revision.fetch_add(1, std::memory_order_relaxed) + 1;
}

double distance(const std::vector<double>& left, const std::vector<double>& right)
{
    if (left.empty() || right.empty()) return std::numeric_limits<double>::infinity();
    const std::size_t dimensions = std::min(left.size(), right.size());
    if (dimensions == 0) return std::numeric_limits<double>::infinity();

    double sum = 0.0;
    for (std::size_t i = 0; i < dimensions; ++i) {
        const double delta = left[i] - right[i];
        sum += delta * delta;
    }
    // Penalise truncated/padded dimensions, without making an empty pose look
    // close to a valid embedding.
    sum += static_cast<double>(std::max(left.size(), right.size()) - dimensions);
    return std::sqrt(sum / static_cast<double>(std::max(left.size(), right.size())));
}

std::uint64_t splitMix64(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::size_t levelFor(std::size_t index)
{
    std::uint64_t state = splitMix64(static_cast<std::uint64_t>(index));
    std::size_t level = 0;
    // P(level >= n) ~= 1/4^n. The cap prevents pathological graphs.
    while (level < 16 && (state & 0xffffU) < 0x4000U) {
        ++level;
        state = splitMix64(state);
    }
    return level;
}

using Candidate = std::pair<double, std::size_t>;

template<class DistanceTo>
std::vector<std::size_t> searchLayer(const std::vector<MotionIndex::Node>& nodes,
                                     const DistanceTo& distanceTo,
                                     std::size_t entry,
                                     std::size_t level,
                                     std::size_t width)
{
    if (nodes.empty() || entry >= nodes.size() || width == 0) return {};

    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> candidates;
    std::priority_queue<Candidate> best;
    std::vector<bool> visited(nodes.size(), false);
    const double entryDistance = distanceTo(entry);
    candidates.emplace(entryDistance, entry);
    best.emplace(entryDistance, entry);
    visited[entry] = true;

    while (!candidates.empty()) {
        const auto [candidateDistance, candidate] = candidates.top();
        candidates.pop();
        if (best.size() >= width && candidateDistance > best.top().first) break;
        if (level >= nodes[candidate].neighbors.size()) continue;
        for (const std::size_t neighbour : nodes[candidate].neighbors[level]) {
            if (neighbour >= nodes.size() || visited[neighbour]) continue;
            visited[neighbour] = true;
            const double neighbourDistance = distanceTo(neighbour);
            if (best.size() < width || neighbourDistance < best.top().first) {
                candidates.emplace(neighbourDistance, neighbour);
                best.emplace(neighbourDistance, neighbour);
                if (best.size() > width) best.pop();
            }
        }
    }

    std::vector<std::size_t> result;
    result.reserve(best.size());
    while (!best.empty()) {
        result.push_back(best.top().second);
        best.pop();
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void connect(MotionIndex::Node& node, std::size_t neighbour, std::size_t level,
             const std::vector<MotionIndex::Node>& nodes, std::size_t maximumConnections)
{
    if (level >= node.neighbors.size()) node.neighbors.resize(level + 1);
    auto& links = node.neighbors[level];
    if (std::find(links.begin(), links.end(), neighbour) == links.end()) links.push_back(neighbour);
    if (links.size() <= maximumConnections) return;
    // Compute each exact distance once, rather than twice per sort comparison.
    // Preserve the existing equal-distance comparator and initial link order.
    std::vector<Candidate> ranked;
    ranked.reserve(links.size());
    for (const auto id : links) ranked.emplace_back(distance(node.embedding, nodes[id].embedding), id);
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    links.resize(maximumConnections);
    for (std::size_t i = 0; i < links.size(); ++i) links[i] = ranked[i].second;
}

} // namespace

MotionIndex::MotionIndex(std::size_t maximumConnections, std::size_t constructionSearch)
    : maximumConnections_(maximumConnections), constructionSearch_(constructionSearch)
{
    if (maximumConnections_ < 2 || constructionSearch_ == 0)
        throw std::invalid_argument("MotionIndex: invalid graph parameters");
}

void MotionIndex::clear()
{
    revision_ = nextRevision();
    nodes_.clear();
    entryPoint_ = 0;
    maxLevel_ = 0;
    built_ = false;
}

void MotionIndex::add(std::size_t id, std::vector<double> embedding)
{
    if (embedding.empty()) return;
    revision_ = nextRevision();
    nodes_.push_back(Node{id, std::move(embedding), {}});
    built_ = false;
}

void MotionIndex::build()
{
    revision_ = nextRevision();
    for (Node& node : nodes_) node.neighbors.clear();
    if (nodes_.size() <= kExactSearchLimit) {
        built_ = true;
        return;
    }

    entryPoint_ = 0;
    maxLevel_ = 0;
    nodes_[0].neighbors.resize(levelFor(0) + 1);
    maxLevel_ = nodes_[0].neighbors.size() - 1;

    for (std::size_t index = 1; index < nodes_.size(); ++index) {
        const std::size_t nodeLevel = levelFor(index);
        nodes_[index].neighbors.resize(nodeLevel + 1);
        std::size_t current = entryPoint_;
        const auto distanceTo = [&](std::size_t candidate) {
            return distance(nodes_[index].embedding, nodes_[candidate].embedding);
        };

        for (std::size_t level = maxLevel_; level > nodeLevel; --level) {
            const auto candidates = searchLayer(nodes_, distanceTo, current, level, 1);
            if (!candidates.empty()) current = candidates.front();
        }

        const std::size_t lowestLevel = std::min(nodeLevel, maxLevel_);
        for (std::size_t level = lowestLevel + 1; level-- > 0;) {
            const auto candidates = searchLayer(nodes_, distanceTo, current, level,
                                                constructionSearch_);
            const std::size_t links = std::min(maximumConnections_, candidates.size());
            for (std::size_t rank = 0; rank < links; ++rank) {
                const std::size_t neighbour = candidates[rank];
                connect(nodes_[index], neighbour, level, nodes_, maximumConnections_);
                connect(nodes_[neighbour], index, level, nodes_, maximumConnections_);
            }
            if (!candidates.empty()) current = candidates.front();
        }

        if (nodeLevel > maxLevel_) {
            maxLevel_ = nodeLevel;
            entryPoint_ = index;
        }
    }
    built_ = true;
}

std::vector<MotionIndex::Neighbor> MotionIndex::query(const std::vector<double>& embedding,
                                                      std::size_t count,
                                                      std::size_t searchWidth) const
{
    QueryWorkspace workspace;
    return query(embedding, count, searchWidth, workspace);
}

std::vector<MotionIndex::Neighbor> MotionIndex::query(const std::vector<double>& embedding,
                                                      std::size_t count,
                                                      std::size_t searchWidth,
                                                      QueryWorkspace& workspace) const
{
    if (!built_ || nodes_.empty() || embedding.empty() || count == 0) return {};
    if (workspace.owner_ != this || workspace.revision_ != revision_
        || workspace.embedding_ != embedding) {
        workspace.owner_ = this;
        workspace.revision_ = revision_;
        workspace.embedding_ = embedding;
        workspace.distances_.assign(nodes_.size(), -1.0);
    }
    const auto distanceTo = [&](std::size_t candidate) {
        auto& value = workspace.distances_[candidate];
        if (value < 0.0) value = distance(embedding, nodes_[candidate].embedding);
        return value;
    };
    const auto exactQuery = [&] {
        std::vector<Candidate> candidates;
        candidates.reserve(nodes_.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            candidates.emplace_back(distanceTo(i), i);
        const auto keep = std::min(count, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end());
        std::vector<Neighbor> result;
        result.reserve(keep);
        for (std::size_t i = 0; i < keep; ++i)
            result.push_back({nodes_[candidates[i].second].id,
                std::clamp(std::exp(-candidates[i].first), 0.0, 1.0)});
        return result;
    };
    if (nodes_.size() <= kExactSearchLimit) return exactQuery();
    searchWidth = std::max(searchWidth, count);
    std::size_t current = entryPoint_;
    for (std::size_t level = maxLevel_; level > 0; --level) {
        const auto candidates = searchLayer(nodes_, distanceTo, current, level, 1);
        if (!candidates.empty()) current = candidates.front();
    }
    const auto candidates = searchLayer(nodes_, distanceTo, current, 0,
                                        std::min(searchWidth, nodes_.size()));
    // A flat, incomplete ANN neighborhood has no useful distance gradient.
    // Equal-distance pruning can isolate the actual nearest cluster even in
    // a large graph; use exact retrieval for this degenerate query only.
    if (candidates.size() < std::min(count, nodes_.size())
        || (!candidates.empty()
            && std::abs(distanceTo(candidates.front())
                - distanceTo(candidates.back())) <= 1e-12))
        return exactQuery();
    std::vector<Neighbor> result;
    result.reserve(std::min(count, candidates.size()));
    for (const std::size_t candidate : candidates) {
        const double candidateDistance = distanceTo(candidate);
        result.push_back({nodes_[candidate].id, std::clamp(std::exp(-candidateDistance), 0.0, 1.0)});
        if (result.size() >= count) break;
    }
    return result;
}

std::size_t MotionIndex::size() const noexcept { return nodes_.size(); }

} // namespace pfcore
