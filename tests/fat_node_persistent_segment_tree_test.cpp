#include <valseg/fat_node_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using valseg::FatNodePersistentSegmentTree;

// Deterministic suite for the fat-node persistent segment tree.
// nodeCount() is the structural evidence: an update appends states to the
// nodes it visits in place and allocates a fresh node only for a visited
// node whose HISTORY_CAPACITY (= 3) state slots are already full, so the
// arena grows by exactly the number of overflowing visited nodes, and never
// by more than the 4(h + 1) nodes an update can visit, however many
// versions exist.

namespace {

constexpr std::size_t kCapacity = FatNodePersistentSegmentTree::HISTORY_CAPACITY;

std::size_t floorLog2(std::size_t n) {
  std::size_t result = 0;
  while ((static_cast<std::size_t>(1) << (result + 1)) <= n) {
    ++result;
  }
  return result;
}

// Smallest h with 2^h >= n: the tree height.
std::size_t ceilLog2(std::size_t n) {
  return n <= 1 ? 0 : floorLog2(n - 1) + 1;
}

// Independent model of the fat-node bookkeeping. Logical nodes are
// identified by their segment (the tree shape never changes); the model
// records how many modifications each has received and reports, for one
// update over [queryLeft, queryRight], how many visited nodes overflow —
// i.e. how many fresh arena nodes the update must append. A node's first
// slot holds its creation state, so its m-th modification overflows exactly
// when m is a multiple of the capacity.
class FatNodeModel {
public:
  explicit FatNodeModel(std::size_t n) : size(n) {}

  std::size_t applyUpdate(std::size_t queryLeft, std::size_t queryRight) {
    visitedInLastUpdate = 0;
    return visit(0, size - 1, queryLeft, queryRight);
  }

  std::size_t visited() const {
    return visitedInLastUpdate;
  }

private:
  std::size_t visit(std::size_t segmentLeft, std::size_t segmentRight, std::size_t queryLeft,
                    std::size_t queryRight) {
    ++visitedInLastUpdate;
    const std::size_t modifications = ++modificationCount[{segmentLeft, segmentRight}];
    std::size_t overflowed = (modifications % kCapacity == 0) ? 1 : 0;
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      return overflowed;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    if (queryLeft <= middle) {
      overflowed += visit(segmentLeft, middle, queryLeft, queryRight);
    }
    if (queryRight > middle) {
      overflowed += visit(middle + 1, segmentRight, queryLeft, queryRight);
    }
    return overflowed;
  }

  std::size_t size;
  std::size_t visitedInLastUpdate = 0;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> modificationCount;
};

} // namespace

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

TEST(FatNodePersistentSegmentTreeTest, ReinitializationToEmptyClearsAllState) {
  FatNodePersistentSegmentTree tree({1, 2, 3});
  tree.rangeAdd(0, 2, 10);

  tree.initialize({});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

TEST(FatNodePersistentSegmentTreeTest, SingleElementTree) {
  FatNodePersistentSegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 1u);      // the state is appended to the only node
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
}

// ---------------------------------------------------------------------------
// Fat-node allocation evidence
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, UpdateExceedingCapacityCopiesOnlyTheFullNode) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});

  EXPECT_EQ(tree.nodeCount(), 7u); // initial build has 7 nodes

  // The root's first slot holds its creation state, so two full-range
  // updates append in place before the capacity of 3 is exhausted.
  std::size_t v1 = tree.rangeAdd(0, 3, 1); // v1: root slot 1
  EXPECT_EQ(tree.nodeCount(), 7u);
  std::size_t v2 = tree.rangeAdd(0, 3, 1); // v2: root slot 2, root full
  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);

  // The third full-range update finds the root full: one fresh root is
  // appended, the six other nodes are untouched, and the old root keeps
  // serving versions 0–2.
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);
  EXPECT_EQ(tree.nodeCount(), 8u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);

  // Two more fit in the fresh root; the sixth overflows again.
  tree.rangeAdd(0, 3, 1);
  tree.rangeAdd(0, 3, 1);
  EXPECT_EQ(tree.nodeCount(), 8u);
  std::size_t v6 = tree.rangeAdd(0, 3, 1);
  EXPECT_EQ(tree.nodeCount(), 9u);
  EXPECT_EQ(tree.rangeSum(v6, 0, 3), 24);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

TEST(FatNodePersistentSegmentTreeTest, PartialRangeUpdateAppendsToVisitedNodesOnly) {
  FatNodePersistentSegmentTree tree({10, 20, 30, 40});

  // Updating [1, 2] visits the root, both halves, and leaves 1 and 2 (five
  // nodes), each of which takes the state in place: no allocation.
  std::size_t v1 = tree.rangeAdd(1, 2, 5);

  EXPECT_EQ(tree.nodeCount(), 7u);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), 50);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 60);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10);
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 40);

  // A second update on the same range fills those five nodes (creation
  // state plus two appends); the third overflows all five at once, and
  // nothing else.
  tree.rangeAdd(1, 2, 5);
  EXPECT_EQ(tree.nodeCount(), 7u);
  tree.rangeAdd(1, 2, 5);
  EXPECT_EQ(tree.nodeCount(), 12u);
  EXPECT_EQ(tree.rangeSum(3, 1, 2), 80);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 60);
}

// A fresh copy carries the full node's latest lazy tag: sub-range reads at
// versions routed through the copied root must still resolve the deferred
// deltas, and later partial updates must combine with them.
TEST(FatNodePersistentSegmentTreeTest, LazyTagSurvivesNodeCopy) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 2);
  std::size_t v3 = tree.rangeAdd(0, 3, 3); // overflows the root: lazy 6 lives in the copy

  EXPECT_EQ(tree.nodeCount(), 8u);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 2);
  EXPECT_EQ(tree.rangeSum(v2, 0, 1), 6);
  EXPECT_EQ(tree.rangeSum(v3, 1, 2), 12);
  EXPECT_EQ(tree.rangeSum(v3, 0, 0), 6);

  std::size_t v4 = tree.rangeAdd(1, 2, 1); // partial update below the copied root

  EXPECT_EQ(tree.rangeSum(v4, 1, 2), 14);
  EXPECT_EQ(tree.rangeSum(v4, 0, 3), 26);
  EXPECT_EQ(tree.rangeSum(v3, 1, 2), 12); // v3 unchanged
}

// One leaf path (root, [0, 1], leaf 0 for n = 4) modified 10 × capacity
// times: every version must still read correctly through the copies, and
// the arena must hold exactly 1 + floor(m / 3) nodes per logical node.
TEST(FatNodePersistentSegmentTreeTest, RepeatedlyModifiedNodesStayReachableAcrossCopies) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});
  constexpr std::size_t updates = 10 * kCapacity;

  for (std::size_t i = 0; i < updates; ++i) {
    tree.rangeAdd(0, 0, 1);
  }

  EXPECT_EQ(tree.versionCount(), updates + 1);
  EXPECT_EQ(tree.nodeCount(), 7u + 3 * (updates / kCapacity)); // 7 + 3 × 10

  for (std::size_t version = 0; version <= updates; ++version) {
    const auto expected = static_cast<long long>(version);
    EXPECT_EQ(tree.rangeSum(version, 0, 0), expected) << "version " << version;
    EXPECT_EQ(tree.rangeSum(version, 0, 3), expected) << "version " << version;
    EXPECT_EQ(tree.rangeSum(version, 1, 3), 0) << "version " << version;
  }
}

// Node copying must not cascade or chain: however many versions exist, one
// update appends exactly as many nodes as the independent model says
// overflow, and never more than the model's visited-node count (itself
// bounded by 4(h + 1); the tree's own visit count is not observable).
TEST(FatNodePersistentSegmentTreeTest, ArenaGrowthPerUpdateIsBoundedIndependentOfVersionCount) {
  constexpr std::size_t n = 64;
  const std::size_t height = ceilLog2(n);
  FatNodePersistentSegmentTree tree(std::vector<long long>(n, 0));
  FatNodeModel model(n);

  // 3 000 versions cycling through point, 33-wide, and suffix ranges.
  for (std::size_t i = 0; i < 3000; ++i) {
    const std::size_t left = i % n;
    const std::size_t right = (i % 3 == 0) ? left : (i % 3 == 1) ? (left + n / 2) % n : n - 1;
    const std::size_t low = left < right ? left : right;
    const std::size_t high = left < right ? right : left;

    const std::size_t before = tree.nodeCount();
    tree.rangeAdd(low, high, 1);
    const std::size_t appended = tree.nodeCount() - before;
    const std::size_t expected = model.applyUpdate(low, high);

    ASSERT_EQ(appended, expected) << "update " << i << " range [" << low << ", " << high << "]";
    ASSERT_LE(appended, model.visited()) << "update " << i;
    ASSERT_LE(model.visited(), 4 * (height + 1)) << "update " << i;
  }

  EXPECT_EQ(tree.rangeSum(0, 0, n - 1), 0); // version 0 isolated
}

// The quantitative evidence over perfect and uneven trees: for every range
// [left, right] the arena grows by exactly the number of visited nodes whose
// state slots are full (independently tracked by FatNodeModel), and the
// final sums are exact.
TEST(FatNodePersistentSegmentTreeTest, NodeCountGrowsByOverflowingVisitedNodesPerRange) {
  constexpr std::size_t sizes[] = {1, 2, 3, 5, 7, 16, 17};
  for (std::size_t n : sizes) {
    FatNodePersistentSegmentTree tree(std::vector<long long>(n, 1));
    FatNodeModel model(n);
    const std::size_t height = ceilLog2(n);

    EXPECT_EQ(tree.nodeCount(), 2 * n - 1) << "n=" << n;

    long long total = static_cast<long long>(n);
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        const std::size_t before = tree.nodeCount();
        tree.rangeAdd(left, right, 1);
        const std::size_t appended = tree.nodeCount() - before;
        const std::string where = "n=" + std::to_string(n) + " range [" + std::to_string(left) +
                                  ", " + std::to_string(right) + "]";

        EXPECT_EQ(appended, model.applyUpdate(left, right)) << where;
        EXPECT_LE(appended, model.visited()) << where;
        EXPECT_LE(model.visited(), 4 * (height + 1)) << where;
        total += static_cast<long long>(right - left + 1);
      }
    }

    EXPECT_EQ(tree.rangeSum(0, 0, n - 1), static_cast<long long>(n)) << "n=" << n; // v0 isolated
    EXPECT_EQ(tree.rangeSum(tree.versionCount() - 1, 0, n - 1), total) << "n=" << n;
  }
}

// ---------------------------------------------------------------------------
// Cumulative updates
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4, 5});

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

TEST(FatNodePersistentSegmentTreeTest, MultipleUpdatesAccumulate) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4});

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

TEST(FatNodePersistentSegmentTreeTest, MultipleZeroDeltaUpdatesStackCorrectly) {
  FatNodePersistentSegmentTree tree({1, 2, 3});
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

TEST(FatNodePersistentSegmentTreeTest, ZeroDeltaUpdateStillValidatesRange) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeAdd(2, 1, 0), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 0), std::out_of_range);
  EXPECT_EQ(tree.versionCount(), 1u);
}

// Zero-delta versions leave gaps in the version stamps of every node; the
// stamp search must still resolve every version, published or shared.
TEST(FatNodePersistentSegmentTreeTest, UpdateAfterZeroDeltaLeavesSharedVersionIntact) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  std::size_t v1 = tree.rangeAdd(0, 2, 0); // shares version 0's root
  std::size_t v2 = tree.rangeAdd(0, 2, 10);
  std::size_t v3 = tree.rangeAdd(1, 1, 0); // shares version 2's root
  std::size_t v4 = tree.rangeAdd(0, 2, 5);

  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 36);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 36);
  EXPECT_EQ(tree.rangeSum(v4, 0, 2), 51);
}

// ---------------------------------------------------------------------------
// Negative values and deltas
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, NegativeInitialValues) {
  FatNodePersistentSegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // −9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (−1+10) + (−4+10)
}

TEST(FatNodePersistentSegmentTreeTest, NegativeDelta) {
  FatNodePersistentSegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

TEST(FatNodePersistentSegmentTreeTest, NegativeDeltaPartialRange) {
  FatNodePersistentSegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// v1: +10 to [0,3]. v2: −3 to [1,2].
TEST(FatNodePersistentSegmentTreeTest, MixedPositiveAndNegativeDeltas) {
  FatNodePersistentSegmentTree tree({0, 0, 0, 0});

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

TEST(FatNodePersistentSegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

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
TEST(FatNodePersistentSegmentTreeTest, EachVersionInChainReturnsCorrectCumulative) {
  FatNodePersistentSegmentTree tree({0});
  std::vector<std::size_t> versions;
  versions.reserve(10);

  for (int i = 0; i < 10; ++i) {
    versions.push_back(tree.rangeAdd(0, 0, 1));
  }

  EXPECT_EQ(tree.nodeCount(), 4u); // 1 + floor(10 / 3): the leaf root overflowed three times
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 0);
  for (std::size_t i = 0; i < versions.size(); ++i) {
    EXPECT_EQ(tree.rangeSum(versions[i], 0, 0), static_cast<long long>(i) + 1);
  }
}

TEST(FatNodePersistentSegmentTreeTest, InterleavedPartialUpdateIsolation) {
  FatNodePersistentSegmentTree tree({1, 1, 1, 1});

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

TEST(FatNodePersistentSegmentTreeTest, FailedUpdatePublishesNothing) {
  FatNodePersistentSegmentTree tree({1, 2, 3});

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  EXPECT_THROW(tree.rangeAdd(2, 1, 5), std::invalid_argument);
  EXPECT_THROW(tree.rangeAdd(0, 3, 5), std::out_of_range);

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

TEST(FatNodePersistentSegmentTreeTest, InvalidRangeOnUpdateThrows) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

TEST(FatNodePersistentSegmentTreeTest, InvalidRangeOnQueryThrows) {
  FatNodePersistentSegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ---------------------------------------------------------------------------
// Large 64-bit values
// ---------------------------------------------------------------------------

TEST(FatNodePersistentSegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;
  FatNodePersistentSegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

TEST(FatNodePersistentSegmentTreeTest, LargeNegativeValuesCorrect) {
  const long long base = -500'000'000'000'000LL;
  const long long delta = -100'000'000'000'000LL;
  FatNodePersistentSegmentTree tree({base, base, base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);

  std::size_t v1 = tree.rangeAdd(0, 3, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4 * base);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4 * base + 4 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}
