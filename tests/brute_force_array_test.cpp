#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>

using valseg::BruteForceArray;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, Initialization) {
  BruteForceArray arr({1, 2, 3, 4, 5});

  EXPECT_EQ(arr.versionCount(), 1u);
  EXPECT_EQ(arr.size(), 5u);
  EXPECT_EQ(arr.rangeSum(0, 0, 4), 15);
}

// ---------------------------------------------------------------------------
// Single range update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, SingleRangeUpdate) {
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

TEST(BruteForceArrayTest, MultipleVersions) {
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

TEST(BruteForceArrayTest, HistoricalQueries) {
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

TEST(BruteForceArrayTest, NegativeUpdates) {
  BruteForceArray arr({5, 5, 5, 5});

  std::size_t v1 = arr.rangeAdd(0, 0, 3, -2);

  EXPECT_EQ(arr.rangeSum(v1, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Single element update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, SingleElementUpdate) {
  BruteForceArray arr({1, 2, 3});

  std::size_t v1 = arr.rangeAdd(0, 1, 1, 10);

  EXPECT_EQ(arr.rangeSum(v1, 1, 1), 12);
  EXPECT_EQ(arr.rangeSum(0, 1, 1), 2);
}

// ---------------------------------------------------------------------------
// Whole array update
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, WholeArrayUpdate) {
  BruteForceArray arr({1, 1, 1, 1});

  std::size_t v1 = arr.rangeAdd(0, 0, 3, 3);

  EXPECT_EQ(arr.rangeSum(v1, 0, 3), 16);
}

// ---------------------------------------------------------------------------
// Version isolation — modifying v2 must not alter v1
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, VersionIsolation) {
  BruteForceArray arr({1, 2, 3});

  std::size_t v1 = arr.rangeAdd(0, 0, 0, 10);
  std::size_t v2 = arr.rangeAdd(v1, 1, 1, 20);

  EXPECT_EQ(arr.rangeSum(v1, 0, 2), 16);
  EXPECT_EQ(arr.rangeSum(v2, 0, 2), 36);
}

// ---------------------------------------------------------------------------
// Validation contracts
// ---------------------------------------------------------------------------

TEST(BruteForceArrayTest, InvalidVersionQueryThrows) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(BruteForceArrayTest, InvalidBaseVersionUpdateThrowsWithoutCreatingVersion) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeAdd(1, 0, 2, 5), std::out_of_range);
  EXPECT_EQ(arr.versionCount(), 1u);
}

TEST(BruteForceArrayTest, DefaultArrayHasNoVersion) {
  BruteForceArray arr;

  EXPECT_EQ(arr.versionCount(), 0u);
  EXPECT_EQ(arr.size(), 0u);
  EXPECT_THROW(arr.rangeSum(0, 0, 0), std::out_of_range);
  EXPECT_THROW(arr.rangeAdd(0, 0, 0, 1), std::out_of_range);
}

TEST(BruteForceArrayTest, InitializedEmptyArrayRejectsRanges) {
  BruteForceArray arr;
  arr.initialize({});

  EXPECT_EQ(arr.versionCount(), 1u);
  EXPECT_EQ(arr.size(), 0u);
  EXPECT_THROW(arr.rangeSum(0, 0, 0), std::out_of_range);
  EXPECT_THROW(arr.rangeAdd(0, 0, 0, 1), std::out_of_range);
  EXPECT_EQ(arr.versionCount(), 1u);
}

TEST(BruteForceArrayTest, ReversedQueryRangeThrows) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeSum(0, 2, 1), std::invalid_argument);
}

TEST(BruteForceArrayTest, ReversedUpdateRangeThrowsWithoutCreatingVersion) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeAdd(0, 2, 1, 5), std::invalid_argument);
  EXPECT_EQ(arr.versionCount(), 1u);
}

TEST(BruteForceArrayTest, OutOfRangeQueryThrows) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeSum(0, 0, 3), std::out_of_range);
}

TEST(BruteForceArrayTest, OutOfRangeUpdateThrowsWithoutCreatingVersion) {
  BruteForceArray arr({1, 2, 3});

  EXPECT_THROW(arr.rangeAdd(0, 0, 3, 5), std::out_of_range);
  EXPECT_EQ(arr.versionCount(), 1u);
}

TEST(BruteForceArrayTest, ReinitializeResetsVersionHistory) {
  BruteForceArray arr({1, 2, 3});
  std::size_t oldVersion = arr.rangeAdd(0, 0, 2, 5);

  arr.initialize({10, 20});

  EXPECT_EQ(arr.versionCount(), 1u);
  EXPECT_EQ(arr.size(), 2u);
  EXPECT_EQ(arr.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(arr.rangeSum(oldVersion, 0, 1), std::out_of_range);
}
