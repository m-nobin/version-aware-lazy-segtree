#ifndef VALSEG_POINT_ONLY_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_POINT_ONLY_PERSISTENT_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Point-only persistent segment tree (benchmark baseline).
 *
 * Supports the same operation model as PersistentLazySegmentTree:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * This baseline keeps path copying and structural sharing but drops the
 * lazy tag, so it isolates what the lazy tag buys. It uses the same
 * index-based, append-only arena as PersistentLazySegmentTree; nodes outside
 * an update range stay shared between versions. Inside the range, however,
 * an update cannot stop at a fully covered node: with no tag to defer the
 * delta it must descend to every leaf in the range, copying every node on
 * the way.
 *
 * A node stores only its two child indices and the exact segment sum
 * (three 8-byte fields, 24 bytes per node, versus 32 bytes in the lazy tree).
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        Θ(k + log n) for a range of k leaves, appending exactly
 *                    the nodes whose segment intersects the range: the k
 *                    leaves plus every ancestor of one of them, never more
 *                    than 2k + 2h - 1 for tree height h. A single point costs
 *                    h + 1 nodes; the full range costs Θ(n), copying all
 *                    2n - 1 nodes.
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n), allocation-free
 *
 * Retained space after U non-zero updates covering k_1, ..., k_U leaves:
 * 2n - 1 + sum of Θ(k_i + log n) nodes, i.e. O(n + sum(k_i) + U log n).
 */
class PointOnlyPersistentSegmentTree {
public:
  using ValueType = long long;

  /**
   * @brief Construct a tree with no versions.
   */
  PointOnlyPersistentSegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial array; may be empty.
   */
  explicit PointOnlyPersistentSegmentTree(const std::vector<ValueType>& values);

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
   * Publishes exactly one new version. A non-zero value copies every leaf in
   * the range and every ancestor of such a leaf (Θ(k + log n) nodes for k
   * leaves); nodes outside the range are shared with the previous version.
   * A zero value publishes a new version that reuses the latest root without
   * allocating nodes. A failed update publishes no version and appends no
   * nodes.
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
   * Queries allocate nothing and mutate nothing.
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
   */
  std::size_t versionCount() const;

  /**
   * @brief Number of elements in every version.
   */
  std::size_t size() const;

  /**
   * @brief Total number of nodes stored in the arena.
   *
   * Grows by exactly the number of nodes whose segment intersects the update
   * range on every non-zero update. Read-only evidence for the sharing tests
   * and memory benchmarks; multiply by 24 bytes for the node payload.
   */
  std::size_t nodeCount() const;

private:
  /**
   * @brief Immutable tree node stored in the append-only arena.
   *
   * Arena indices stay valid across vector reallocation. No node reachable
   * from a published root is ever modified.
   */
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    ValueType sum;
  };

  /**
   * Sentinel arena index used for leaf children and the empty-array root.
   */
  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  /**
   * Append-only arena of immutable nodes owned by the tree.
   */
  std::vector<Node> nodes;

  /**
   * roots[v] is the arena index of version v's root.
   */
  std::vector<std::size_t> roots;

  std::size_t arraySize;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  static std::size_t build(const std::vector<ValueType>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight);

  std::size_t pointOnlyUpdate(std::size_t nodeIndex, std::size_t segmentLeft,
                              std::size_t segmentRight, std::size_t queryLeft,
                              std::size_t queryRight, ValueType value);

  ValueType query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight) const;

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
