#ifndef VALSEG_BUFFERED_PATH_COPYING_SEGMENT_TREE_HPP
#define VALSEG_BUFFERED_PATH_COPYING_SEGMENT_TREE_HPP

#include <array>
#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Buffered path-copying segment tree (benchmark baseline).
 *
 * Supports the same operation model as PersistentLazySegmentTree:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * This baseline keeps the lazy tag and path copying but gives every node a
 * two-slot buffer of version-tagged value modifications, so it isolates what
 * a small in-node buffer buys over plain path copying: fewer copied nodes
 * per update, paid for with a 2.75x larger node and a two-entry version
 * filter on every node a query visits. It is a design-space point between
 * path copying and node copying, not node copying itself: the buffer holds
 * only value deltas (sum and lazy), never a child index, so once any visited
 * node is copied every visited ancestor must be copied as well.
 *
 * An update visits exactly the nodes PersistentLazySegmentTree would copy.
 * A visited node whose buffer has a free slot absorbs its share of the delta
 * in place, tagged with the new version number; a visited node whose buffer
 * is full is copied with its buffer flushed into the base fields, and so is
 * every visited ancestor of a copied node. Nodes reachable from published
 * roots are therefore mutated in place, but only by appending version-tagged
 * entries that readers of older versions ignore, so historical results never
 * change. The latest version's nodes always reference the latest copy of each
 * child (a copied child forces its parent to be copied with the new index),
 * so no forwarding pointers are needed.
 *
 * Sum and lazy-tag invariant, for a node read at version v (base fields plus
 * every buffered entry tagged <= v):
 *  - sum is the exact sum of the entire segment in version v;
 *  - lazy is already included in the node's own sum;
 *  - lazy is not included in either child's sum.
 *
 * A node stores two child indices, the base sum and lazy, the buffer fill and
 * two 24-byte entries {version, deltaSum, deltaLazy}: 88 bytes per node,
 * versus 32 bytes in the lazy tree.
 *
 * Time Complexity, with P the set of nodes an update visits (|P| = O(log n),
 * the same set the lazy tree copies) and d the depth of a leaf
 * (floor(log2 n) or ceil(log2 n)):
 *  Build:            O(n)
 *  Range Add:        O(log n) time, appending between 0 and |P| nodes: 0
 *                    when every visited node has a free slot, |P| when every
 *                    visited node is copied. Repeated point updates on one
 *                    leaf of a fresh tree append 0, 0, d + 1, 0, 0, d + 1, ...
 *                    nodes: (d + 1) / 3 amortised, one third of the lazy
 *                    tree's d + 1 per update.
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n), allocation-free; every visited node folds at
 *                    most two buffered entries into its base fields.
 *
 * Retained space after U non-zero updates: 2n - 1 + sum of c_i nodes with
 * 0 <= c_i <= |P_i|, i.e. O(n + U log n) worst case, at 88 bytes per node.
 *
 * Numeric domain: every stored long long element, canonical segment sum,
 * buffered or base lazy tag and evaluated arithmetic intermediate must be
 * exactly representable. An out-of-domain initialization, update or query
 * throws std::overflow_error; failed writes are rolled back completely.
 */
class BufferedPathCopyingSegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct a tree with no versions.
   */
  BufferedPathCopyingSegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial array; may be empty.
   *
   * @throws std::overflow_error A canonical segment sum is not representable.
   */
  explicit BufferedPathCopyingSegmentTree(const std::vector<ValueType>& values);

  /**
   * @brief Build version 0, replacing all previous versions and nodes.
   *
   * Replacement state is built first and swapped in only after the build
   * succeeds, so a failed initialization leaves the tree unchanged.
   *
   * @param values Initial array; may be empty.
   *
   * @throws std::overflow_error A canonical segment sum is not representable;
   *                             the previous versions are left unchanged.
   */
  void initialize(const std::vector<ValueType>& values);

  /**
   * @brief Add value to every element in [left, right] of the latest version.
   *
   * Publishes exactly one new version. A non-zero value visits the O(log n)
   * nodes the lazy tree would copy; each one absorbs its share of the delta
   * into a free buffer slot, or is copied when its buffer is full or one of
   * its children was copied. Nodes outside the range are shared with the
   * previous version. A zero value publishes a new version that reuses the
   * latest root without allocating nodes. A failed update publishes no
   * version, appends no nodes and leaves every buffer as it was.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   * @param value Value to add.
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   * @throws std::overflow_error    The update leaves the numeric domain; no
   *                                version, node or buffer entry is published.
   */
  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right] in the given version.
   *
   * Queries allocate nothing and mutate nothing; every visited node folds
   * the buffered entries tagged <= version into its base fields.
   *
   * @param version Version to query.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   * @return Sum of the elements in [left, right] of that version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::out_of_range      Invalid version number.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   * @throws std::overflow_error    The exact requested sum is not representable.
   */
  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  /**
   * @brief Number of published versions.
   *
   * @return Number of published versions, or zero before initialization.
   */
  std::size_t versionCount() const;

  /**
   * @brief Number of elements in every version.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const;

  /**
   * @brief Total number of nodes stored in the arena.
   *
   * Grows by between 0 and the number of visited nodes on every non-zero
   * update: exactly the visited nodes with a full buffer plus their visited
   * ancestors. Read-only evidence for the buffering tests and memory
   * benchmarks; multiply by 88 bytes for the node payload.
   *
   * @return Number of nodes retained by the structure.
   */
  std::size_t nodeCount() const;

private:
  /**
   * Number of buffered modifications a node holds before it is copied.
   */
  static constexpr std::size_t bufferCapacity = 2;

  /**
   * @brief One buffered modification: the version that made it and the
   * deltas it adds to the node's sum and lazy tag.
   */
  struct Modification {
    std::size_t version;
    ValueType deltaSum;
    ValueType deltaLazy;
  };

  /**
   * @brief Tree node stored in the append-only arena.
   *
   * Arena indices stay valid across vector reallocation. Unlike the sibling
   * baselines a node reachable from a published root is mutated in place,
   * but only by appending an entry to its buffer, tagged with a newer version;
   * base fields, child indices and older entries are never changed, and a
   * copy flushes every entry into fresh base fields.
   */
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    ValueType baseSum;
    ValueType baseLazy;
    std::size_t bufferSize;
    std::array<Modification, bufferCapacity> buffer;
  };

  // The node layout is part of the memory-benchmark contract: five 8-byte
  // fields plus two 24-byte entries with no padding, 88 bytes on the 64-bit
  // targets the benchmarks run on (32-bit ABIs may pad the entries).
  static_assert(sizeof(std::size_t) != 8 || (sizeof(Modification) == 24 && sizeof(Node) == 88),
                "Node must pack five 8-byte fields and two 24-byte entries without padding");

  /**
   * @brief Sum and lazy tag of a node as read at one version.
   */
  struct Snapshot {
    ValueType sum;
    ValueType lazy;
  };

  /**
   * Sentinel arena index used for leaf children and the empty-array root.
   */
  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  /**
   * Append-only arena of nodes owned by the tree.
   */
  std::vector<Node> nodes;

  /**
   * roots[v] is the arena index of version v's root.
   */
  std::vector<std::size_t> roots;

  std::size_t arraySize;

  /**
   * Nodes that absorbed an entry in place during the update in progress;
   * a failed update pops those entries again. Kept as a member so that,
   * once it has grown to the largest visited set, a successful update
   * allocates nothing beyond copied nodes.
   */
  std::vector<std::size_t> bufferedThisUpdate;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  static std::size_t build(const std::vector<ValueType>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight);

  /**
   * Sum and lazy of node as read at version: the base fields plus every
   * buffered entry tagged <= version.
   */
  static Snapshot snapshotAt(const Node& node, std::size_t version);

  /**
   * Apply one modification to the node at nodeIndex: append it to a free
   * buffer slot in place, or copy the node with its buffer flushed. Returns
   * the index that now holds the modification (the node itself or its copy).
   */
  std::size_t modify(std::size_t nodeIndex, std::size_t version, ValueType deltaSum,
                     ValueType deltaLazy);

  std::size_t update(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                     std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                     ValueType value);

  ValueType query(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                  std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                  ValueType inheritedLazy) const;

  static std::size_t segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
