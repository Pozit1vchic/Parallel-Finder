#include <gtest/gtest.h>

#include <pfcore/MotionIndex.hpp>

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
