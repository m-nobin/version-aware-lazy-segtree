#include <valseg/persistent_lazy_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>

using valseg::PersistentLazySegmentTree;

// ---------------------------------------------------------------------------
// Deterministic suite for the persistent tree, covering issue #7.
//
// Every case asserts the version numbers returned by rangeAdd, queries the
// affected versions after each update, and uses versionCount() and
// nodeCount() as evidence of publication and structural sharing.
// ---------------------------------------------------------------------------

// ===========================================================================
// GROUP 1 — Initialization
// ===========================================================================

// Verifies that constructing from a non-empty array produces exactly one
// version (version 0), the correct element count, the expected node count
// (2n − 1 for a full binary tree), and correct sums for both full and
// single-element ranges.
TEST(PersistentLazySegmentTreeTest, InitializationPublishesVersionZero) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 5u);

  // A segment tree over n leaves stores exactly 2n − 1 nodes.
  EXPECT_EQ(tree.nodeCount(), 9u);

  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 2, 2), 3);
}

// Verifies that initializing with an empty vector produces one version whose
// root is the sentinel, size() returns zero, and no nodes are allocated.
// Any range operation must throw because the structure is empty.
TEST(PersistentLazySegmentTreeTest, EmptyArrayInitializationRejectsRangeOperations) {
  PersistentLazySegmentTree tree(std::vector<long long>{});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);

  // Both rangeSum and rangeAdd must reject because arraySize is zero.
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

// Verifies that initialize() replaces all previously published versions and
// nodes completely. After reinitialization the tree behaves as if it were
// freshly constructed from the new array; the old version count, node count,
// and historical results are all gone.
TEST(PersistentLazySegmentTreeTest, ReinitializationReplacesAllState) {
  PersistentLazySegmentTree tree({1, 2, 3});

  // Publish one update so the tree has history to discard.
  tree.rangeAdd(0, 2, 10);
  EXPECT_EQ(tree.versionCount(), 2u);

  // Reinitialize with a completely different array.
  tree.initialize({10, 20});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 2u);

  // 2 leaves → 2×2−1 = 3 nodes.
  EXPECT_EQ(tree.nodeCount(), 3u);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);

  // The old version 1 no longer exists.
  EXPECT_THROW(tree.rangeSum(1, 0, 1), std::out_of_range);
}

// Verifies a single-element tree: n=1 produces exactly one leaf node,
// a full-coverage update copies only that leaf, and historical isolation
// holds across both versions.
TEST(PersistentLazySegmentTreeTest, SingleElementTree) {
  PersistentLazySegmentTree tree({7});

  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.size(), 1u);

  // 2×1−1 = 1 node (only the leaf; no internal nodes).
  EXPECT_EQ(tree.nodeCount(), 1u);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);

  std::size_t v1 = tree.rangeAdd(0, 0, 3);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 2u);        // one new leaf node
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 7);   // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // 7 + 3
}

// ===========================================================================
// GROUP 2 — Hand-traced historical results (design reference example)
// ===========================================================================

// Traces the exact example from the design document step by step.
// Confirms every historical query result after each publication, and uses
// versionCount() and nodeCount() as structural evidence.
//
// Version 0 : [1, 2, 3, 4]   sum = 10
// Version 1 : rangeAdd(0,3,5) — full coverage, root only copied
//             sum = 30,  rangeSum(v1,1,2) = 15
// Version 2 : rangeAdd(1,2,2) — partial update, 5 new nodes
//             sum = 34,  rangeSum(v2,1,2) = 19
// All earlier results must stay unchanged after each publication.
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
  // side: 5 new nodes total (8 → 13).
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);
  EXPECT_EQ(tree.nodeCount(), 13u);

  // Every historical result must remain unchanged.
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 34);
  EXPECT_EQ(tree.rangeSum(v2, 1, 2), 19);
}

// ===========================================================================
// GROUP 3 — Full, partial, and overlapping updates
// ===========================================================================

// Verifies a full-coverage update: the request spans the entire array, so
// the update stops at the root without recursing. Only the root is copied;
// both subtrees are shared with the previous version.
TEST(PersistentLazySegmentTreeTest, FullCoverageUpdateCopiesOnlyTheRoot) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  std::size_t before = tree.nodeCount(); // 7
  std::size_t v1 = tree.rangeAdd(0, 3, 5);

  // Full coverage stops at the root; both subtrees stay shared.
  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 1u); // only the root was copied
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 30);
}

// Verifies a partial update that touches only the left half of the tree.
// Nodes on the right subtree must be shared, so nodeCount must grow by
// exactly the path length on the left side plus a new root.
// Array: {10, 20, 30, 40}. Update [0,1] +5.
// Expected v1 rangeSum([0,1]) = 40, rangeSum([2,3]) unchanged = 70.
TEST(PersistentLazySegmentTreeTest, PartialUpdateLeftHalfOnly) {
  PersistentLazySegmentTree tree({10, 20, 30, 40});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 1, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before + 2u);

  // Version 0 untouched.
  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_EQ(tree.rangeSum(0, 2, 3), 70);
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 100);

  // Version 1: left side updated, right side unchanged.
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 40);
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 70);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
}

// Verifies a partial update that touches only the right half of the tree.
// Mirrors PartialUpdateLeftHalfOnly to confirm the right-branch path
// is also handled correctly.
// Array: {10, 20, 30, 40}. Update [2,3] +5.
TEST(PersistentLazySegmentTreeTest, PartialUpdateRightHalfOnly) {
  PersistentLazySegmentTree tree({10, 20, 30, 40});

  std::size_t before = tree.nodeCount(); // 7
  std::size_t v1 = tree.rangeAdd(2, 3, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);

  // Copied: new root + new right-child [2,3] = 2 new nodes.
  EXPECT_EQ(tree.nodeCount(), before + 2u);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 30);
  EXPECT_EQ(tree.rangeSum(0, 2, 3), 70);

  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 30); // left unchanged
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 80); // right updated
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 110);
}

// Verifies two overlapping updates where each subsequent update partially
// overlaps the previous range. Confirms that the inherited lazy values
// from v1 are correctly applied when querying v2 on ranges that cross
// the update boundary.
//
// Array  : {1, 2, 3, 4, 5}
// v1     : rangeAdd(1, 3, 10)  → elements logically 1,12,13,14,5
// v2     : rangeAdd(2, 4,  5)  → elements logically 1,12,18,19,10
TEST(PersistentLazySegmentTreeTest, OverlappingUpdatesAccumulateCorrectly) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  std::size_t v1 = tree.rangeAdd(1, 3, 10);
  std::size_t v2 = tree.rangeAdd(2, 4, 5);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(tree.versionCount(), 3u);

  // Version 0 — original values.
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
  EXPECT_EQ(tree.rangeSum(0, 1, 3), 9);

  // Version 1 — only [1,3] updated.
  EXPECT_EQ(tree.rangeSum(v1, 0, 4), 45);
  EXPECT_EQ(tree.rangeSum(v1, 1, 3), 39);
  EXPECT_EQ(tree.rangeSum(v1, 2, 4), 32); // 13+14+5
  EXPECT_EQ(tree.rangeSum(v1, 4, 4), 5);  // element 4 unchanged in v1

  // Version 2 — [2,4] additionally updated on top of v1.
  EXPECT_EQ(tree.rangeSum(v2, 0, 4), 60);
  EXPECT_EQ(tree.rangeSum(v2, 2, 4), 47); // 18+19+10
  EXPECT_EQ(tree.rangeSum(v2, 1, 1), 12); // element 1: only v1 touched it

  // Version 0 isolation: none of the above updates must affect it.
  EXPECT_EQ(tree.rangeSum(0, 0, 4), 15);
}

// Verifies that repeated full-coverage updates on the same range accumulate
// correctly across many versions. Each update copies only the root, so
// nodeCount grows by exactly one per update.
//
// Array: {0, 0, 0, 0}. Add 1 three times.
// v1 sum=4, v2 sum=8, v3 sum=12.
TEST(PersistentLazySegmentTreeTest, RepeatedFullCoverageUpdatesAccumulate) {
  PersistentLazySegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 1);
  std::size_t v3 = tree.rangeAdd(0, 3, 1);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(v2, 2u);
  EXPECT_EQ(v3, 3u);
  EXPECT_EQ(tree.versionCount(), 4u);

  // Each full-coverage update copies only the root: 7 + 3 = 10 nodes.
  EXPECT_EQ(tree.nodeCount(), 10u);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 8);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 12);
}

// Verifies that a single-element range update (touching exactly one leaf)
// creates exactly one new leaf plus one new node per level on the path from
// root to that leaf. For n=8 the path depth is 3, so 4 new nodes total.
TEST(PersistentLazySegmentTreeTest, SingleLeafUpdateCreatesExactPathLength) {
  // n=8: nodeCount=15, depth=3, so a single-leaf update creates 4 new nodes.
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

  EXPECT_EQ(tree.nodeCount(), 15u);

  std::size_t v1 = tree.rangeAdd(3, 3, 100);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), 19u); // 15 + 4 path nodes

  // Only element 3 changes.
  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(v1, 0, 7), 136);
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 104);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);  // untouched left segment
  EXPECT_EQ(tree.rangeSum(v1, 4, 7), 26); // untouched right segment
}

// ===========================================================================
// GROUP 4 — Negative values and negative deltas
// ===========================================================================

// Verifies that the tree is built and queried correctly when the initial
// array contains negative values. The lazy invariant must hold for negative
// sums as well as positive ones.
TEST(PersistentLazySegmentTreeTest, NegativeInitialValues) {
  PersistentLazySegmentTree tree({-3, -1, -4, -1});

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);
  EXPECT_EQ(tree.rangeSum(0, 1, 2), -5);

  std::size_t v1 = tree.rangeAdd(0, 3, 10);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), -9);  // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 31); // -9 + 10×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 15); // (-1+10) + (-4+10)
}

// Verifies that a negative delta subtracts correctly from all elements in
// the requested range, and that elements outside the range are unchanged.
TEST(PersistentLazySegmentTreeTest, NegativeDelta) {
  PersistentLazySegmentTree tree({5, 5, 5, 5});

  std::size_t v1 = tree.rangeAdd(0, 3, -3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 20); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 8); // 20 − 3×4
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 4); // (5−3)×2
}

// Verifies that a negative delta applied to a partial range does not affect
// elements outside that range, and that version 0 remains unchanged.
// Array: {10, 10, 10, 10}. Subtract 4 from [1,2].
TEST(PersistentLazySegmentTreeTest, NegativeDeltaPartialRange) {
  PersistentLazySegmentTree tree({10, 10, 10, 10});

  std::size_t v1 = tree.rangeAdd(1, 2, -4);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 40);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 32); // 40 − 4×2
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 10); // element 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 3, 3), 10); // element 3 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 12); // (10−4)×2
}

// Verifies that mixing positive and negative deltas across versions produces
// the correct accumulated result, including on ranges that span both the
// added and subtracted regions.
// Array: {0, 0, 0, 0}.
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

// ===========================================================================
// GROUP 5 — Zero-delta updates and structural sharing
// ===========================================================================

// Verifies that a zero-delta update publishes a new version (incrementing
// versionCount) without allocating any nodes (nodeCount stays unchanged).
// Both the new and old version must return identical results for full,
// partial, and single-element ranges.
TEST(PersistentLazySegmentTreeTest, ZeroDeltaUpdatePublishesVersionWithoutNodes) {
  PersistentLazySegmentTree tree({1, 2, 3});

  std::size_t before = tree.nodeCount();
  std::size_t v1 = tree.rangeAdd(0, 2, 0);

  EXPECT_EQ(v1, 1u);
  EXPECT_EQ(tree.versionCount(), 2u);
  EXPECT_EQ(tree.nodeCount(), before); // no new nodes allocated

  // Both versions read identically, including partial and leaf ranges.
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 6);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(v1, 1, 2), 5);
}

// Verifies that multiple consecutive zero-delta updates all share the same
// root correctly: each increments versionCount by one, none allocates nodes,
// and all versions return the same results.
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
  EXPECT_EQ(tree.nodeCount(), before); // still no new nodes

  // All versions must return the same result.
  EXPECT_EQ(tree.rangeSum(0, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v1, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v2, 0, 2), 15);
  EXPECT_EQ(tree.rangeSum(v3, 0, 2), 15);
}

// ===========================================================================
// GROUP 6 — Historical-version isolation after many updates
// ===========================================================================

// Verifies that version 0 is completely isolated after many subsequent
// updates. Ten full-coverage updates are applied; then version 0 is queried
// at multiple sub-ranges to confirm no node in the original tree was mutated.
TEST(PersistentLazySegmentTreeTest, VersionZeroIsolatedAfterManyUpdates) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5, 6, 7, 8});

  // Apply 10 updates, each adding 10 to the entire array.
  for (int i = 0; i < 10; ++i) {
    tree.rangeAdd(0, 7, 10);
  }

  EXPECT_EQ(tree.versionCount(), 11u);

  // Version 0 must still reflect the original values exactly.
  EXPECT_EQ(tree.rangeSum(0, 0, 7), 36);
  EXPECT_EQ(tree.rangeSum(0, 0, 0), 1);
  EXPECT_EQ(tree.rangeSum(0, 3, 5), 15); // 4+5+6
  EXPECT_EQ(tree.rangeSum(0, 7, 7), 8);
}

// Verifies that every intermediate version in a long update chain returns
// the correct cumulative result, not just the first and last versions.
// Array: {0}. Ten updates each adding 1.
// Version k must return k for all k in 0..10.
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

// Verifies deep lazy accumulation: three nested lazy tags accumulate through
// three levels of the tree and are correctly resolved in a single query pass.
// Array: {0, 0, 0, 0}.
// v1: +1 to [0,3]  → root lazy=1
// v2: +2 to [0,3]  → new root lazy=3
// v3: +3 to [0,3]  → new root lazy=6
// Query [1,2] on v3 must return 12 (6×2), resolved without any pushdown.
TEST(PersistentLazySegmentTreeTest, DeepLazyAccumulationResolvedCorrectly) {
  PersistentLazySegmentTree tree({0, 0, 0, 0});

  std::size_t v1 = tree.rangeAdd(0, 3, 1);
  std::size_t v2 = tree.rangeAdd(0, 3, 2);
  std::size_t v3 = tree.rangeAdd(0, 3, 3);

  EXPECT_EQ(tree.rangeSum(0, 0, 3), 0);
  EXPECT_EQ(tree.rangeSum(v1, 0, 3), 4);
  EXPECT_EQ(tree.rangeSum(v2, 0, 3), 12);
  EXPECT_EQ(tree.rangeSum(v3, 0, 3), 24);

  // Partial queries must also resolve the full accumulated lazy.
  EXPECT_EQ(tree.rangeSum(v3, 1, 2), 12); // 6 per element × 2 elements
  EXPECT_EQ(tree.rangeSum(v2, 0, 1), 6);  // 3 per element × 2 elements
}

// Verifies isolation across interleaved partial updates on different regions.
// Updates to [0,1] must never affect queries on [2,3] and vice versa, for
// any version.
TEST(PersistentLazySegmentTreeTest, InterleavedPartialUpdateIsolation) {
  PersistentLazySegmentTree tree({1, 1, 1, 1});

  std::size_t v1 = tree.rangeAdd(0, 1, 10); // left half only
  std::size_t v2 = tree.rangeAdd(2, 3, 20); // right half only
  std::size_t v3 = tree.rangeAdd(0, 1, 5);  // left half again

  // v1: left=22, right=2.
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 22);
  EXPECT_EQ(tree.rangeSum(v1, 2, 3), 2);

  // v2: left=22 (v1 left), right=42.
  EXPECT_EQ(tree.rangeSum(v2, 0, 1), 22);
  EXPECT_EQ(tree.rangeSum(v2, 2, 3), 42);

  // v3: left=32 (v2 left + 5×2), right=42 (v2 right unchanged).
  EXPECT_EQ(tree.rangeSum(v3, 0, 1), 32);
  EXPECT_EQ(tree.rangeSum(v3, 2, 3), 42);

  // Version 0 must still be entirely unchanged.
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 4);
}

// ===========================================================================
// GROUP 7 — Failed-operation isolation
// ===========================================================================

// Verifies that a failed update (invalid range or out-of-bounds) publishes
// no new version and appends no nodes, leaving the existing structure intact.
// The tree must still answer correct queries after the failed attempts.
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

// Verifies that a failed query (invalid version or out-of-bounds range)
// throws the correct exception type without affecting the tree state.
TEST(PersistentLazySegmentTreeTest, FailedQueryThrowsAndLeavesStateIntact) {
  PersistentLazySegmentTree tree({1, 2, 3, 4});

  tree.rangeAdd(0, 3, 5); // publish version 1

  std::size_t versionsBefore = tree.versionCount();
  std::size_t nodesBefore = tree.nodeCount();

  // Invalid version.
  EXPECT_THROW(tree.rangeSum(5, 0, 3), std::out_of_range);
  // left > right.
  EXPECT_THROW(tree.rangeSum(0, 3, 1), std::invalid_argument);
  // right >= size().
  EXPECT_THROW(tree.rangeSum(0, 0, 4), std::out_of_range);

  EXPECT_EQ(tree.versionCount(), versionsBefore);
  EXPECT_EQ(tree.nodeCount(), nodesBefore);

  // Existing results unaffected.
  EXPECT_EQ(tree.rangeSum(0, 0, 3), 10);
  EXPECT_EQ(tree.rangeSum(1, 0, 3), 30);
}

// ===========================================================================
// GROUP 8 — Validation contracts
// ===========================================================================

// Verifies that a default-constructed tree (no initialize called) has no
// versions, no size, and no nodes, and rejects both rangeSum and rangeAdd
// with std::runtime_error.
TEST(PersistentLazySegmentTreeTest, DefaultTreeHasNoVersions) {
  PersistentLazySegmentTree tree;

  EXPECT_EQ(tree.versionCount(), 0u);
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_EQ(tree.nodeCount(), 0u);
  EXPECT_THROW(tree.rangeSum(0, 0, 0), std::runtime_error);
  EXPECT_THROW(tree.rangeAdd(0, 0, 1), std::runtime_error);
}

// Verifies that requesting a version that has not been published yet throws
// std::out_of_range. Only version 0 exists after initialization.
TEST(PersistentLazySegmentTreeTest, InvalidVersionQueryThrows) {
  PersistentLazySegmentTree tree({1, 2, 3});

  EXPECT_THROW(tree.rangeSum(1, 0, 2), std::out_of_range);
}

// Verifies all four invalid-range conditions for rangeAdd:
// left > right, right >= size(), and both on an empty-initialized tree.
TEST(PersistentLazySegmentTreeTest, InvalidRangeOnUpdateThrows) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeAdd(3, 2, 1), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeAdd(0, 5, 1), std::out_of_range);     // right >= size
  EXPECT_THROW(tree.rangeAdd(4, 5, 1), std::out_of_range);     // right >= size
}

// Verifies all invalid-range conditions for rangeSum:
// invalid version, left > right, and right >= size().
TEST(PersistentLazySegmentTreeTest, InvalidRangeOnQueryThrows) {
  PersistentLazySegmentTree tree({1, 2, 3, 4, 5});

  EXPECT_THROW(tree.rangeSum(1, 0, 4), std::out_of_range);     // invalid version
  EXPECT_THROW(tree.rangeSum(0, 3, 2), std::invalid_argument); // left > right
  EXPECT_THROW(tree.rangeSum(0, 0, 5), std::out_of_range);     // right >= size
}

// ===========================================================================
// GROUP 9 — Large 64-bit values
// ===========================================================================

// Verifies correct handling of values near the long long range.
// Uses values of 1e15 and a delta of 1e14 to confirm that the lazy
// arithmetic stays within long long without overflow on this input.
//
// Array: {1_000_000_000_000_000, 1_000_000_000_000_000}
// After +100_000_000_000_000 to both: sum = 2_200_000_000_000_000.
TEST(PersistentLazySegmentTreeTest, LargeValuesSumAndUpdateCorrectly) {
  const long long base = 1'000'000'000'000'000LL;
  const long long delta = 100'000'000'000'000LL;

  PersistentLazySegmentTree tree({base, base});

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base);

  std::size_t v1 = tree.rangeAdd(0, 1, delta);

  EXPECT_EQ(tree.rangeSum(0, 0, 1), 2 * base); // version 0 unchanged
  EXPECT_EQ(tree.rangeSum(v1, 0, 1), 2 * base + 2 * delta);
  EXPECT_EQ(tree.rangeSum(v1, 0, 0), base + delta);
  EXPECT_EQ(tree.rangeSum(v1, 1, 1), base + delta);
}

// Verifies correct handling of large negative sums. An array of large
// negative values combined with a negative delta must not wrap around
// within the representable long long range used by this test.
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