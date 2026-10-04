#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pfcore {

// A small, deterministic HNSW-style approximate nearest-neighbour index.
// It is intentionally dependency-free so pfcore stays portable and Qt-free.
class MotionIndex {
public:
    struct Neighbor {
        std::size_t id = 0;
        double similarity = 0.0;
    };

    // One caller-owned workspace per query embedding. Repeated wider searches
    // still traverse the graph independently, but reuse exact distances.
    // No mutable index state is shared between concurrent readers. Reset or
    // destroy the workspace before its index is destroyed.
    class QueryWorkspace {
        friend class MotionIndex;
        const MotionIndex* owner_ = nullptr;
        std::uint64_t revision_ = 0;
        std::vector<double> embedding_;
        std::vector<double> distances_;
    };

    explicit MotionIndex(std::size_t maximumConnections = 8,
                         std::size_t constructionSearch = 32);

    void clear();
    // Empty/non-finite descriptors are ignored without invalidating the graph;
    // queries with empty/non-finite descriptors return no neighbors.
    void add(std::size_t id, std::vector<double> embedding);
    void build();

    [[nodiscard]] std::vector<Neighbor> query(const std::vector<double>& embedding,
                                               std::size_t count,
                                               std::size_t searchWidth = 32) const;
    [[nodiscard]] std::vector<Neighbor> query(const std::vector<double>& embedding,
                                               std::size_t count,
                                               std::size_t searchWidth,
                                               QueryWorkspace& workspace) const;
    [[nodiscard]] std::size_t size() const noexcept;

    // Exposed only as an implementation carrier so the translation unit can
    // keep graph operations free of Qt or third-party types.
    struct Node {
        std::size_t id = 0;
        std::vector<double> embedding;
        std::vector<std::vector<std::size_t>> neighbors;
    };

private:
    std::size_t maximumConnections_;
    std::size_t constructionSearch_;
    std::vector<Node> nodes_;
    std::size_t entryPoint_ = 0;
    std::size_t maxLevel_ = 0;
    bool built_ = false;
    std::uint64_t revision_ = 0;
};

} // namespace pfcore
