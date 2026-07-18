#include <valseg/lazy_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::LazySegmentTree;

// ---------------------------------------------------------------------------
// Core operation tests
// ---------------------------------------------------------------------------

TEST(LazySegmentTreeTest, Initialization) {
  LazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.rangeSum(0, 4), 15);
}

TEST(LazySegmentTreeTest, SingleRangeUpdate) {
  LazySegmentTree tree({1, 2, 3, 4, 5});

  tree.rangeAdd(1, 3, 5);

  EXPECT_EQ(tree.rangeSum(0, 4), 30);
  EXPECT_EQ(tree.rangeSum(1, 3), 24);
}

TEST(LazySegmentTreeTest, MultipleUpdates) {
  LazySegmentTree tree({1, 2, 3, 4, 5});

  tree.rangeAdd(0, 2, 10);
  tree.rangeAdd(2, 4, -2);

  EXPECT_EQ(tree.rangeSum(0, 4), 39);
}

TEST(LazySegmentTreeTest, WholeArrayUpdate) {
  LazySegmentTree tree({1, 1, 1, 1});

  tree.rangeAdd(0, 3, 3);

  EXPECT_EQ(tree.rangeSum(0, 3), 16);
}

TEST(LazySegmentTreeTest, SingleElementUpdate) {
  LazySegmentTree tree({5, 5, 5});

  tree.rangeAdd(1, 1, 7);

  EXPECT_EQ(tree.rangeSum(1, 1), 12);
  EXPECT_EQ(tree.rangeSum(0, 2), 22);
}

TEST(LazySegmentTreeTest, NegativeUpdates) {
  LazySegmentTree tree({10, 10, 10, 10});

  tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 3), 28);
}

TEST(LazySegmentTreeTest, OverlappingUpdates) {
  LazySegmentTree tree({1, 2, 3, 4, 5});

  tree.rangeAdd(0, 3, 2);
  tree.rangeAdd(2, 4, 5);

  EXPECT_EQ(tree.rangeSum(0, 4), 38);
  EXPECT_EQ(tree.rangeSum(2, 3), 21);
}

TEST(LazySegmentTreeTest, MultipleQueries) {
  LazySegmentTree tree({2, 4, 6, 8, 10});

  EXPECT_EQ(tree.rangeSum(0, 1), 6);
  EXPECT_EQ(tree.rangeSum(2, 4), 24);

  tree.rangeAdd(1, 3, 1);

  EXPECT_EQ(tree.rangeSum(0, 4), 33);
  EXPECT_EQ(tree.rangeSum(1, 3), 21);
}

// ---------------------------------------------------------------------------
// Edge case tests
// ---------------------------------------------------------------------------

TEST(LazySegmentTreeTest, ZeroDeltaUpdate) {
  LazySegmentTree tree({1, 2, 3, 4, 5});

  tree.rangeAdd(0, 4, 0);

  EXPECT_EQ(tree.rangeSum(0, 4), 15);
  EXPECT_EQ(tree.rangeSum(1, 3), 9);
}

TEST(LazySegmentTreeTest, SingleElementArray) {
  LazySegmentTree tree({42});

  EXPECT_EQ(tree.rangeSum(0, 0), 42);

  tree.rangeAdd(0, 0, 8);

  EXPECT_EQ(tree.rangeSum(0, 0), 50);
}

TEST(LazySegmentTreeTest, EmptyTreeRangeSumThrows) {
  LazySegmentTree tree;

  EXPECT_THROW(tree.rangeSum(0, 0), std::runtime_error);
}

TEST(LazySegmentTreeTest, EmptyTreeRangeAddThrows) {
  LazySegmentTree tree;

  EXPECT_THROW(tree.rangeAdd(0, 0, 5), std::runtime_error);
}

TEST(LazySegmentTreeTest, LargeValues) {
  constexpr std::size_t n = 1000;
  constexpr long long value = 1'000'000'000LL;

  std::vector<long long> data(n, value);
  LazySegmentTree tree(data);

  EXPECT_EQ(tree.rangeSum(0, n - 1), value * static_cast<long long>(n));

  tree.rangeAdd(0, n - 1, value);

  EXPECT_EQ(tree.rangeSum(0, n - 1), value * static_cast<long long>(n) * 2);
}

TEST(LazySegmentTreeTest, OutOfRangeAccessRangeSumThrows) {
  LazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(0, 3), std::out_of_range);
}

TEST(LazySegmentTreeTest, OutOfRangeAccessRangeAddThrows) {
  LazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);
}

TEST(LazySegmentTreeTest, ReversedRangeSumThrows) {
  LazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(2, 1), std::invalid_argument);
}

TEST(LazySegmentTreeTest, ReversedRangeAddThrows) {
  LazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
}

TEST(LazySegmentTreeTest, ReinitializeResetsTreeState) {
  LazySegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 5);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.rangeSum(0, 1), 30);
  EXPECT_THROW(tree.rangeSum(0, 2), std::out_of_range);
}
