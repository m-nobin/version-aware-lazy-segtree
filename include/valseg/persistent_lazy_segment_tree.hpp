#ifndef VALSEG_PERSISTENT_LAZY_SEGMENT_TREE_HPP
#define VALSEG_PERSISTENT_LAZY_SEGMENT_TREE_HPP

#include <valseg/detail/sum_add_domain.hpp>

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Partially persistent lazy segment tree.
 *
 * Supports:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * Every successful update publishes exactly one new immutable version
 * through path copying. Nodes untouched by an update stay shared between
 * versions, so historical query results never change.
 *
 * Sum and lazy-tag invariant, for a node covering a segment:
 *  - sum is the exact sum of the entire segment in that version;
 *  - lazy is already included in the node's own sum;
 *  - lazy is not included in either child's sum.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(log n), appending O(log n) copied nodes
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n), allocation-free
 *
 * Numeric domain: values model mathematical signed integers in a long long
 * representation. Initialization and updates require every canonical segment
 * sum, retained lazy tag and evaluated arithmetic intermediate to be
 * representable. Queries require the same of the requested sum. Otherwise
 * std::overflow_error is thrown, and a failed initialization or update
 * publishes nothing and leaves every existing version unchanged. The listed
 * update bound applies when a constant-time magnitude envelope proves this
 * domain. If that proof is inconclusive, an O(n) read-only exact preflight
 * runs before the logarithmic update.
 */
class PersistentLazySegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct a tree with no versions.
   */
  PersistentLazySegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial array; an empty array publishes an empty version 0.
   *
   * @throws std::overflow_error A canonical segment sum is not representable.
   */
  explicit PersistentLazySegmentTree(const std::vector<ValueType>& values);

  /**
   * @brief Build version 0, replacing all previous versions and nodes.
   *
   * Replacement state is built first and swapped in only after the build
   * succeeds, so a failed initialization leaves the tree unchanged.
   *
   * @param values Initial array; an empty array publishes an empty version 0.
   *
   * @throws std::overflow_error A canonical segment sum is not representable;
   *                             the previous versions are left unchanged.
   */
  void initialize(const std::vector<ValueType>& values);

  /**
   * @brief Add value to every element in [left, right] of the latest version.
   *
   * Publishes exactly one new version. A zero value publishes a new version
   * that reuses the latest root without allocating nodes. A failed update
   * publishes no version and appends no nodes.
   *
   * @param left  Left index of the updated range (inclusive).
   * @param right Right index of the updated range (inclusive).
   * @param value Value added to every element of the range.
   *
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   * @throws std::overflow_error    The update leaves the numeric domain; no
   *                                version or node is published.
   */
  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right] in the given version.
   *
   * Queries allocate nothing and mutate nothing.
   *
   * @param version Published version to read.
   * @param left    Left index of the queried range (inclusive).
   * @param right   Right index of the queried range (inclusive).
   *
   * @return Sum over [left, right] in that version.
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
   * Read-only evidence for structural-sharing tests and memory benchmarks.
   * Multiply by sizeof(Node) (32 bytes on 64-bit targets, static_asserted
   * below) for the retained payload in bytes.
   *
   * @return Number of nodes retained in the arena.
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
    ValueType lazy;
  };

  /**
   * Node layout is part of the benchmark comparison: two arena indices and two
   * values, 32 bytes on a 64-bit target, against 24 bytes for the tagless
   * baselines, 88 for the buffered one and 128 for the fat node. Guarded so a
   * 32-bit target does not fail the build over a documented byte count.
   */
  static_assert(sizeof(std::size_t) != 8 || sizeof(Node) == 32,
                "PersistentLazySegmentTree::Node is documented as 32 bytes on 64-bit targets.");

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

  detail::SumAddDomainGuard numericDomain;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  static std::size_t build(const std::vector<ValueType>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight);

  static std::size_t segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  std::size_t update(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t queryLeft, std::size_t queryRight, ValueType value);

  ValueType query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight, ValueType inheritedLazy) const;

  void materialize(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                   ValueType inheritedLazy, std::vector<ValueType>& values) const;

  void validateInitialized() const;

  void validateVersion(std::size_t version) const;

  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
