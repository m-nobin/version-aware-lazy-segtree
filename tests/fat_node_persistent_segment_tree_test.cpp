#include <valseg/fat_node_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::FatNodePersistentSegmentTree;

// Deterministic suite for the Fat-Node persistent segment tree.
// The nodeCount() verification is critical here: it must prove that updates
// do NOT allocate new nodes until the internal history arrays (capacity 3) fill up.

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, InitializationPublishesVersionZero) {
  FatNodePersistentSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(FatNodePersistentSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  FatNodePersistentSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FatNodePersistentSegmentTreeTest, ReinitializationReplacesAllState) {
  FatNodePersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u);
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone
}

// ---------------------------------------------------------------------------
// Fat-Node Allocation Evidence
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, UpdatesWithinCapacityDoNotAllocateNodes) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // Update 1 (Version 1) - Appends to history index 1
  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  EXPECT_EQ(tree.nodeCount(), 7u); // NO new nodes allocated!
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);

  // Update 2 (Version 2) - Appends to history index 2
  std::size_t v2 = tree.rangeAdd(0, 3, 5);
  EXPECT_EQ(tree.nodeCount(), 7u); // Still NO new nodes allocated!
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 60); // 40 + 5*4
}

TEST(FatNodePersistentSegmentTreeTest, UpdateExceedingCapacityTriggersNodeSplit) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u);

  tree.rangeAdd(0, 3, 1); // v1 (history idx 1)
  tree.rangeAdd(0, 3, 1); // v2 (history idx 2, full)

  EXPECT_EQ(tree.nodeCount(), 7u); // Still 7 nodes

  std::size_t v3 = tree.rangeAdd(0, 3, 1); // v3 (exceeds capacity 3, triggers split)

  // The root node was updated. Because it was full, it split.
  // We expect 1 new node (the split root).
  EXPECT_EQ(tree.nodeCount(), 8u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Complex Updates & Lazy Propagation
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, PartialRangeUpdatesWorkCorrectly) {
  FatNodePersistentSegmentTree tree({10, 20, 30, 40});

  std::size_t v1 = tree.rangeAdd(1, 2, 5); // Add 5 to index 1 and 2

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), 50);

  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 60);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 40);
}

// ---------------------------------------------------------------------------
// Zero-delta updates
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

// ---------------------------------------------------------------------------
// Failed operation validation
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, FailedUpdateRollsBackInPlaceMutations) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 5, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}

TEST(FatNodePersistentSegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4});

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

TEST(FatNodePersistentSegmentTreeTest, DefaultTreeHasNoVersions) {
  FatNodePersistentSegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FatNodePersistentSegmentTreeTest, InvalidVersionQueryThrows) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}
