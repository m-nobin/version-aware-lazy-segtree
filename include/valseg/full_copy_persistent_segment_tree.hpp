#ifndef VALSEG_FULL_COPY_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_FULL_COPY_PERSISTENT_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Full-Copy Persistent Segment Tree (Benchmarking Baseline).
 *
 * This structure serves as the Tier 1 baseline representing the cost of *not*
 * path-copying. On every `rangeAdd`, the entire tree (all 2n-1 nodes) is
 * rebuilt from scratch and pushed into the arena. 
 *
 * Because the entire tree is rebuilt on every update, there is no need for
 * lazy propagation tags to defer updates. The node structure simply stores
 * the exact sum.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(n), allocating 2n - 1 copied nodes
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n)
 */
class FullCopyPersistentSegmentTree {
public:
  using ValueType = long long;

  FullCopyPersistentSegmentTree();

  explicit FullCopyPersistentSegmentTree(const std::vector<ValueType>& values);

  void initialize(const std::vector<ValueType>& values);

  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  std::size_t versionCount() const;

  std::size_t size() const;

  std::size_t nodeCount() const;

private:
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    ValueType sum;
  };

  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  std::vector<Node> nodes;
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

  static ValueType segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
