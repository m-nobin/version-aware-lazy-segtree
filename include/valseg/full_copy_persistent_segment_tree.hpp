#ifndef VALSEG_FULL_COPY_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_FULL_COPY_PERSISTENT_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Full-copy persistent segment tree (benchmark baseline).
 *
 * Supports the same operation model as PersistentLazySegmentTree:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * This baseline is the upper bound of the persistence design space: every
 * non-zero update rebuilds all 2n - 1 nodes of the tree and appends them to
 * the arena, so no node is ever shared between two versions. It exists to
 * measure what path copying and lazy tags save, not to be fast.
 *
 * Because every update materializes the full tree, no lazy tags are needed;
 * a node stores only its two child indices and the exact segment sum
 * (three 8-byte fields, 24 bytes per node, versus 32 bytes in the lazy tree).
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(n), appending exactly 2n - 1 copied nodes
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n), allocation-free
 *
 * Retained space after U non-zero updates: (2n - 1)(U + 1) nodes, i.e. O(nU).
 *
 * Numeric domain: every stored long long element, canonical segment sum and
 * evaluated arithmetic intermediate must represent its exact mathematical
 * value. An unrepresentable initialization, update or query throws
 * std::overflow_error; failed writes leave all published versions unchanged.
 */
class FullCopyPersistentSegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct a tree with no versions.
   */
  FullCopyPersistentSegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial array; may be empty.
   *
   * @throws std::overflow_error A canonical segment sum is not representable.
   */
  explicit FullCopyPersistentSegmentTree(const std::vector<ValueType>& values);

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
   * Publishes exactly one new version. A non-zero value copies the entire
   * tree (2n - 1 nodes). A zero value publishes a new version that reuses
   * the latest root without allocating nodes. A failed update publishes no
   * version and appends no nodes.
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
   *                                version or node is published.
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
   * Equals (2n - 1)(U + 1) after U non-zero updates. Read-only evidence for
   * the no-sharing tests and memory benchmarks; multiply by 24 bytes for the
   * node payload.
   *
   * @return Number of nodes retained by the structure.
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

  // The node layout is part of the memory-benchmark contract: two child
  // indices and one sum with no padding, 24 bytes on 64-bit targets.
  static_assert(sizeof(Node) == 2 * sizeof(std::size_t) + sizeof(ValueType),
                "Node must pack two child indices and one sum without padding");

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

  std::size_t fullCopyUpdate(std::size_t nodeIndex, std::size_t segmentLeft,
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
