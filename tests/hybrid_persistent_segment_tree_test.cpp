#include <valseg/hybrid_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::HybridPersistentSegmentTree;

// Deterministic suite for the Hybrid persistent segment tree.
// Validates that modifications are buffered inside nodes up to BUFFER_CAPACITY (2)
// and that structural splits cascade appropriately.

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(HybridPersistentSegmentTreeTest, InitializationPublishesVersionZero) {
  HybridPersistentSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(HybridPersistentSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  HybridPersistentSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Buffer Modification Evidence
// ---------------------------------------------------------------------------

TEST(HybridPersistentSegmentTreeTest, UpdatesWithinBufferCapacityDoNotAllocateNodes) {
  HybridPersistentSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // Update 1 (Version 1) - Appends to buffer index 0
  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  EXPECT_EQ(tree.nodeCount(), 7u); // NO new nodes allocated
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);

  // Update 2 (Version 2) - Appends to buffer index 1
  std::size_t v2 = tree.rangeAdd(0, 3, 5);
  EXPECT_EQ(tree.nodeCount(), 7u); // Still NO new nodes allocated
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 60); // 40 + 5*4
}

TEST(HybridPersistentSegmentTreeTest, UpdateExceedingBufferTriggersNodeSplit) {
  HybridPersistentSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u);

  tree.rangeAdd(0, 3, 1); // v1 (buffer idx 0)
  tree.rangeAdd(0, 3, 1); // v2 (buffer idx 1, full)

  EXPECT_EQ(tree.nodeCount(), 7u); // Still 7 nodes

  std::size_t v3 = tree.rangeAdd(0, 3, 1); // v3 (exceeds capacity 2, triggers split)

  // The root node was updated. Because its buffer was full, it split.
  // The split node does NOT cascade structural changes up (it is the root).
  EXPECT_EQ(tree.nodeCount(), 8u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Cascading Validation
// ---------------------------------------------------------------------------

TEST(HybridPersistentSegmentTreeTest, StructuralSplitsCascadeToParents) {
  HybridPersistentSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u);

  // Buffer capacity is 2. Let's fill the buffer of leaf node [0].
  tree.rangeAdd(0, 0, 1); // v1
  tree.rangeAdd(0, 0, 1); // v2

  // Parent nodes ([0,1] and [0,3]) absorb the sum deltas into their buffers!
  // Leaf [0] absorbed into its buffer.
  // Subtree [0,1] absorbed delta=1 into buffer.
  // Root [0,3] absorbed delta=1 into buffer.
  EXPECT_EQ(tree.nodeCount(), 7u);

  // v3 will exceed the buffer of leaf [0]. It splits leaf 0.
  // This causes a structural pointer change for parent [0,1].
  // Structural changes CANNOT be buffered, so parent [0,1] MUST split.
  // This causes a structural pointer change for root [0,3], so it MUST split.
  // So v3 should allocate exactly 3 new nodes (a full path copy).
  std::size_t v3 = tree.rangeAdd(0, 0, 1);

  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + 3

  EXPECT_EQ(tree.rangeSum(0, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(1, 0, 0), 11);
  EXPECT_EQ(tree.rangeSum(2, 0, 0), 12);
  EXPECT_EQ(tree.rangeSum(v3, 0, 0), 13);

  // Query right side (untouched structurally, but old sum)
  EXPECT_EQ(tree.rangeSum(v3, 2, 3), 70);
}

// ---------------------------------------------------------------------------
// Failed operation validation
// ---------------------------------------------------------------------------

TEST(HybridPersistentSegmentTreeTest, FailedUpdateRollsBackInPlaceMutations) {
  HybridPersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 5, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}
