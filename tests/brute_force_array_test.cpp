#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

using valseg::BruteForceArray;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, Initialization)
{
    BruteForceArray arr({1, 2, 3, 4, 5});

    EXPECT_EQ(arr.versionCount(), 1u);
    EXPECT_EQ(arr.size(), 5u);
    EXPECT_EQ(arr.rangeSum(0, 0, 4), 15);
}

// ---------------------------------------------------------------------------
// Single range update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, SingleRangeUpdate)
{
    BruteForceArray arr({1, 2, 3, 4, 5});

    std::size_t v1 = arr.rangeAdd(0, 1, 3, 5);

    EXPECT_EQ(v1, 1u);

    // Version 0 must remain unchanged.
    EXPECT_EQ(arr.rangeSum(0, 0, 4), 15);

    // Version 1 reflects the update.
    EXPECT_EQ(arr.rangeSum(v1, 0, 4), 30);
    EXPECT_EQ(arr.rangeSum(v1, 1, 3), 24);
}

// ---------------------------------------------------------------------------
// Multiple versions
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, MultipleVersions)
{
    BruteForceArray arr({1, 2, 3, 4, 5});

    std::size_t v1 = arr.rangeAdd(0, 0, 2, 10);
    std::size_t v2 = arr.rangeAdd(v1, 2, 4, -2);

    EXPECT_EQ(arr.rangeSum(0, 0, 4), 15);
    EXPECT_EQ(arr.rangeSum(v1, 0, 4), 45);
    EXPECT_EQ(arr.rangeSum(v2, 0, 4), 39);
}

// ---------------------------------------------------------------------------
// Historical queries — old versions remain unchanged
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, HistoricalQueries)
{
    BruteForceArray arr({10, 20, 30, 40});

    std::size_t v1 = arr.rangeAdd(0, 0, 3, 5);
    std::size_t v2 = arr.rangeAdd(v1, 1, 2, 10);

    EXPECT_EQ(arr.rangeSum(0, 0, 3), 100);
    EXPECT_EQ(arr.rangeSum(v1, 0, 3), 120);
    EXPECT_EQ(arr.rangeSum(v2, 0, 3), 140);
}

// ---------------------------------------------------------------------------
// Negative updates
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, NegativeUpdates)
{
    BruteForceArray arr({5, 5, 5, 5});

    std::size_t v1 = arr.rangeAdd(0, 0, 3, -2);

    EXPECT_EQ(arr.rangeSum(v1, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Single element update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, SingleElementUpdate)
{
    BruteForceArray arr({1, 2, 3});

    std::size_t v1 = arr.rangeAdd(0, 1, 1, 10);

    EXPECT_EQ(arr.rangeSum(v1, 1, 1), 12);
    EXPECT_EQ(arr.rangeSum(0, 1, 1), 2);
}

// ---------------------------------------------------------------------------
// Whole array update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, WholeArrayUpdate)
{
    BruteForceArray arr({1, 1, 1, 1});

    std::size_t v1 = arr.rangeAdd(0, 0, 3, 3);

    EXPECT_EQ(arr.rangeSum(v1, 0, 3), 16);
}

// ---------------------------------------------------------------------------
// Version isolation — modifying v2 must not alter v1
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, VersionIsolation)
{
    BruteForceArray arr({1, 2, 3});

    std::size_t v1 = arr.rangeAdd(0, 0, 0, 10);
    std::size_t v2 = arr.rangeAdd(v1, 1, 1, 20);

    EXPECT_EQ(arr.rangeSum(v1, 0, 2), 16);
    EXPECT_EQ(arr.rangeSum(v2, 0, 2), 36);
}