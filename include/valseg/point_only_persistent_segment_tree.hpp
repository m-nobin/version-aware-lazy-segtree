#ifndef VALSEG_POINT_ONLY_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_POINT_ONLY_PERSISTENT_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Point-Only Persistent Segment Tree (Benchmarking Baseline).
 *
 * This structure serves as the Tier 1 baseline representing the cost of *not*
 * using lazy propagation once path-copying persistence is already in place.
 *
 * It uses the same index-based, append-only arena as the proposed structure.
 * However, because there are no lazy tags, any range update must descend
 * recursively all the way to every leaf node within the range, creating new
 * path-copied nodes for all touched leaves.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(k * log n) where k is the number of leaves in the range
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n)
 */
class PointOnlyPersistentSegmentTree {
public:
  using ValueType = long long;

  PointOnlyPersistentSegmentTree();

  explicit PointOnlyPersistentSegmentTree(const std::vector<ValueType>& values);

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

  std::size_t pointOnlyUpdate(std::size_t nodeIndex, std::size_t segmentLeft,
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
