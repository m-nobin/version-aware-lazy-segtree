#include <valseg/persistent_lazy_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::PersistentLazySegmentTree;

// Deterministic suite for the persistent tree, covering issue #7.
// The version numbers returned by rangeAdd, versionCount(), and nodeCount()
// serve as evidence of publication and structural sharing.

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, InitializationPublishesVersionZero) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 3);
}

TEST(PersistentLazySegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  PersistentLazySegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PersistentLazySegmentTreeTest, ReinitializationReplacesAllState) {
  PersistentLazySegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u);
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone
}

TEST(PersistentLazySegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  PersistentLazySegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PersistentLazySegmentTreeTest, SingleElementTree) {
  PersistentLazySegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 2u);      // one copied leaf
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
}

// ---------------------------------------------------------------------------
// Hand-traced design example
// ---------------------------------------------------------------------------

// Reference example from the design document:
//   version 0: [1, 2, 3, 4]        sum = 10
//   version 1: rangeAdd(0, 3, 5)   sum = 30, full coverage copies only the root
//   version 2: rangeAdd(1, 2, 2)   sum = 34, partial update copies 5 nodes
TEST(PersistentLazySegmentTreeTest, HandTracedHistoricalResults) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);

  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 8u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);

  std::size_t v2 = tree.rangeAdd(1, 2, 2);

  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);
  EXPECT_EQ(tree.nodeCount(), 13u); // root, both children, one leaf per side

  // Every historical result stays unchanged.
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 19);
}

// ---------------------------------------------------------------------------
// Full, partial, and overlapping updates
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, FullCoverageUpdateCopiesOnlyTheRoot) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 1u); // both subtrees stay shared
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
}

TEST(PersistentLazySegmentTreeTest, PartialUpdateLeftHalfOnly) {
  PersistentLazySegmentTree tree({10, 20, 30, 40});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 1, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 2u); // new root + new left child [0,1]

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_EQ(tree.rangeSum(0, 2, 3), 70);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);

  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 40);
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 70); // right half shared
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
}

TEST(PersistentLazySegmentTreeTest, PartialUpdateRightHalfOnly) {
  PersistentLazySegmentTree tree({10, 20, 30, 40});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(2, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 2u); // new root + new right child [2,3]

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_EQ(tree.rangeSum(0, 2, 3), 70);

  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 30); // left half shared
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 80);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
}

// v1: rangeAdd(1, 3, 10) → logical values {1, 12, 13, 14, 5}
// v2: rangeAdd(2, 4, 5)  → logical values {1, 12, 18, 19, 10}
TEST(PersistentLazySegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  std::size_t v1 = tree.rangeAdd(1, 3, 10);
  std::size_t v2 = tree.rangeAdd(2, 4, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);

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

TEST(PersistentLazySegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  PersistentLazySegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 1);
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + one copied root per update

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

TEST(PersistentLazySegmentTreeTest, SingleLeafUpdateCreatesExactPathLength) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

  EXPECT_EQ(tree.nodeCount(), 15u);

  std::size_t v1 = tree.rangeAdd(3, 3, 100);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 19u); // n = 8: root-to-leaf path copies 4 nodes

  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(v1, 0, 7), 136);
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 104);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);  // untouched left segment
  EXPECT_EQ(tree.rangeSum(v1, 4, 7), 26); // untouched right segment
}

// ---------------------------------------------------------------------------
// Negative values and deltas
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, NegativeInitialValues) {
  PersistentLazySegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(PersistentLazySegmentTreeTest, NegativeDelta) {
  PersistentLazySegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(PersistentLazySegmentTreeTest, NegativeDeltaPartialRange) {
  PersistentLazySegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2].
TEST(PersistentLazySegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  PersistentLazySegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  std::size_t v2 = tree.rangeAdd(1, 2, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34); // 40 − 3×2
  EXPECT_EQ(tree.rangeSum(v2, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 14); // (10−3)×2
  EXPECT_EQ(tree.rangeSum(v2, 3, 3), 10);
}

// ---------------------------------------------------------------------------
// Zero-delta updates
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  PersistentLazySegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 5);
}

TEST(PersistentLazySegmentTreeTest, MultipleZeroDeltaUpdatesStackCorrectly) {
  PersistentLazySegmentTree tree({4, 5, 6});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);
  std::size_t v2 = tree.rangeAdd(0, 2, 0);
  std::size_t v3 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), before); // still no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 15);
}

TEST(PersistentLazySegmentTreeTest, ZeroDeltaUpdateStillValidatesRange) {
  PersistentLazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 0), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 3, 0), std::out_of_range);     // right >= size

  EXPECT_EQ(tree.versionCount(), 1u); // failed fast path publishes nothing
}

TEST(PersistentLazySegmentTreeTest, UpdateAfterZeroDeltaLeavesSharedVersionIntact) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  std::size_t v1 = tree.rangeAdd(0, 3, 0); // shares the version-0 root
  std::size_t v2 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 5);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 30);
}

// ---------------------------------------------------------------------------
// Historical isolation
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

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
TEST(PersistentLazySegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  PersistentLazySegmentTree tree({0});

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

// Three stacked full-coverage updates leave lazy = 6 on the v3 root; partial
// queries must resolve the accumulated value in a single read-only pass.
TEST(PersistentLazySegmentTreeTest, DeepLazyAccumulationResolvedCorrectly) {
  PersistentLazySegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 2);
  std::size_t v3 = tree.rangeAdd(0, 3, 3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 12);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 24);

  EXPECT_EQ(tree.rangeSum(v3, 1, 2), 12); // 6 per element × 2 elements
  EXPECT_EQ(tree.rangeSum(v2, 0, 1), 6);  // 3 per element × 2 elements
}

TEST(PersistentLazySegmentTreeTest, InterleavedPartialUpdateIsolation) {
  PersistentLazySegmentTree tree({1, 1, 1, 1});

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

// ---------------------------------------------------------------------------
// Failed-operation isolation
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, FailedUpdatePublishesNothing) {
  PersistentLazySegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}

TEST(PersistentLazySegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

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

// ---------------------------------------------------------------------------
// Validation contracts
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, DefaultTreeHasNoVersions) {
  PersistentLazySegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PersistentLazySegmentTreeTest, InvalidVersionQueryThrows) {
  PersistentLazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(PersistentLazySegmentTreeTest, InvalidRangeOnUpdateThrows) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(PersistentLazySegmentTreeTest, InvalidRangeOnQueryThrows) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ---------------------------------------------------------------------------
// Large 64-bit values
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;

  PersistentLazySegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(PersistentLazySegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;

  PersistentLazySegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 2 * base + 2 * delta);
}
