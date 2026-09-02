#include <valseg/full_copy_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::FullCopyPersistentSegmentTree;

// Deterministic suite for the full-copy persistent segment tree.
// The version numbers returned by rangeAdd, versionCount(), and nodeCount()
// serve as evidence of full duplication (as opposed to structural sharing).

// Initialization

TEST(FullCopyPersistentSegmentTreeTest, InitializationPublishesVersionZero) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 3);
}

TEST(FullCopyPersistentSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  FullCopyPersistentSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FullCopyPersistentSegmentTreeTest, ReinitializationReplacesAllState) {
  FullCopyPersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u);
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone
}

TEST(FullCopyPersistentSegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  FullCopyPersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FullCopyPersistentSegmentTreeTest, SingleElementTree) {
  FullCopyPersistentSegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 2u);      // version 0 (1 node) + version 1 (1 node)
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
}

// Copying validation (Evidence that it does NOT structurally share)

TEST(FullCopyPersistentSegmentTreeTest, RangeUpdateCopiesWholeTree) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  // Full-copy tree should allocate another 2n - 1 nodes (7 nodes) for the update.
  EXPECT_EQ(tree.nodeCount(), 14u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
}

TEST(FullCopyPersistentSegmentTreeTest, PartialRangeUpdateCopiesWholeTree) {
  FullCopyPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u);

  std::size_t v1 = tree.rangeAdd(0, 1, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  // Unlike path-copying which only allocates log(n) nodes, full-copy
  // allocates the entire 2n - 1 nodes.
  EXPECT_EQ(tree.nodeCount(), 14u);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_EQ(tree.rangeSum(0, 2, 3), 70);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);

  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 40);
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 70);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
}

// After U non-zero updates on n = 16 the arena must hold exactly (2n − 1)(U + 1)
// nodes: the quantitative "no structural sharing" evidence for this baseline.
TEST(FullCopyPersistentSegmentTreeTest, NodeCountGrowsByFullTreePerUpdate) {
  FullCopyPersistentSegmentTree tree(std::vector<long long>(16, 1));
  const std::size_t nodesPerVersion = 2 * 16 - 1;

  EXPECT_EQ(tree.nodeCount(), nodesPerVersion);

  for (std::size_t u = 1; u <= 10; ++u) {
    std::size_t version = tree.rangeAdd(u % 16, 15, 1); // varying partial ranges
    EXPECT_EQ(version, u);
    EXPECT_EQ(tree.nodeCount(), nodesPerVersion * (u + 1));
  }
  EXPECT_EQ(tree.rangeSum(0, 0, 15), 16); // version 0 still isolated
}

// Cumulative updates

TEST(FullCopyPersistentSegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  std::size_t v1 = tree.rangeAdd(1, 3, 10);
  std::size_t v2 = tree.rangeAdd(2, 4, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);
  // 9 nodes initially, then 9 more, then 9 more = 27 nodes total
  EXPECT_EQ(tree.nodeCount(), 27u);

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 1, 3), 9);

  EXPECT_EQ(tree.rangeSum(v1, 0, 4), 45);
  EXPECT_EQ(tree.rangeSum(v1, 1, 3), 39);
  EXPECT_EQ(tree.rangeSum(v1, 2, 4), 32); // 13 + 14 + 5
  EXPECT_EQ(tree.rangeSum(v1, 4, 4), 5);  // element 4 untouched in v1

  EXPECT_EQ(tree.rangeSum(v2, 0, 4), 60);
  EXPECT_EQ(tree.rangeSum(v2, 2, 4), 47); // 18 + 19 + 10
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 12); // only v1 touched element 1

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15); // version 0 still isolated
}

TEST(FullCopyPersistentSegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  FullCopyPersistentSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 1);
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), 28u); // 4 full versions * 7 nodes = 28

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// Zero-delta updates

TEST(FullCopyPersistentSegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  FullCopyPersistentSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

// Negative values and deltas

TEST(FullCopyPersistentSegmentTreeTest, NegativeInitialValues) {
  FullCopyPersistentSegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(FullCopyPersistentSegmentTreeTest, NegativeDelta) {
  FullCopyPersistentSegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(FullCopyPersistentSegmentTreeTest, NegativeDeltaPartialRange) {
  FullCopyPersistentSegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2].
TEST(FullCopyPersistentSegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  FullCopyPersistentSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  std::size_t v2 = tree.rangeAdd(1, 2, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34); // 40 − 3×2
  EXPECT_EQ(tree.rangeSum(v2, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 14); // (10−3)×2
  EXPECT_EQ(tree.rangeSum(v2, 3, 3), 10);
}

// Historical isolation

TEST(FullCopyPersistentSegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

  for (int i = 0; i < 10; ++i) {
    tree.rangeAdd(0, 7, 10);
  }

  EXPECT_EQ(tree.versionCount(), 11u);
  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(0, 3, 5), 15); // 4 + 5 + 6
  EXPECT_EQ(tree.rangeSum(0, 7, 7), 8);
}

// Array {0}, ten updates of +1: version k must read k for every k.
TEST(FullCopyPersistentSegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  FullCopyPersistentSegmentTree tree({0});
  std::vector<std::size_t> versions;
  versions.reserve(10);

  for (int i = 0; i < 10; ++i) {
    versions.push_back(tree.rangeAdd(0, 0, 1));
  }

  EXPECT_EQ(tree.rangeSum(0, 0, 0), 0);
  for (std::size_t i = 0; i < versions.size(); ++i) {
    EXPECT_EQ(tree.rangeSum(versions[i], 0, 0), static_cast<long long>(i) + 1);
  }
}

TEST(FullCopyPersistentSegmentTreeTest, InterleavedPartialUpdateIsolation) {
  FullCopyPersistentSegmentTree tree({1, 1, 1, 1});

  std::size_t v1 = tree.rangeAdd(0, 1, 10); // left half only
  std::size_t v2 = tree.rangeAdd(2, 3, 20); // right half only
  std::size_t v3 = tree.rangeAdd(0, 1, 5);  // left half again

  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 22);
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 2);
  EXPECT_EQ(tree.rangeSum(v2, 0, 1), 22);
  EXPECT_EQ(tree.rangeSum(v2, 2, 3), 42);
  EXPECT_EQ(tree.rangeSum(v3, 0, 1), 32);
  EXPECT_EQ(tree.rangeSum(v3, 2, 3), 42);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4); // version 0 still isolated
}

// Failed-operation isolation

TEST(FullCopyPersistentSegmentTreeTest, FailedUpdatePublishesNothing) {
  FullCopyPersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}

TEST(FullCopyPersistentSegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4});

  tree.rangeAdd(0, 3, 5);

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeSum(5, 0, 3), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 4), std::out_of_range);     // right >= size

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 30);
}

// Validation contracts

TEST(FullCopyPersistentSegmentTreeTest, DefaultTreeHasNoVersions) {
  FullCopyPersistentSegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FullCopyPersistentSegmentTreeTest, InvalidVersionQueryThrows) {
  FullCopyPersistentSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(FullCopyPersistentSegmentTreeTest, InvalidRangeOnUpdateThrows) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(FullCopyPersistentSegmentTreeTest, InvalidRangeOnQueryThrows) {
  FullCopyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// Large 64-bit values

TEST(FullCopyPersistentSegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;
  FullCopyPersistentSegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(FullCopyPersistentSegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;
  FullCopyPersistentSegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}
