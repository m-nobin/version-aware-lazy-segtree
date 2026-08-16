#include <valseg/checkpointing_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

using valseg::CheckpointingSegmentTree;

// Deterministic suite for the Checkpointing persistent segment tree.
// Validates event log construction, temporary deep-copy instantiation,
// and historical log replay.

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, InitializationPublishesVersionZero) {
  CheckpointingSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  // N = 5 -> 9 nodes per tree. 1 live tree + 1 checkpoint = 18. Log is empty (0).
  EXPECT_EQ(tree.nodeCount(), 18u);
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(CheckpointingSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  CheckpointingSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Log Replay & Checkpointing
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, UpdatesLogAndReplayCorrectly) {
  CheckpointingSegmentTree tree({1, 2, 3, 4});

  std::size_t v1 = tree.rangeAdd(1, 2, 5);  // v1
  std::size_t v2 = tree.rangeAdd(2, 3, 10); // v2

  // Latest version (fast path)
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 40);

  // Historical versions (requires log replay from v0)
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);
}

TEST(CheckpointingSegmentTreeTest, ZeroDeltaUpdateHandledWithoutOverhead) {
  CheckpointingSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  // Expecting 1 additional "pseudo-node" for the zero-event logged.
  EXPECT_EQ(tree.nodeCount(), before + 1);

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

// ---------------------------------------------------------------------------
// Forced Checkpoint Crossing (K=500 Simulation)
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, HeavyUpdatesCrossCheckpoints) {
  CheckpointingSegmentTree tree({0, 0, 0});

  // The internal K interval is 500. Let's do 505 updates.
  for (int i = 0; i < 505; ++i) {
    tree.rangeAdd(0, 2, 1);
  }

  EXPECT_EQ(tree.versionCount(), 506u);

  // Test v0 (Before all updates, uses Checkpoint 0)
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 0);

  // Test v250 (Uses Checkpoint 0 + replays 250 events)
  EXPECT_EQ(tree.rangeSum(250, 0, 2), 250 * 3);

  // Test v500 (Exact checkpoint hit, no replay needed)
  EXPECT_EQ(tree.rangeSum(500, 0, 2), 500 * 3);

  // Test v502 (Uses Checkpoint 500 + replays 2 events)
  EXPECT_EQ(tree.rangeSum(502, 0, 2), 502 * 3);

  // Test v505 (Latest, uses fast path)
  EXPECT_EQ(tree.rangeSum(505, 0, 2), 505 * 3);
}

// ---------------------------------------------------------------------------
// Failed operation validation
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, FailedUpdateLeavesLogIntact) {
  CheckpointingSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 5, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}
