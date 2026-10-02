#include <gtest/gtest.h>

#include <pfcore/MotionIndex.hpp>
#include <cmath>
#include <future>

namespace {
void expectSameNeighbors(const std::vector<pfcore::MotionIndex::Neighbor>& left,
                         const std::vector<pfcore::MotionIndex::Neighbor>& right)
{
    ASSERT_EQ(left.size(), right.size());
    for (std::size_t i = 0; i < left.size(); ++i) {
        EXPECT_EQ(left[i].id, right[i].id);
        EXPECT_DOUBLE_EQ(left[i].similarity, right[i].similarity);
    }
}
}

TEST(MotionIndex, ReusedWorkspaceKeepsIndependentWidthSearchResults)
{
    pfcore::MotionIndex index;
    for (std::size_t i = 0; i < 640; ++i) {
        std::vector<double> embedding(32);
        for (std::size_t d = 0; d < embedding.size(); ++d)
            embedding[d] = std::sin(static_cast<double>(i * 17 + d * 29) * 0.137);
        index.add(i, std::move(embedding));
    }
    index.build();
    pfcore::MotionIndex::QueryWorkspace workspace;
    for (const auto& query : {std::vector<double>(32, 0.2), std::vector<double>(24, -0.4)})
        for (const auto count : {1U, 24U, 96U, 192U, 384U, 640U, 24U})
            expectSameNeighbors(index.query(query, count, count * 2),
                                index.query(query, count, count * 2, workspace));

    // Each reader owns its workspace; the const graph is safe to share.
    auto reader = [&index] {
        pfcore::MotionIndex::QueryWorkspace local;
        return index.query(std::vector<double>(32, 0.2), 96, 192, local);
    };
    auto one = std::async(std::launch::async, reader);
    auto two = std::async(std::launch::async, reader);
    expectSameNeighbors(one.get(), two.get());
}

TEST(MotionIndex, WorkspaceInvalidatesOnChangesAndOtherIndex)
{
    pfcore::MotionIndex index;
    pfcore::MotionIndex::QueryWorkspace workspace;
    index.add(1, {1.0});
    index.build();
    EXPECT_EQ(index.query({1.0}, 1, 2, workspace).front().id, 1U);
    index.add(2, {0.0});
    EXPECT_TRUE(index.query({1.0}, 1, 2, workspace).empty());
    index.build();
    expectSameNeighbors(index.query({0.0}, 2), index.query({0.0}, 2, 4, workspace));
    index.clear();
    index.add(3, {8.0, 9.0});
    index.build();
    expectSameNeighbors(index.query({1.0}, 2), index.query({1.0}, 2, 4, workspace));
    pfcore::MotionIndex other;
    other.add(4, {-8.0});
    other.build();
    expectSameNeighbors(other.query({1.0}, 2), other.query({1.0}, 2, 4, workspace));
    // Assignment may reuse the same vector allocation and node count.
    other = index;
    expectSameNeighbors(other.query({1.0}, 2), other.query({1.0}, 2, 4, workspace));
    other.add(5, {1.0});
    other.build();
    expectSameNeighbors(other.query({1.0}, 2), other.query({1.0}, 2, 4, workspace));
}

TEST(MotionIndex, WorkspaceKeepsExactFallbackAndEqualDistanceTieOrder)
{
    pfcore::MotionIndex index;
    for (std::size_t i = 0; i < 600; ++i) {
        std::vector<double> embedding(300, 0.0);
        embedding[i / 2] = 1.0;
        index.add(i, std::move(embedding));
    }
    index.build();
    std::vector<double> query(300, 0.0);
    query[260] = 1.0;
    pfcore::MotionIndex::QueryWorkspace workspace;
    for (const auto count : {2U, 64U, 128U, 256U, 600U, 2U})
        expectSameNeighbors(index.query(query, count, count * 2),
                            index.query(query, count, count * 2, workspace));
}

TEST(MotionIndex, EqualDistanceClustersDoNotHideExactNeighbors)
{
    pfcore::MotionIndex index;
    for (std::size_t id = 0; id < 240; ++id) {
        std::vector<double> embedding(120, 0.0);
        embedding[id / 2] = 1.0;
        index.add(id, std::move(embedding));
    }
    index.build();
    std::vector<double> query(120, 0.0);
    query[100] = 1.0;
    const auto neighbors = index.query(query, 2, 130);
    ASSERT_EQ(neighbors.size(), 2u);
    EXPECT_EQ(neighbors[0].id, 200u);
    EXPECT_EQ(neighbors[1].id, 201u);
    EXPECT_DOUBLE_EQ(neighbors[0].similarity, 1.0);
}

TEST(MotionIndex, DegenerateLargeGraphStillFindsExactNeighbors)
{
    pfcore::MotionIndex index;
    for (std::size_t id = 0; id < 600; ++id) {
        std::vector<double> embedding(300, 0.0);
        embedding[id / 2] = 1.0;
        index.add(id, std::move(embedding));
    }
    index.build();
    std::vector<double> query(300, 0.0);
    query[260] = 1.0;
    const auto neighbors = index.query(query, 2, 130);
    ASSERT_EQ(neighbors.size(), 2u);
    EXPECT_EQ(neighbors[0].id, 520u);
    EXPECT_EQ(neighbors[1].id, 521u);
}

TEST(MotionIndex, ReturnsNearestEmbeddingsInStableOrder)
{
    pfcore::MotionIndex index(4, 16);
    index.add(10, {0.0, 0.0, 0.0});
    index.add(20, {1.0, 1.0, 1.0});
    index.add(30, {0.05, 0.0, 0.0});
    index.build();

    const auto neighbours = index.query({0.02, 0.0, 0.0}, 2, 8);
    ASSERT_EQ(neighbours.size(), 2U);
    EXPECT_EQ(neighbours[0].id, 10U);
    EXPECT_EQ(neighbours[1].id, 30U);
    EXPECT_GT(neighbours[0].similarity, neighbours[1].similarity);
}

TEST(MotionIndex, EmptyAndSingleNodeQueriesAreSafe)
{
    pfcore::MotionIndex index;
    index.build();
    EXPECT_TRUE(index.query({1.0}, 4).empty());

    index.add(7, {1.0});
    index.build();
    const auto result = index.query({1.0}, 4);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.front().id, 7U);
}
