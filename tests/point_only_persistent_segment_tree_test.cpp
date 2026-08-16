#include <valseg/point_only_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::PointOnlyPersistentSegmentTree;

// Deterministic suite for the point-only persistent segment tree.
// The nodeCount() verification is critical here: it must prove that single
// updates scale logarithmically, while range updates scale linearly with range size.

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

// ---------------------------------------------------------------------------
// Path-Copying & Point-Only Allocation Evidence
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, SinglePointUpdateBehavesLikePathCopying) {
  PointOnlyPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // Update a single element [2, 2].
  // Path copying should allocate exactly 3 nodes for N = 4:
  // - 1 leaf node for index 2
  // - 1 parent node covering [2, 3]
  // - 1 root node covering [0, 3]
  std::size_t v1 = tree.rangeAdd(2, 2, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + 3 = 10 nodes total

  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
  EXPECT_EQ(tree.rangeSum(v1, 2, 2), 35);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 105);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 30); // shared subtrees unchanged
}

TEST(PointOnlyPersistentSegmentTreeTest, FullRangeUpdateSinksToLeaves) {
  PointOnlyPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u);

  // Update full range [0, 3].
  // Without lazy tags, this update must recurse all the way to every leaf.
  // This causes it to allocate 2n - 1 nodes (7 nodes), duplicating the entire tree.
  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.nodeCount(), 14u); // 7 + 7 = 14 nodes total

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 120); // 100 + 5*4
}

TEST(PointOnlyPersistentSegmentTreeTest, PartialRangeUpdateRecursesOnlyToOverlappingLeaves) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8}); // N = 8, height = 3

  EXPECT_EQ(tree.nodeCount(), 15u); // 2n - 1 = 15

  // Update range [0, 2] (3 elements: leaves 0, 1, 2).
  // Touches:
  // - Leaves: 0, 1, 2 (3 nodes)
  // - Left sub-internal [0, 1] (1 node, parents of 0 and 1)
  // - Mid sub-internal [2, 3] (1 node, parent of 2 and 3)
  // - Left subtree root [0, 3] (1 node)
  // - Overall root [0, 7] (1 node)
  // Total nodes allocated: 3 + 1 + 1 + 1 + 1 = 7 nodes.
  // Note: leaf 3, sub-internal [4, 5], [6, 7] and left-subtree [4, 7] are shared.
  std::size_t v1 = tree.rangeAdd(0, 2, 10);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.nodeCount(), 22u); // 15 + 7 = 22 nodes

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 36); // 6 + 10*3
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 4);  // untouched leaf 3
  EXPECT_EQ(tree.rangeSum(v1, 4, 7), 26); // untouched shared subtree [4, 7]
}

// ---------------------------------------------------------------------------
// Cumulative updates and zero deltas
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, MultipleUpdatesAccumulate) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3, 4});

  std::size_t v1 = tree.rangeAdd(1, 2, 5);  // updates elements 1, 2
  std::size_t v2 = tree.rangeAdd(2, 3, 10); // updates elements 2, 3

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);

  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (2+5) + (3+5) = 7 + 8 = 15
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 20); // 1 + 7 + 8 + 4 = 20

  // In v2, elements are: 1, (2+5), (3+5+10), (4+10) = 1, 7, 18, 14
  EXPECT_EQ(tree.rangeSum(v2, 2, 3), 32); // 18 + 14 = 32
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 7);  // 7
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 40); // 1 + 7 + 18 + 14 = 40
}

TEST(PointOnlyPersistentSegmentTreeTest, ZeroDeltaUpdateIsAllocFree) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.nodeCount(), before);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

// ---------------------------------------------------------------------------
// Failed operation validation
// ---------------------------------------------------------------------------

TEST(PointOnlyPersistentSegmentTreeTest, FailedUpdateRollsBack) {
  PointOnlyPersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 5, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
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
