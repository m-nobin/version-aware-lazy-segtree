#include <valseg/point_only_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::PointOnlyPersistentSegmentTree;

// Deterministic suite for the point-only persistent segment tree.
// nodeCount() is the structural evidence: a point update must cost one
// root-to-leaf path (h + 1 nodes), while a range update must copy every node
// whose segment intersects the range, growing linearly with the range width.

namespace {

// Independent count of the nodes whose segment intersects [queryLeft, queryRight]
// in the tree over [segmentLeft, segmentRight]: exactly the nodes a point-only
// update must copy.
std::size_t touchedNodes(std::size_t segmentLeft, std::size_t segmentRight, std::size_t queryLeft,
                         std::size_t queryRight) {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }
  if (segmentLeft == segmentRight) {
    return 1;
  }
  const std::size_t middle = (segmentLeft + segmentRight) / 2;
  return 1 + touchedNodes(segmentLeft, middle, queryLeft, queryRight) +
         touchedNodes(middle + 1, segmentRight, queryLeft, queryRight);
}

} // namespace

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, InitializationPublishesVersionZero) {
  PointOnlyPersistentSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(PointOnlyPersistentSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  PointOnlyPersistentSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PointOnlyPersistentSegmentTreeTest, ReinitializationReplacesAllState) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u);
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone
}

TEST(PointOnlyPersistentSegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PointOnlyPersistentSegmentTreeTest, SingleElementTree) {
  PointOnlyPersistentSegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 2u);      // the single leaf is copied once
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
}

// ---------------------------------------------------------------------------
// Path copying and point-only allocation evidence
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, SinglePointUpdateCopiesOnePath) {
  PointOnlyPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // Updating [2, 2] copies one root-to-leaf path of h + 1 = 3 nodes for n = 4:
  // the leaf for index 2, its parent covering [2, 3], and the root [0, 3].
  std::size_t v1 = tree.rangeAdd(2, 2, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + 3

  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
  EXPECT_EQ(tree.rangeSum(v1, 2, 2), 35);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 105);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 30); // shared subtree unchanged
}

// n = 16, height 4: every single-leaf update appends exactly h + 1 = 5 nodes.
TEST(PointOnlyPersistentSegmentTreeTest, SingleLeafUpdateCreatesExactPathLength) {
  PointOnlyPersistentSegmentTree tree(std::vector<long long>(16, 0));
  const std::size_t pathLength = 5;

  for (std::size_t leaf = 0; leaf < 16; ++leaf) {
    const std::size_t before = tree.nodeCount();
    tree.rangeAdd(leaf, leaf, 1);
    EXPECT_EQ(tree.nodeCount() - before, pathLength) << "leaf " << leaf;
  }
  EXPECT_EQ(tree.rangeSum(16, 0, 15), 16);
  EXPECT_EQ(tree.rangeSum(0, 0, 15), 0); // version 0 still isolated
}

TEST(PointOnlyPersistentSegmentTreeTest, FullRangeUpdateCopiesWholeTree) {
  PointOnlyPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u);

  // Without a lazy tag the update descends to every leaf, so a full-range
  // update copies all 2n − 1 nodes: the worst case, matching the full-copy tree.
  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.nodeCount(), 14u); // 7 + 7

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 120); // 100 + 5×4
}

TEST(PointOnlyPersistentSegmentTreeTest, PartialRangeUpdateCopiesOnlyIntersectingNodes) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8}); // n = 8, height 3

  EXPECT_EQ(tree.nodeCount(), 15u); // 2n − 1

  // Updating [0, 2] copies the nodes whose segment intersects [0, 2]:
  // leaves 0, 1, 2; internal [0, 1] and [2, 3]; [0, 3]; and the root [0, 7]
  // — 7 nodes. Leaf 3 and the whole subtree [4, 7] stay shared.
  std::size_t v1 = tree.rangeAdd(0, 2, 10);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.nodeCount(), 22u); // 15 + 7

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 36); // 6 + 10×3
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 4);  // untouched leaf 3
  EXPECT_EQ(tree.rangeSum(v1, 4, 7), 26); // untouched shared subtree [4, 7]
}

// The quantitative Θ(k + log n) evidence on n = 16: for every leaf range
// [left, right] the arena must grow by exactly the number of nodes whose
// segment intersects the range (independently counted by touchedNodes), the
// full range must copy all 2n − 1 nodes, and no update may exceed 2k + 2h − 1.
TEST(PointOnlyPersistentSegmentTreeTest, NodeCountGrowsByIntersectingNodesPerRange) {
  const std::size_t n = 16;
  const std::size_t height = 4;
  PointOnlyPersistentSegmentTree tree(std::vector<long long>(n, 1));

  EXPECT_EQ(tree.nodeCount(), 2 * n - 1);

  for (std::size_t left = 0; left < n; ++left) {
    for (std::size_t right = left; right < n; ++right) {
      const std::size_t k = right - left + 1;
      const std::size_t before = tree.nodeCount();
      tree.rangeAdd(left, right, 1);
      const std::size_t appended = tree.nodeCount() - before;

      EXPECT_EQ(appended, touchedNodes(0, n - 1, left, right))
          << "range [" << left << ", " << right << "]";
      EXPECT_LE(appended, 2 * k + 2 * height - 1) << "range [" << left << ", " << right << "]";
      EXPECT_GE(appended, k + height) << "range [" << left << ", " << right << "]";
    }
  }

  const std::size_t before = tree.nodeCount();
  tree.rangeAdd(0, n - 1, 1);
  EXPECT_EQ(tree.nodeCount() - before, 2 * n - 1); // full range copies the whole tree

  EXPECT_EQ(tree.rangeSum(0, 0, n - 1), static_cast<long long>(n)); // version 0 isolated
}

// ---------------------------------------------------------------------------
// Cumulative updates
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5});

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

TEST(PointOnlyPersistentSegmentTreeTest, MultipleUpdatesAccumulate) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4});

  std::size_t v1 = tree.rangeAdd(1, 2, 5);  // elements 1, 2
  std::size_t v2 = tree.rangeAdd(2, 3, 10); // elements 2, 3

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);

  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (2+5) + (3+5)
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 20); // 1 + 7 + 8 + 4

  // v2 elements: 1, 7, 18, 14
  EXPECT_EQ(tree.rangeSum(v2, 2, 3), 32);
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 7);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 40);
}

TEST(PointOnlyPersistentSegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  PointOnlyPersistentSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 1);
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), 28u); // every full-range update copies all 7 nodes

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Zero-delta updates
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

TEST(PointOnlyPersistentSegmentTreeTest, MultipleZeroDeltaUpdatesStackCorrectly) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});
  const std::size_t before = tree.nodeCount();

  std::size_t v1 = tree.rangeAdd(0, 2, 0);
  std::size_t v2 = tree.rangeAdd(1, 1, 0);
  std::size_t v3 = tree.rangeAdd(0, 0, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), before);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 6);
}

TEST(PointOnlyPersistentSegmentTreeTest, ZeroDeltaUpdateStillValidatesRange) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 0), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 0), std::out_of_range);
  EXPECT_EQ(tree.versionCount(), 1u);
}

TEST(PointOnlyPersistentSegmentTreeTest, UpdateAfterZeroDeltaLeavesSharedVersionIntact) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  std::size_t v1 = tree.rangeAdd(0, 2, 0); // shares version 0's root
  std::size_t v2 = tree.rangeAdd(0, 2, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 36);
}

// ---------------------------------------------------------------------------
// Negative values and deltas
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, NegativeInitialValues) {
  PointOnlyPersistentSegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(PointOnlyPersistentSegmentTreeTest, NegativeDelta) {
  PointOnlyPersistentSegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(PointOnlyPersistentSegmentTreeTest, NegativeDeltaPartialRange) {
  PointOnlyPersistentSegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2].
TEST(PointOnlyPersistentSegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  PointOnlyPersistentSegmentTree tree({0, 0, 0, 0});

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
// Historical isolation
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

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
TEST(PointOnlyPersistentSegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  PointOnlyPersistentSegmentTree tree({0});
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

TEST(PointOnlyPersistentSegmentTreeTest, InterleavedPartialUpdateIsolation) {
  PointOnlyPersistentSegmentTree tree({1, 1, 1, 1});

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

TEST(PointOnlyPersistentSegmentTreeTest, FailedUpdatePublishesNothing) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}

TEST(PointOnlyPersistentSegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4});

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

TEST(PointOnlyPersistentSegmentTreeTest, DefaultTreeHasNoVersions) {
  PointOnlyPersistentSegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(PointOnlyPersistentSegmentTreeTest, InvalidVersionQueryThrows) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(PointOnlyPersistentSegmentTreeTest, InvalidRangeOnUpdateThrows) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(PointOnlyPersistentSegmentTreeTest, InvalidRangeOnQueryThrows) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ---------------------------------------------------------------------------
// Large 64-bit values
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;
  PointOnlyPersistentSegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(PointOnlyPersistentSegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;
  PointOnlyPersistentSegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}
