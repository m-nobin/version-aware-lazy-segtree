#include <valseg/persistent_lazy_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>

using valseg::PersistentLazySegmentTree;

// ---------------------------------------------------------------------------
// Baseline suite for the persistent tree.
//
// These exemplar cases establish the testing patterns for the deterministic
// coverage tracked in issue #7: assert on version numbers returned by
// rangeAdd, query every affected version after each update, and use
// versionCount() and nodeCount() as evidence of publication and sharing.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, InitializationPublishesVersionZero) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);

  // A tree over n leaves stores exactly 2 * n - 1 nodes.
  EXPECT_EQ(tree.nodeCount(), 9u);

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 3);
}

// ---------------------------------------------------------------------------
// Hand-traced historical results (design reference example)
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, HandTracedHistoricalResults) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);

  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  // Full coverage publishes one version and copies only the root.
  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 8u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);

  std::size_t v2 = tree.rangeAdd(1, 2, 2);

  // The partial update copies the root, both children, and one leaf per
  // side; every earlier result must remain unchanged.
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);
  EXPECT_EQ(tree.nodeCount(), 13u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 19);
}

// ---------------------------------------------------------------------------
// Structural sharing evidence
// ---------------------------------------------------------------------------

TEST(PersistentLazySegmentTreeTest, FullCoverageUpdateCopiesOnlyTheRoot) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  // Full coverage stops at the root; both subtrees stay shared.
  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
}

TEST(PersistentLazySegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  PersistentLazySegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before);

  // Both versions read identically, including partial and leaf ranges.
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 5);
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
