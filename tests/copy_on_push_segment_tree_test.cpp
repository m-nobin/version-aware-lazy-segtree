#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <vector>

#include "../bench/copy_on_push_segment_tree.hpp"

namespace {

using valseg::BruteForceArray;
using valseg::bench::CopyOnPushSegmentTree;

TEST(CopyOnPushSegmentTreeTest, PushesTaggedAncestorsIntoCopiedChildren) {
  CopyOnPushSegmentTree tree;
  tree.initialize({1, 2, 3, 4});
  EXPECT_EQ(tree.nodeCount(), 7u);

  tree.rangeAdd(0, 3, 10);
  EXPECT_EQ(tree.nodeCount(), 8u);

  // Descending to one leaf pushes the root tag to two copied children, then
  // pushes the left-child tag to two copied leaves, before copying the update
  // path itself: seven new records in total.
  tree.rangeAdd(0, 0, 1);
  EXPECT_EQ(tree.nodeCount(), 15u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 50);
  EXPECT_EQ(tree.rangeSum(2, 0, 3), 51);
  EXPECT_EQ(tree.rangeSum(2, 0, 0), 12);
}

TEST(CopyOnPushSegmentTreeTest, MatchesVersionedArrayOnSeededHistory) {
  constexpr std::size_t size = 32;
  std::mt19937_64 rng(20260830);
  std::vector<long long> initial(size);
  for (long long& value : initial) {
    value = static_cast<long long>(rng() % 101) - 50;
  }

  CopyOnPushSegmentTree tree;
  tree.initialize(initial);
  BruteForceArray oracle(initial);

  for (int step = 0; step < 120; ++step) {
    std::size_t left = rng() % size;
    std::size_t right = rng() % size;
    if (left > right) {
      std::swap(left, right);
    }
    const long long delta = static_cast<long long>(rng() % 41) - 20;
    ASSERT_EQ(tree.rangeAdd(left, right, delta),
              oracle.rangeAdd(oracle.versionCount() - 1, left, right, delta));
  }

  ASSERT_EQ(tree.versionCount(), oracle.versionCount());
  for (std::size_t version = 0; version < tree.versionCount(); ++version) {
    for (int probe = 0; probe < 6; ++probe) {
      std::size_t left = rng() % size;
      std::size_t right = rng() % size;
      if (left > right) {
        std::swap(left, right);
      }
      EXPECT_EQ(tree.rangeSum(version, left, right), oracle.rangeSum(version, left, right));
    }
  }
}

TEST(CopyOnPushSegmentTreeTest, RejectsInvalidOperationsWithoutPublishing) {
  CopyOnPushSegmentTree tree;
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);

  tree.initialize({1, 2, 3});
  EXPECT_THROW(tree.rangeAdd(2, 1, 1), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 1), std::out_of_range);
  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
  EXPECT_EQ(tree.versionCount(), 1u);
}

} // namespace
