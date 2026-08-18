#include <valseg/checkpointing_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using valseg::CheckpointingSegmentTree;

// Deterministic suite for the checkpointing segment tree.
// checkpointCount() and nodeCount() are the structural evidence: a checkpoint
// (a full 2n - 1 node copy) must appear at version 0 and at exactly the
// versions that are multiples of K, zero-delta versions included, and every
// update must add exactly one log entry. Historical results must be identical
// whether they come from the ephemeral tree, a checkpoint, or a checkpoint
// plus replay, so several tests use a small K to force replay across
// checkpoint boundaries.

namespace {

// Independent oracle: the array of every published version, computed by
// plain element-wise addition.
class VersionedArrayOracle {
public:
  explicit VersionedArrayOracle(std::vector<long long> initial) {
    versions.push_back(std::move(initial));
  }

  void rangeAdd(std::size_t left, std::size_t right, long long value) {
    std::vector<long long> next = versions.back();
    for (std::size_t i = left; i <= right; ++i) {
      next[i] += value;
    }
    versions.push_back(std::move(next));
  }

  long long rangeSum(std::size_t version, std::size_t left, std::size_t right) const {
    long long sum = 0;
    for (std::size_t i = left; i <= right; ++i) {
      sum += versions[version][i];
    }
    return sum;
  }

private:
  std::vector<std::vector<long long>> versions;
};

// Nodes retained after U updates with checkpoint interval K over n elements:
// the ephemeral tree, the version-0 checkpoint and one checkpoint per K
// versions, 2n - 1 nodes each, plus one log entry per update.
std::size_t expectedNodeCount(std::size_t n, std::size_t updates, std::size_t interval) {
  return (updates / interval + 2) * (2 * n - 1) + updates;
}

} // namespace

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, InitializationPublishesVersionZero) {
  CheckpointingSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.checkpointCount(), 1u); // version 0 is always checkpointed
  EXPECT_EQ(tree.nodeCount(), 18u);      // 2n − 1 = 9 nodes, ephemeral tree + checkpoint
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(CheckpointingSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  CheckpointingSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.checkpointCount(), 1u); // version 0 exists but holds no nodes
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(CheckpointingSegmentTreeTest, ReinitializationReplacesAllState) {
  CheckpointingSegmentTree tree({1, 2, 3}, 1);
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.checkpointCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), 6u); // 3 nodes, ephemeral tree + checkpoint, empty log
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone

  tree.rangeAdd(0, 1, 1);
  EXPECT_EQ(tree.checkpointCount(), 1u); // the interval was replaced by the default
}

TEST(CheckpointingSegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  CheckpointingSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.checkpointCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(CheckpointingSegmentTreeTest, ZeroCheckpointIntervalIsRejected) {
  EXPECT_THROW(CheckpointingSegmentTree({1, 2, 3}, 0), std::invalid_argument);

  CheckpointingSegmentTree tree({1, 2, 3}, 2);
  tree.rangeAdd(0, 2, 10);

  EXPECT_THROW(tree.initialize({4, 5}, 0), std::invalid_argument);

  // The failed initialization left the tree, including its interval, unchanged.
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.size(), 3u);
  EXPECT_EQ(tree.rangeSum(1, 0, 2), 36);
  tree.rangeAdd(0, 2, 1);
  EXPECT_EQ(tree.checkpointCount(), 2u); // version 2 is checkpointed with K = 2
}

TEST(CheckpointingSegmentTreeTest, SingleElementTree) {
  CheckpointingSegmentTree tree({7}, 1);

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.checkpointCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), 2u); // one node, ephemeral tree + checkpoint
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.checkpointCount(), 2u); // K = 1: version 1 is checkpointed
  EXPECT_EQ(tree.nodeCount(), 4u);       // 3 single-node trees + 1 log entry
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);  // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
}

// ---------------------------------------------------------------------------
// Checkpoint schedule and retained-space evidence
// ---------------------------------------------------------------------------

// For perfect and uneven n and K in {1, 4, 500}: after every update the
// checkpoint count grows exactly when the new version is a multiple of K
// (every fourth update is a zero delta, so at K = 4 every checkpoint version
// is a zero-delta one), nodeCount() equals the closed formula, and every
// version reads exactly like the oracle from either the ephemeral tree, a
// checkpoint, or a checkpoint plus replay.
TEST(CheckpointingSegmentTreeTest, CheckpointScheduleAndNodeCountMatchFormula) {
  const std::size_t updates = 12;

  for (std::size_t n : {1u, 2u, 3u, 5u, 7u, 16u, 17u}) {
    std::vector<long long> initial(n);
    for (std::size_t i = 0; i < n; ++i) {
      initial[i] = static_cast<long long>(i) - 3;
    }

    for (std::size_t interval : {1u, 4u, 500u}) {
      CheckpointingSegmentTree tree(initial, interval);
      VersionedArrayOracle oracle(initial);

      EXPECT_EQ(tree.checkpointCount(), 1u);
      EXPECT_EQ(tree.nodeCount(), expectedNodeCount(n, 0, interval)) << "n=" << n;

      for (std::size_t u = 1; u <= updates; ++u) {
        const std::size_t left = (u * 3) % n;
        const std::size_t right = left + (u * 5) % (n - left);
        const long long delta = (u % 4 == 0) ? 0 : static_cast<long long>(u) * 7 - 20;
        const std::size_t checkpointsBefore = tree.checkpointCount();

        ASSERT_EQ(tree.rangeAdd(left, right, delta), u);
        oracle.rangeAdd(left, right, delta);

        const std::size_t expectedGrowth = (u % interval == 0) ? 1 : 0;
        EXPECT_EQ(tree.checkpointCount(), checkpointsBefore + expectedGrowth)
            << "n=" << n << " K=" << interval << " version=" << u;
        EXPECT_EQ(tree.nodeCount(), expectedNodeCount(n, u, interval))
            << "n=" << n << " K=" << interval << " version=" << u;
      }

      EXPECT_EQ(tree.versionCount(), updates + 1);
      EXPECT_EQ(tree.checkpointCount(), updates / interval + 1);

      for (std::size_t version = 0; version <= updates; ++version) {
        for (std::size_t left = 0; left < n; ++left) {
          for (std::size_t right = left; right < n; ++right) {
            EXPECT_EQ(tree.rangeSum(version, left, right), oracle.rangeSum(version, left, right))
                << "n=" << n << " K=" << interval << " version=" << version << " range=[" << left
                << ", " << right << "]";
          }
        }
      }
    }
  }
}

// Regression: a zero-delta update landing on a multiple of K must still take
// the checkpoint, otherwise replay is no longer bounded by K.
TEST(CheckpointingSegmentTreeTest, ZeroDeltaAtCheckpointBoundaryStillCheckpoints) {
  CheckpointingSegmentTree tree({1, 2, 3, 4}, 4);

  tree.rangeAdd(0, 3, 1);                  // v1
  tree.rangeAdd(1, 2, 2);                  // v2
  tree.rangeAdd(2, 3, 3);                  // v3
  EXPECT_EQ(tree.checkpointCount(), 1u);   // only version 0 so far
  std::size_t v4 = tree.rangeAdd(0, 0, 0); // v4, zero delta on the boundary
  EXPECT_EQ(v4, 4u);
  EXPECT_EQ(tree.checkpointCount(), 2u);     // the checkpoint was taken
  EXPECT_EQ(tree.nodeCount(), 3u * 7u + 4u); // 3 trees of 7 nodes + 4 log entries

  std::size_t v5 = tree.rangeAdd(0, 1, 10); // inside the next window
  std::size_t v6 = tree.rangeAdd(3, 3, -5);

  // v4 elements: 2, 5, 9, 8; v5: 12, 15, 9, 8; v6: 12, 15, 9, 3
  EXPECT_EQ(tree.rangeSum(v4, 0, 3), 24);
  EXPECT_EQ(tree.rangeSum(v5, 0, 3), 44);
  EXPECT_EQ(tree.rangeSum(v5, 1, 2), 24);
  EXPECT_EQ(tree.rangeSum(v6, 0, 3), 39);
  EXPECT_EQ(tree.rangeSum(v6, 3, 3), 3);
  EXPECT_EQ(tree.rangeSum(3, 0, 3), 24); // before the boundary: version 0 + replay
  EXPECT_EQ(tree.checkpointCount(), 2u);
}

TEST(CheckpointingSegmentTreeTest, IntervalAboveUpdateCountKeepsOnlyVersionZero) {
  CheckpointingSegmentTree tree({1, 2, 3, 4}, std::numeric_limits<std::size_t>::max());

  for (int i = 0; i < 20; ++i) {
    tree.rangeAdd(1, 2, 1);
  }

  EXPECT_EQ(tree.checkpointCount(), 1u); // a pure log
  EXPECT_EQ(tree.nodeCount(), 2u * 7u + 20u);
  for (std::size_t version = 0; version <= 20; ++version) {
    EXPECT_EQ(tree.rangeSum(version, 0, 3), 10 + 2 * static_cast<long long>(version));
    EXPECT_EQ(tree.rangeSum(version, 1, 1), 2 + static_cast<long long>(version));
  }
}

TEST(CheckpointingSegmentTreeTest, DefaultIntervalCheckpointsEveryFiveHundredVersions) {
  CheckpointingSegmentTree tree({0, 0, 0});

  for (int i = 0; i < 499; ++i) {
    tree.rangeAdd(0, 2, 1);
  }
  EXPECT_EQ(tree.checkpointCount(), 1u);

  tree.rangeAdd(0, 2, 1); // v500
  EXPECT_EQ(tree.checkpointCount(), 2u);

  for (int i = 0; i < 5; ++i) {
    tree.rangeAdd(0, 2, 1);
  }
  EXPECT_EQ(tree.versionCount(), 506u);
  EXPECT_EQ(tree.checkpointCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u * 5u + 505u);

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 0);         // checkpoint 0
  EXPECT_EQ(tree.rangeSum(250, 0, 2), 250 * 3); // checkpoint 0 + 250 replayed entries
  EXPECT_EQ(tree.rangeSum(499, 0, 2), 499 * 3); // longest replay in the window
  EXPECT_EQ(tree.rangeSum(500, 0, 2), 500 * 3); // checkpoint 500, no replay
  EXPECT_EQ(tree.rangeSum(502, 0, 2), 502 * 3); // checkpoint 500 + 2 replayed entries
  EXPECT_EQ(tree.rangeSum(505, 0, 2), 505 * 3); // latest, ephemeral tree
}

// ---------------------------------------------------------------------------
// Range updates and replay
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  CheckpointingSegmentTree tree({1, 2, 3, 4, 5}, 2);

  std::size_t v1 = tree.rangeAdd(1, 3, 10);
  std::size_t v2 = tree.rangeAdd(2, 4, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 1, 3), 9);

  EXPECT_EQ(tree.rangeSum(v1, 0, 4), 45); // checkpoint 0 + 1 replayed entry
  EXPECT_EQ(tree.rangeSum(v1, 1, 3), 39);
  EXPECT_EQ(tree.rangeSum(v1, 2, 4), 32); // 13 + 14 + 5
  EXPECT_EQ(tree.rangeSum(v1, 4, 4), 5);  // element 4 untouched in v1

  EXPECT_EQ(tree.rangeSum(v2, 0, 4), 60); // latest, ephemeral tree
  EXPECT_EQ(tree.rangeSum(v2, 2, 4), 47); // 18 + 19 + 10
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 12); // only v1 touched element 1

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15); // version 0 still isolated
}

TEST(CheckpointingSegmentTreeTest, MultipleUpdatesAccumulate) {
  CheckpointingSegmentTree tree({1, 2, 3, 4});

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

TEST(CheckpointingSegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  CheckpointingSegmentTree tree({0, 0, 0, 0}, 3);

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 1);
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.checkpointCount(), 2u); // versions 0 and 3
  EXPECT_EQ(tree.nodeCount(), 3u * 7u + 3u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// ---------------------------------------------------------------------------
// Zero-delta updates
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutTreeNodes) {
  CheckpointingSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.checkpointCount(), 1u);
  EXPECT_EQ(tree.nodeCount(), before + 1); // one log entry, no tree nodes

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

TEST(CheckpointingSegmentTreeTest, MultipleZeroDeltaUpdatesStackCorrectly) {
  CheckpointingSegmentTree tree({1, 2, 3});
  const std::size_t before = tree.nodeCount();

  std::size_t v1 = tree.rangeAdd(0, 2, 0);
  std::size_t v2 = tree.rangeAdd(1, 1, 0);
  std::size_t v3 = tree.rangeAdd(0, 0, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), before + 3);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 2);
}

TEST(CheckpointingSegmentTreeTest, ZeroDeltaUpdateStillValidatesRange) {
  CheckpointingSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 0), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 0), std::out_of_range);
  EXPECT_EQ(tree.versionCount(), 1u);
}

TEST(CheckpointingSegmentTreeTest, UpdateAfterZeroDeltaLeavesEarlierVersionsIntact) {
  CheckpointingSegmentTree tree({1, 2, 3});

  std::size_t v1 = tree.rangeAdd(0, 2, 0);
  std::size_t v2 = tree.rangeAdd(0, 2, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 36);
}

// ---------------------------------------------------------------------------
// Negative values and deltas
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, NegativeInitialValues) {
  CheckpointingSegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(CheckpointingSegmentTreeTest, NegativeDelta) {
  CheckpointingSegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(CheckpointingSegmentTreeTest, NegativeDeltaPartialRange) {
  CheckpointingSegmentTree tree({10, 10, 10, 10}, 1);

  std::size_t v1 = tree.rangeAdd(1, 2, -4);
  tree.rangeAdd(0, 0, 1); // v2, so v1 is read from its own checkpoint

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2]. v3: 0 to [0,3], so v2 is replayed.
TEST(CheckpointingSegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  CheckpointingSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  std::size_t v2 = tree.rangeAdd(1, 2, -3);
  tree.rangeAdd(0, 3, 0);

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

TEST(CheckpointingSegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  CheckpointingSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8}, 3);

  for (int i = 0; i < 10; ++i) {
    tree.rangeAdd(0, 7, 10);
  }

  EXPECT_EQ(tree.versionCount(), 11u);
  EXPECT_EQ(tree.checkpointCount(), 4u); // versions 0, 3, 6, 9
  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(0, 3, 5), 15); // 4 + 5 + 6
  EXPECT_EQ(tree.rangeSum(0, 7, 7), 8);
}

// Array {0}, ten updates of +1 with K = 3: version k must read k for every k,
// whether it is a checkpoint (0, 3, 6, 9), replayed (1, 2, 4, 5, 7, 8) or the
// latest (10).
TEST(CheckpointingSegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  CheckpointingSegmentTree tree({0}, 3);
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

TEST(CheckpointingSegmentTreeTest, InterleavedPartialUpdateIsolation) {
  CheckpointingSegmentTree tree({1, 1, 1, 1}, 2);

  std::size_t v1 = tree.rangeAdd(0, 1, 10); // left half only
  std::size_t v2 = tree.rangeAdd(2, 3, 20); // right half only, checkpointed
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

TEST(CheckpointingSegmentTreeTest, FailedUpdatePublishesNothing) {
  CheckpointingSegmentTree tree({1, 2, 3}, 1);

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();
  std::size_t checkpointsBefore = tree.checkpointCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.checkpointCount(), checkpointsBefore); // K = 1: no checkpoint either
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
}

TEST(CheckpointingSegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  CheckpointingSegmentTree tree({1, 2, 3, 4});

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

TEST(CheckpointingSegmentTreeTest, DefaultTreeHasNoVersions) {
  CheckpointingSegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_EQ(tree.checkpointCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(CheckpointingSegmentTreeTest, InvalidVersionQueryThrows) {
  CheckpointingSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(CheckpointingSegmentTreeTest, InvalidRangeOnUpdateThrows) {
  CheckpointingSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(CheckpointingSegmentTreeTest, InvalidRangeOnQueryThrows) {
  CheckpointingSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ---------------------------------------------------------------------------
// Large 64-bit values
// ---------------------------------------------------------------------------

TEST(CheckpointingSegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;
  CheckpointingSegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);
  tree.rangeAdd(0, 0, 0); // v2, so v1 is replayed from checkpoint 0

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(CheckpointingSegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;
  CheckpointingSegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}
