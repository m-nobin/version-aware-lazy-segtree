#include <valseg/buffered_path_copying_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

using valseg::BufferedPathCopyingSegmentTree;

// Deterministic suite for the buffered path-copying segment tree.
// nodeCount() is the structural evidence: a visited node with a free buffer
// slot absorbs an update in place (0 nodes), a visited node with a full
// two-entry buffer is copied together with every visited ancestor, and every
// version published before a copy must keep reading its own values.

namespace {

// Independent model of the copy rule over the segment tree on [0, n − 1]:
// a visited node is copied when its buffer is full or one of its children was
// copied; a copy starts with an empty buffer; every other visited node takes
// one buffer slot. Nodes are addressed heap-style (root 1, children 2i, 2i+1).
class CopyModel {
public:
  explicit CopyModel(std::size_t n) : arraySize(n), fill(4 * n + 4, 0) {}

  // Returns the number of nodes the update on [left, right] must copy;
  // visited() then reports how many nodes that update touched at all.
  std::size_t update(std::size_t left, std::size_t right) {
    copies = 0;
    visitedNodes = 0;
    visit(1, 0, arraySize - 1, left, right);
    return copies;
  }

  std::size_t visited() const {
    return visitedNodes;
  }

private:
  bool visit(std::size_t id, std::size_t segmentLeft, std::size_t segmentRight,
             std::size_t queryLeft, std::size_t queryRight) {
    ++visitedNodes;
    bool childCopied = false;
    if (!(queryLeft <= segmentLeft && segmentRight <= queryRight)) {
      const std::size_t middle = (segmentLeft + segmentRight) / 2;
      if (queryLeft <= middle) {
        childCopied = visit(2 * id, segmentLeft, middle, queryLeft, queryRight) || childCopied;
      }
      if (queryRight > middle) {
        childCopied =
            visit(2 * id + 1, middle + 1, segmentRight, queryLeft, queryRight) || childCopied;
      }
    }
    if (childCopied || fill[id] == 2) {
      fill[id] = 0;
      ++copies;
      return true;
    }
    ++fill[id];
    return false;
  }

  std::size_t arraySize;
  std::vector<std::size_t> fill;
  std::size_t copies = 0;
  std::size_t visitedNodes = 0;
};

// Depth of the leaf holding index in the tree on [0, n − 1]: the number of
// edges from the root, so its root-to-leaf path has depth + 1 nodes.
std::size_t leafDepth(std::size_t n, std::size_t index) {
  std::size_t depth = 0;
  std::size_t segmentLeft = 0;
  std::size_t segmentRight = n - 1;
  while (segmentLeft != segmentRight) {
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    if (index <= middle) {
      segmentRight = middle;
    } else {
      segmentLeft = middle + 1;
    }
    ++depth;
  }
  return depth;
}

} // namespace

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, InitializationPublishesVersionZero) {
  BufferedPathCopyingSegmentTree tree({10, 20, 30, 40, 50});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.nodeCount(), 9u); // 2n − 1 nodes for n leaves
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 150);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 30);
}

TEST(BufferedPathCopyingSegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  BufferedPathCopyingSegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(BufferedPathCopyingSegmentTreeTest, ReinitializationReplacesAllState) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);
  EXPECT_EQ(tree.nodeCount(), 3u);
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range); // old version 1 is gone
}

TEST(BufferedPathCopyingSegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

// n = 1: the single leaf buffers two updates in place and is copied by the
// third, so the arena stays at one node for two updates and grows to two.
TEST(BufferedPathCopyingSegmentTreeTest, SingleElementTree) {
  BufferedPathCopyingSegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);
  std::size_t v2 = tree.rangeAdd(0, 0, 3);
  std::size_t v3 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), 2u);      // buffered, buffered, copied
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v2, 0, 0), 13);
  EXPECT_EQ(tree.rangeSum(v3, 0, 0), 16);
}

// ---------------------------------------------------------------------------
// Buffering and copy evidence
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, UpdatesWithinBufferCapacityAppendNoNodes) {
  BufferedPathCopyingSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // A full-range update visits only the root; its two buffer slots absorb
  // two updates without allocating.
  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);

  std::size_t v2 = tree.rangeAdd(0, 3, 5);
  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 60); // 40 + 5×4
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 30); // the lazy tag reaches every element
}

TEST(BufferedPathCopyingSegmentTreeTest, FullBufferCopiesOnlyTheOverflowingNode) {
  BufferedPathCopyingSegmentTree tree({0, 0, 0, 0});

  tree.rangeAdd(0, 3, 1); // v1: root buffer slot 0
  tree.rangeAdd(0, 3, 1); // v2: root buffer slot 1, buffer full
  EXPECT_EQ(tree.nodeCount(), 7u);

  // v3 overflows the root's buffer: the root is copied with both entries
  // flushed into its base fields; it has no ancestor, so exactly one node.
  std::size_t v3 = tree.rangeAdd(0, 3, 1);
  EXPECT_EQ(tree.nodeCount(), 8u);

  // Two more updates fit in the copy's empty buffer, the third copies again.
  tree.rangeAdd(0, 3, 1); // v4
  tree.rangeAdd(0, 3, 1); // v5
  EXPECT_EQ(tree.nodeCount(), 8u);
  std::size_t v6 = tree.rangeAdd(0, 3, 1);
  EXPECT_EQ(tree.nodeCount(), 9u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
  EXPECT_EQ(tree.rangeSum(4, 0, 3), 16);
  EXPECT_EQ(tree.rangeSum(5, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v6, 0, 3), 24);
}

TEST(BufferedPathCopyingSegmentTreeTest, LeafOverflowCopiesThePathToTheRoot) {
  BufferedPathCopyingSegmentTree tree({10, 20, 30, 40});

  EXPECT_EQ(tree.nodeCount(), 7u);

  // Point updates on index 0 visit leaf [0], its parent [0, 1] and the root
  // [0, 3]; the first two fit into every buffer on that path.
  tree.rangeAdd(0, 0, 1); // v1
  tree.rangeAdd(0, 0, 1); // v2
  EXPECT_EQ(tree.nodeCount(), 7u);

  // v3 overflows the leaf's buffer. The leaf is copied; the new child index
  // cannot be buffered, so [0, 1] is copied, and so is the root: 3 nodes.
  std::size_t v3 = tree.rangeAdd(0, 0, 1);
  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + 3

  EXPECT_EQ(tree.rangeSum(0, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(1, 0, 0), 11);
  EXPECT_EQ(tree.rangeSum(2, 0, 0), 12);
  EXPECT_EQ(tree.rangeSum(v3, 0, 0), 13);
  EXPECT_EQ(tree.rangeSum(v3, 1, 1), 20); // sibling leaf shared and unchanged
  EXPECT_EQ(tree.rangeSum(v3, 2, 3), 70); // untouched shared subtree [2, 3]
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 103);
}

// A copy forced by a copied child must carry the node's own lazy tag along,
// whether the tag sits in the base fields or in a buffered entry. [0, 1]
// holds lazy +10 in its base (flushed by its own overflow at v3) and lazy +5
// in a buffered entry when leaf 0 overflows at v5 and forces its copy; the
// untouched leaf 1 must still read both tags through that copy.
TEST(BufferedPathCopyingSegmentTreeTest, ChildForcedCopyKeepsTheParentsLazyTag) {
  BufferedPathCopyingSegmentTree tree({0, 0, 0, 0});

  tree.rangeAdd(0, 0, 1); // v1: leaf 0, [0, 1] and root take slot 0
  tree.rangeAdd(0, 0, 1); // v2: slot 1 everywhere on the path
  EXPECT_EQ(tree.nodeCount(), 7u);

  std::size_t v3 = tree.rangeAdd(0, 1, 10); // [0, 1] overflows: it and the root are copied
  EXPECT_EQ(tree.nodeCount(), 9u);
  std::size_t v4 = tree.rangeAdd(0, 1, 5); // lazy +5 buffered on the fresh [0, 1]
  EXPECT_EQ(tree.nodeCount(), 9u);

  std::size_t v5 = tree.rangeAdd(0, 0, 1); // leaf 0 overflows: leaf, [0, 1], root copied
  EXPECT_EQ(tree.nodeCount(), 12u);

  EXPECT_EQ(tree.rangeSum(v5, 0, 0), 18); // 3 + 10 + 5
  EXPECT_EQ(tree.rangeSum(v5, 1, 1), 15); // both lazy tags survive the forced copy
  EXPECT_EQ(tree.rangeSum(v5, 0, 1), 33);
  EXPECT_EQ(tree.rangeSum(v5, 0, 3), 33);
  EXPECT_EQ(tree.rangeSum(v4, 1, 1), 15);
  EXPECT_EQ(tree.rangeSum(v3, 1, 1), 10);
  EXPECT_EQ(tree.rangeSum(v3, 0, 0), 12);
  EXPECT_EQ(tree.rangeSum(2, 0, 1), 2);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
}

// The path-copy period, over perfect and uneven trees: repeated point updates
// on one leaf of depth d append 0, 0, d + 1, 0, 0, d + 1, ... nodes (the
// buffered pair, then the copy of the whole root-to-leaf path), and after each
// round every published version must still read its own cumulative value.
TEST(BufferedPathCopyingSegmentTreeTest, RepeatedPointUpdatesCopyThePathEveryThirdTime) {
  constexpr std::size_t sizes[] = {1, 2, 3, 5, 7, 16, 17};
  for (std::size_t n : sizes) {
    for (std::size_t index : {static_cast<std::size_t>(0), n / 2, n - 1}) {
      BufferedPathCopyingSegmentTree tree(std::vector<long long>(n, 0));
      const std::size_t pathNodes = leafDepth(n, index) + 1;
      const std::string where = "n=" + std::to_string(n) + " index=" + std::to_string(index);

      for (std::size_t round = 0; round < 4; ++round) {
        for (std::size_t step = 1; step <= 3; ++step) {
          const std::size_t before = tree.nodeCount();
          tree.rangeAdd(index, index, 1);
          const std::size_t appended = tree.nodeCount() - before;
          EXPECT_EQ(appended, step == 3 ? pathNodes : 0u) << where << " round=" << round;
        }
      }

      // Version k reads k at the updated index and 0 elsewhere.
      for (std::size_t version = 0; version < tree.versionCount(); ++version) {
        EXPECT_EQ(tree.rangeSum(version, index, index), static_cast<long long>(version)) << where;
        EXPECT_EQ(tree.rangeSum(version, 0, n - 1), static_cast<long long>(version)) << where;
      }
    }
  }
}

// The quantitative evidence for arbitrary ranges, over perfect and uneven
// trees: for a fixed sequence of range updates the arena must grow by exactly
// the number of copies predicted by CopyModel (a visited node is copied when
// its buffer is full or a child was copied), never by more than the visited
// nodes; and every intermediate version must still read its own totals.
TEST(BufferedPathCopyingSegmentTreeTest, NodeCountMatchesCopyModelOverRanges) {
  constexpr std::size_t sizes[] = {1, 2, 3, 5, 7, 16, 17};
  for (std::size_t n : sizes) {
    BufferedPathCopyingSegmentTree tree(std::vector<long long>(n, 1));
    CopyModel model(n);
    std::vector<long long> totals{static_cast<long long>(n)};

    EXPECT_EQ(tree.nodeCount(), 2 * n - 1) << "n=" << n;

    // Deterministic mix of point, prefix, suffix and interior ranges; every
    // (left, right) pair is visited so the small trees exercise every node.
    for (std::size_t round = 0; round < 3; ++round) {
      for (std::size_t left = 0; left < n; ++left) {
        for (std::size_t right = left; right < n; ++right) {
          const std::size_t before = tree.nodeCount();
          tree.rangeAdd(left, right, 1);
          const std::size_t appended = tree.nodeCount() - before;
          const std::size_t expected = model.update(left, right);
          const std::string where = "n=" + std::to_string(n) + " round=" + std::to_string(round) +
                                    " range [" + std::to_string(left) + ", " +
                                    std::to_string(right) + "]";
          EXPECT_EQ(appended, expected) << where;
          EXPECT_LE(appended, model.visited()) << where;
          totals.push_back(totals.back() + static_cast<long long>(right - left + 1));
        }
      }
    }

    for (std::size_t version = 0; version < totals.size(); ++version) {
      EXPECT_EQ(tree.rangeSum(version, 0, n - 1), totals[version])
          << "n=" << n << " version=" << version;
    }
  }
}

// ---------------------------------------------------------------------------
// Cumulative updates
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4, 5});

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

TEST(BufferedPathCopyingSegmentTreeTest, MultipleUpdatesAccumulate) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4});

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

// Ten full-range updates: the root alone is visited, so it is copied on
// updates 3, 6 and 9 and the arena grows by exactly 3 nodes.
TEST(BufferedPathCopyingSegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  BufferedPathCopyingSegmentTree tree({0, 0, 0, 0});
  std::vector<std::size_t> versions;
  versions.reserve(10);

  for (int i = 0; i < 10; ++i) {
    versions.push_back(tree.rangeAdd(0, 3, 1));
  }

  EXPECT_EQ(tree.versionCount(), 11u);
  EXPECT_EQ(tree.nodeCount(), 10u); // 7 + 3 root copies

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  for (std::size_t i = 0; i < versions.size(); ++i) {
    EXPECT_EQ(tree.rangeSum(versions[i], 0, 3), 4 * (static_cast<long long>(i) + 1));
    EXPECT_EQ(tree.rangeSum(versions[i], 2, 2), static_cast<long long>(i) + 1);
  }
}

// ---------------------------------------------------------------------------
// Zero-delta updates
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no nodes allocated

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
}

TEST(BufferedPathCopyingSegmentTreeTest, MultipleZeroDeltaUpdatesStackCorrectly) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});
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

TEST(BufferedPathCopyingSegmentTreeTest, ZeroDeltaUpdateStillValidatesRange) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 0), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 0), std::out_of_range);
  EXPECT_EQ(tree.versionCount(), 1u);
}

// A zero-delta version shares the latest root; the buffered entry a later
// update appends in place is tagged with the later version, so the shared
// version keeps reading the old value.
TEST(BufferedPathCopyingSegmentTreeTest, UpdateAfterZeroDeltaLeavesSharedVersionIntact) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});

  std::size_t v1 = tree.rangeAdd(0, 2, 0); // shares version 0's root
  std::size_t v2 = tree.rangeAdd(0, 2, 10);
  std::size_t v3 = tree.rangeAdd(1, 1, 0); // shares version 2's root
  std::size_t v4 = tree.rangeAdd(0, 2, 10);

  EXPECT_EQ(tree.nodeCount(), 5u); // both updates buffered into the root
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 36);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 36);
  EXPECT_EQ(tree.rangeSum(v4, 0, 2), 66);
}

// ---------------------------------------------------------------------------
// Negative values and deltas
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, NegativeInitialValues) {
  BufferedPathCopyingSegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(BufferedPathCopyingSegmentTreeTest, NegativeDelta) {
  BufferedPathCopyingSegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(BufferedPathCopyingSegmentTreeTest, NegativeDeltaPartialRange) {
  BufferedPathCopyingSegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2]. v3: −7 to [0,3].
TEST(BufferedPathCopyingSegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  BufferedPathCopyingSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 10);
  std::size_t v2 = tree.rangeAdd(1, 2, -3);
  std::size_t v3 = tree.rangeAdd(0, 3, -7);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34); // 40 − 3×2
  EXPECT_EQ(tree.rangeSum(v2, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 14); // (10−3)×2
  EXPECT_EQ(tree.rangeSum(v2, 3, 3), 10);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 6); // 34 − 7×4
  EXPECT_EQ(tree.rangeSum(v3, 1, 2), 0); // (10−3−7)×2
  EXPECT_EQ(tree.rangeSum(v3, 3, 3), 3);
}

// ---------------------------------------------------------------------------
// Historical isolation
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

  for (int i = 0; i < 10; ++i) {
    tree.rangeAdd(0, 7, 10);
  }

  EXPECT_EQ(tree.versionCount(), 11u);
  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(0, 3, 5), 15); // 4 + 5 + 6
  EXPECT_EQ(tree.rangeSum(0, 7, 7), 8);
}

// Array {0}, ten updates of +1: version k must read k for every k, across
// three buffer overflows of the single leaf.
TEST(BufferedPathCopyingSegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  BufferedPathCopyingSegmentTree tree({0});
  std::vector<std::size_t> versions;
  versions.reserve(10);

  for (int i = 0; i < 10; ++i) {
    versions.push_back(tree.rangeAdd(0, 0, 1));
  }

  EXPECT_EQ(tree.nodeCount(), 4u); // copies on updates 3, 6 and 9
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 0);
  for (std::size_t i = 0; i < versions.size(); ++i) {
    EXPECT_EQ(tree.rangeSum(versions[i], 0, 0), static_cast<long long>(i) + 1);
  }
}

TEST(BufferedPathCopyingSegmentTreeTest, InterleavedPartialUpdateIsolation) {
  BufferedPathCopyingSegmentTree tree({1, 1, 1, 1});

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

TEST(BufferedPathCopyingSegmentTreeTest, FailedUpdatePublishesNothing) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 5); // one buffered entry in the root

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(1, 0, 2), 21);
  EXPECT_EQ(tree.rangeAdd(0, 2, 5), 2u); // the next update still buffers normally
  EXPECT_EQ(tree.nodeCount(), nodesBefore);
  EXPECT_EQ(tree.rangeSum(2, 0, 2), 36);
}

TEST(BufferedPathCopyingSegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4});

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

TEST(BufferedPathCopyingSegmentTreeTest, DefaultTreeHasNoVersions) {
  BufferedPathCopyingSegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(BufferedPathCopyingSegmentTreeTest, InvalidVersionQueryThrows) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

TEST(BufferedPathCopyingSegmentTreeTest, InvalidRangeOnUpdateThrows) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(BufferedPathCopyingSegmentTreeTest, InvalidRangeOnQueryThrows) {
  BufferedPathCopyingSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ---------------------------------------------------------------------------
// Large 64-bit values
// ---------------------------------------------------------------------------

TEST(BufferedPathCopyingSegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;
  BufferedPathCopyingSegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(BufferedPathCopyingSegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;
  BufferedPathCopyingSegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}
