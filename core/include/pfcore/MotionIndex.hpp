#pragma once

#include <cstddef>
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

    explicit MotionIndex(std::size_t maximumConnections = 8,
                         std::size_t constructionSearch = 32);

    void clear();
    void add(std::size_t id, std::vector<double> embedding);
    void build();

    [[nodiscard]] std::vector<Neighbor> query(const std::vector<double>& embedding,
                                               std::size_t count,
                                               std::size_t searchWidth = 32) const;
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
};

} // namespace pfcore
