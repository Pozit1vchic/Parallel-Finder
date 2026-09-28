#include <gtest/gtest.h>

#include <pfcore/MotionIndex.hpp>

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
