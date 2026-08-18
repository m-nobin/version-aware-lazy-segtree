#ifndef VALSEG_FAT_NODE_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_FAT_NODE_PERSISTENT_SEGMENT_TREE_HPP

#include <array>
#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Fat-node persistent segment tree (benchmark baseline).
 *
 * Supports the same operation model as PersistentLazySegmentTree:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * This baseline is the classical alternative to path copying: partial
 * persistence by fat nodes with node copying (Driscoll, Sarnak, Sleator,
 * Tarjan, "Making data structures persistent", 1989). The tree shape is
 * fixed at build time; every node keeps a version-stamped list of its
 * states, and an update appends one state to each node it visits instead
 * of copying it. Only when a node's list is full is a fresh copy created,
 * and because the tree has in-degree one and updates always start at the
 * latest root, the parent is on the update path and records the fresh
 * index in the state it appends for the same version. Reading a node at
 * version v binary-searches its stamps, so no successor chains are ever
 * walked and the cost of an access does not depend on the number of
 * versions.
 *
 * It isolates the trade fat nodes make against path copying: one arena
 * append per HISTORY_CAPACITY modifications instead of one per copied
 * node, paid for with larger nodes, a version stamp per modification, and
 * a bounded search on every node access.
 *
 * Node layout: a state count plus HISTORY_CAPACITY = 3 states of
 * {version, leftChild, rightChild, sum, lazy} (five 8-byte fields, 40 bytes
 * each) — 128 bytes per node on 64-bit targets, both sizes static_asserted —
 * versus 32 bytes in the lazy tree.
 *
 * Time Complexity, with h = ceil(log2 n) the tree height and K the
 * capacity HISTORY_CAPACITY:
 *  Build:            O(n)
 *  Range Add:        O(log n), visiting the same nodes a path-copying
 *                    update copies (at most 4 per level, so at most
 *                    4(h + 1)) and appending one state to each; a visited
 *                    node whose K slots are full is replaced by a fresh
 *                    arena node instead. Arena growth is amortized O(1)
 *                    per append.
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n * log K) = O(log n), one binary search over
 *                    at most K stamps per visited node, allocation-free and
 *                    independent of the number of versions
 *
 * Retained space: a logical node that has received m modifications occupies
 * 1 + floor(m / K) arena nodes, so after U non-zero updates that visited
 * M nodes in total (M <= 4(h + 1) U) the arena holds exactly
 * 2n - 1 + sum over nodes of floor(m_v / K) nodes, at most 2n - 1 + M / K:
 * on top of a fixed 128 (2n - 1)-byte build (four times the lazy tree's),
 * growth costs 128 / K ~ 42.7 bytes per modification amortized against
 * 32 bytes per copied node in PersistentLazySegmentTree.
 */
class FatNodePersistentSegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * Number of states a node holds before the next modification is written
   * to a fresh copy. The first slot of every node is its creation state, so
   * a logical node occupies 1 + floor(m / HISTORY_CAPACITY) arena nodes
   * after m modifications.
   */
  static constexpr std::size_t HISTORY_CAPACITY = 3;

  /**
   * @brief Construct a tree with no versions.
   */
  FatNodePersistentSegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial array; may be empty.
   */
  explicit FatNodePersistentSegmentTree(const std::vector<ValueType>& values);

  /**
   * @brief Build version 0, replacing all previous versions and nodes.
   *
   * Replacement state is built first and swapped in only after the build
   * succeeds, so a failed initialization leaves the tree unchanged.
   *
   * @param values Initial array; may be empty.
   */
  void initialize(const std::vector<ValueType>& values);

  /**
   * @brief Add value to every element in [left, right] of the latest version.
   *
   * Publishes exactly one new version. A non-zero value appends one state,
   * stamped with the new version number, to every node the update visits
   * (O(log n) nodes) and appends a fresh arena node only for visited nodes
   * whose HISTORY_CAPACITY slots were already full; nodes outside the
   * update path are untouched and stay shared by every version. A zero
   * value publishes a new version that reuses the latest root without
   * touching any node. All arena and root capacity is reserved before the
   * first modification, so a failed update publishes no version, appends
   * no nodes, and leaves every state list unchanged.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   * @param value Value to add.
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right] in the given version.
   *
   * Queries allocate nothing and mutate nothing. Each visited node is read
   * at the requested version through one binary search over its stamps.
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
   * Grows on a non-zero update by exactly the number of visited nodes whose
   * HISTORY_CAPACITY state slots were already full. Read-only evidence for
   * the structural tests and memory benchmarks; multiply by 128 bytes for
   * the node payload.
   *
   * @return Number of nodes retained by the structure.
   */
  std::size_t nodeCount() const;

private:
  /**
   * @brief One version-stamped state of a node.
   *
   * A node's states are stored in increasing version order; the state in
   * effect at version v is the last one whose stamp is not greater than v.
   * Child indices are part of the state because a child that overflowed is
   * replaced by a fresh arena node from that version on.
   */
  struct Modification {
    std::size_t version;
    std::size_t leftChild;
    std::size_t rightChild;
    ValueType sum;
    ValueType lazy;
  };

  // On 64-bit targets the five fields pack without padding into 40 bytes;
  // 32-bit targets with 8-byte-aligned ValueType pad and are not part of the
  // memory-benchmark contract.
  static_assert(sizeof(std::size_t) != 8 || sizeof(Modification) == 40,
                "Modification must pack a stamp, two child indices, sum and lazy without padding");

  /**
   * @brief Fat node stored in the append-only arena.
   *
   * history[0 .. count) are the node's states in increasing version order.
   * A node is mutated only by appending a state for the version being
   * published; states reachable from a published root are never changed.
   */
  struct Node {
    std::size_t count;
    std::array<Modification, HISTORY_CAPACITY> history;
  };

  // The node layout is part of the memory-benchmark contract: a count and
  // HISTORY_CAPACITY states with no padding, 128 bytes on 64-bit targets.
  static_assert(sizeof(std::size_t) != 8 || sizeof(Node) == 128,
                "Node must pack the state count and HISTORY_CAPACITY states without padding");

  /**
   * Sentinel arena index used for leaf children and the empty-array root.
   */
  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  /**
   * Append-only arena of fat nodes owned by the tree.
   */
  std::vector<Node> nodes;

  /**
   * roots[v] is the arena index of version v's root.
   */
  std::vector<std::size_t> roots;

  std::size_t arraySize;

  /**
   * ceil(log2 arraySize): a range update visits at most 4 nodes per level,
   * so at most 4 * (treeHeight + 1) nodes in total.
   */
  std::size_t treeHeight;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  static std::size_t build(const std::vector<ValueType>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight);

  static ValueType segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  static const Modification& stateAt(const Node& node, std::size_t version);
  static const Modification& latestState(const Node& node);

  void reserveForUpdate();
  std::size_t record(std::size_t nodeIndex, const Modification& state);

  std::size_t update(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                     std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                     ValueType value);

  ValueType query(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                  std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                  ValueType inheritedLazy) const;

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
