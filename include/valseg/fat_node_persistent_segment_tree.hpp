#ifndef VALSEG_FAT_NODE_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_FAT_NODE_PERSISTENT_SEGMENT_TREE_HPP

#include <array>
#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Fat-Node Persistent Segment Tree (Benchmarking Baseline).
 *
 * This structure serves as the Tier 1 baseline representing the classical
 * Fat-Node technique by Driscoll et al. (1989).
 *
 * Instead of path-copying (which allocates O(log n) new nodes per update),
 * each node contains a small history array for its `sum` and `lazy` values.
 * Updates append to this history array in-place. If the array is full, the
 * node "splits": a new node is allocated with the latest values, and the old
 * node's `successorNode` pointer is set to forward future readers.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(log n) (often amortized O(1) allocations due to history space)
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n + versions) (queries binary/linear search history)
 */
class FatNodePersistentSegmentTree {
public:
  using ValueType = long long;

  FatNodePersistentSegmentTree();

  explicit FatNodePersistentSegmentTree(const std::vector<ValueType>& values);

  void initialize(const std::vector<ValueType>& values);

  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  std::size_t versionCount() const;

  std::size_t size() const;

  std::size_t nodeCount() const;

private:
  struct VersionedValue {
    std::size_t version;
    ValueType value;
  };

  static constexpr std::size_t HISTORY_CAPACITY = 3;
  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;

    std::size_t sumCount;
    std::array<VersionedValue, HISTORY_CAPACITY> sums;

    std::size_t lazyCount;
    std::array<VersionedValue, HISTORY_CAPACITY> lazys;

    std::size_t successorNode;
    std::size_t successorVersion;
  };

  struct UndoRecord {
    std::size_t nodeIndex;
    Node state;
  };

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

  std::size_t resolveNode(std::size_t nodeIndex, std::size_t version) const;

  ValueType getSum(std::size_t nodeIndex, std::size_t version) const;
  ValueType getLazy(std::size_t nodeIndex, std::size_t version) const;

  ValueType getLatestSum(std::size_t resolvedNodeIndex) const;
  ValueType getLatestLazy(std::size_t resolvedNodeIndex) const;

  void appendSumAndLazy(std::size_t nodeIndex, std::size_t version, ValueType sumVal,
                        ValueType lazyVal, bool updateLazy, std::vector<UndoRecord>& undoLog);

  ValueType update(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                   std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                   ValueType value, std::vector<UndoRecord>& undoLog);

  ValueType query(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                  std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                  ValueType inheritedLazy) const;

  static ValueType segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
