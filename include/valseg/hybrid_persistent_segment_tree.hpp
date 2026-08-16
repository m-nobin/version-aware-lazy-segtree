#ifndef VALSEG_HYBRID_PERSISTENT_SEGMENT_TREE_HPP
#define VALSEG_HYBRID_PERSISTENT_SEGMENT_TREE_HPP

#include <array>
#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Path-Copying with Node-Updating (Hybrid Baseline).
 *
 * This structure serves as the Tier 1 baseline representing the theoretically
 * optimal hybrid technique by Driscoll et al. (1989).
 *
 * It bridges Path-Copying and Fat-Nodes. Each node has standard fields and a
 * small `buffer` of modifications. Updates write into the buffer in O(1) time
 * without copying nodes or cascading to parents. When a node's buffer fills
 * up, the node is copied (path-copying), its buffer is flushed into its core
 * fields, and the update recursively cascades up to the parent.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        Amortized O(log n) allocations
 *  Zero-delta Add:   O(1), reusing the latest root
 *  Historical Sum:   O(log n) (queries apply buffer modifications dynamically)
 */
class HybridPersistentSegmentTree {
public:
  using ValueType = long long;

  HybridPersistentSegmentTree();

  explicit HybridPersistentSegmentTree(const std::vector<ValueType>& values);

  void initialize(const std::vector<ValueType>& values);

  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  std::size_t versionCount() const;

  std::size_t size() const;

  std::size_t nodeCount() const;

private:
  static constexpr std::size_t BUFFER_CAPACITY = 2;
  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  struct Modification {
    std::size_t version;
    ValueType deltaSum;
    ValueType deltaLazy;
  };

  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;

    ValueType baseSum;
    ValueType baseLazy;

    std::size_t bufferSize;
    std::array<Modification, BUFFER_CAPACITY> buffer;

    // In hybrid trees, when a node is copied because its buffer is full,
    // old readers must be forwarded to the new node.
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

  void applyBuffer(ValueType& sum, ValueType& lazy, const Node& node, std::size_t version) const;

  ValueType getSum(std::size_t nodeIndex, std::size_t version) const;

  std::size_t update(std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft,
                     std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                     ValueType value, std::vector<UndoRecord>& undoLog);

  std::size_t modifyNode(std::size_t nodeIndex, std::size_t version, ValueType deltaSum,
                         ValueType deltaLazy, std::vector<UndoRecord>& undoLog);

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
