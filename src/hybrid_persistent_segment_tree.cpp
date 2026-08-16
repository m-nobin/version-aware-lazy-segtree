#include <valseg/hybrid_persistent_segment_tree.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

HybridPersistentSegmentTree::HybridPersistentSegmentTree() : arraySize(0) {}

HybridPersistentSegmentTree::HybridPersistentSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void HybridPersistentSegmentTree::initialize(const std::vector<ValueType>& values) {
  std::vector<Node> newNodes;
  std::vector<std::size_t> newRoots;

  if (values.empty()) {
    newRoots.push_back(noNode);
  } else {
    newNodes.reserve(2 * values.size() - 1);
    newRoots.push_back(build(values, newNodes, 0, values.size() - 1));
  }

  nodes.swap(newNodes);
  roots.swap(newRoots);
  arraySize = values.size();
}

/*
=========================================================
Public Operations
=========================================================
*/

std::size_t HybridPersistentSegmentTree::rangeAdd(std::size_t left, std::size_t right,
                                                  ValueType value) {
  validateInitialized();
  validateRange(left, right);

  if (value == 0) {
    roots.push_back(roots.back());
    return roots.size() - 1;
  }

  std::size_t nextVersion = roots.size();
  const std::size_t checkpoint = nodes.size();
  std::vector<UndoRecord> undoLog;

  try {
    std::size_t currentRoot = roots.back();
    std::size_t newRoot =
        update(currentRoot, nextVersion, 0, arraySize - 1, left, right, value, undoLog);
    roots.push_back(newRoot);
  } catch (...) {
    nodes.resize(checkpoint);
    for (auto it = undoLog.rbegin(); it != undoLog.rend(); ++it) {
      nodes[it->nodeIndex] = it->state;
    }
    throw;
  }

  return roots.size() - 1;
}

HybridPersistentSegmentTree::ValueType
HybridPersistentSegmentTree::rangeSum(std::size_t version, std::size_t left,
                                      std::size_t right) const {
  validateInitialized();
  validateVersion(version);
  validateRange(left, right);

  return query(roots[version], version, 0, arraySize - 1, left, right, 0);
}

/*
=========================================================
Accessors
=========================================================
*/

std::size_t HybridPersistentSegmentTree::versionCount() const {
  return roots.size();
}

std::size_t HybridPersistentSegmentTree::size() const {
  return arraySize;
}

std::size_t HybridPersistentSegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t HybridPersistentSegmentTree::build(const std::vector<ValueType>& values,
                                               std::vector<Node>& arena, std::size_t segmentLeft,
                                               std::size_t segmentRight) {
  Node node;
  node.bufferSize = 0;
  node.baseLazy = 0;
  node.successorNode = noNode;
  node.successorVersion = 0;

  if (segmentLeft == segmentRight) {
    node.leftChild = noNode;
    node.rightChild = noNode;
    node.baseSum = values[segmentLeft];
    arena.push_back(node);
    return arena.size() - 1;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;
  node.leftChild = build(values, arena, segmentLeft, middle);
  node.rightChild = build(values, arena, middle + 1, segmentRight);
  node.baseSum = arena[node.leftChild].baseSum + arena[node.rightChild].baseSum;

  arena.push_back(node);
  return arena.size() - 1;
}

/*
=========================================================
Segment Arithmetic
=========================================================
*/

HybridPersistentSegmentTree::ValueType
HybridPersistentSegmentTree::segmentLength(std::size_t segmentLeft, std::size_t segmentRight) {
  return static_cast<ValueType>(segmentRight) - static_cast<ValueType>(segmentLeft) + 1;
}

/*
=========================================================
Hybrid Traversal & Access
=========================================================
*/

std::size_t HybridPersistentSegmentTree::resolveNode(std::size_t nodeIndex,
                                                     std::size_t version) const {
  std::size_t current = nodeIndex;
  while (current != noNode && nodes[current].successorNode != noNode &&
         version >= nodes[current].successorVersion) {
    current = nodes[current].successorNode;
  }
  return current;
}

void HybridPersistentSegmentTree::applyBuffer(ValueType& sum, ValueType& lazy, const Node& node,
                                              std::size_t version) const {
  sum = node.baseSum;
  lazy = node.baseLazy;
  for (std::size_t i = 0; i < node.bufferSize; ++i) {
    if (node.buffer[i].version <= version) {
      sum += node.buffer[i].deltaSum;
      lazy += node.buffer[i].deltaLazy;
    }
  }
}

HybridPersistentSegmentTree::ValueType
HybridPersistentSegmentTree::getSum(std::size_t nodeIndex, std::size_t version) const {
  if (nodeIndex == noNode) {
    return 0;
  }
  std::size_t resolved = resolveNode(nodeIndex, version);
  ValueType sum, lazy;
  applyBuffer(sum, lazy, nodes[resolved], version);
  return sum;
}

/*
=========================================================
Hybrid Mutation (Buffered Modification & Node Copying)
=========================================================
*/

std::size_t HybridPersistentSegmentTree::modifyNode(std::size_t nodeIndex, std::size_t version,
                                                    ValueType deltaSum, ValueType deltaLazy,
                                                    std::vector<UndoRecord>& undoLog) {
  std::size_t resolved = resolveNode(nodeIndex, version);

  if (nodes[resolved].bufferSize < BUFFER_CAPACITY) {
    undoLog.push_back({resolved, nodes[resolved]});
    // Buffer has space; just append the delta.
    std::size_t idx = nodes[resolved].bufferSize++;
    nodes[resolved].buffer[idx] = {version, deltaSum, deltaLazy};
    return resolved;
  }

  // Buffer is full. We must create a new node (path-copying style), flush
  // all historical buffered changes into its base fields, and apply the new delta.
  Node newNode;
  newNode.leftChild = nodes[resolved].leftChild;
  newNode.rightChild = nodes[resolved].rightChild;

  // Flush base + old buffer. The new node starts "fresh" from the latest state.
  ValueType currentSum, currentLazy;
  applyBuffer(currentSum, currentLazy, nodes[resolved], version); // version here is latest

  newNode.baseSum = currentSum + deltaSum;
  newNode.baseLazy = currentLazy + deltaLazy;
  newNode.bufferSize = 0;
  newNode.successorNode = noNode;
  newNode.successorVersion = 0;

  nodes.push_back(newNode);
  std::size_t newIndex = nodes.size() - 1;

  undoLog.push_back({resolved, nodes[resolved]});
  nodes[resolved].successorNode = newIndex;
  nodes[resolved].successorVersion = version;

  return newIndex;
}

/*
=========================================================
Hybrid Persistent Range Update
=========================================================
*/

std::size_t HybridPersistentSegmentTree::update(std::size_t nodeIndex, std::size_t version,
                                                std::size_t segmentLeft, std::size_t segmentRight,
                                                std::size_t queryLeft, std::size_t queryRight,
                                                ValueType value, std::vector<UndoRecord>& undoLog) {
  std::size_t resolved = resolveNode(nodeIndex, version);
  ValueType length = segmentLength(segmentLeft, segmentRight);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // Modify node with delta lazy. Returns either the same resolved index
    // (if buffer had space) or a new node index (if it was copied).
    return modifyNode(nodeIndex, version, value * length, value, undoLog);
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  // We are mutating children. If the current node's buffer is NOT full,
  // the mathematical model is complex: do we push down buffers?
  // Driscoll et al. says path-copying must cascade up.
  // We compute the new children.
  std::size_t newLeft = nodes[resolved].leftChild;
  std::size_t newRight = nodes[resolved].rightChild;

  if (queryLeft <= middle) {
    newLeft = update(nodes[resolved].leftChild, version, segmentLeft, middle, queryLeft, queryRight,
                     value, undoLog);
  }
  if (queryRight > middle) {
    newRight = update(nodes[resolved].rightChild, version, middle + 1, segmentRight, queryLeft,
                      queryRight, value, undoLog);
  }

  // If children changed structurally, or their sums changed, the parent must be updated.
  // The delta for the parent is exactly the delta contributed by the child update.
  ValueType oldSum = getSum(resolved, version - 1); // Sum before this update
  ValueType currentLeftSum = getSum(newLeft, version);
  ValueType currentRightSum = getSum(newRight, version);

  // The new required sum for THIS node is left + right + (lazy * length).
  // The lazy used here is the *latest* lazy of THIS node before any new delta.
  ValueType currentLazy;
  ValueType throwawaySum;
  applyBuffer(throwawaySum, currentLazy, nodes[resolved], version - 1);

  ValueType targetSum = currentLeftSum + currentRightSum + currentLazy * length;
  ValueType deltaSum = targetSum - oldSum;

  if (deltaSum == 0 && newLeft == nodes[resolved].leftChild &&
      newRight == nodes[resolved].rightChild) {
    return resolved;
  }

  // To preserve structural sharing, if we are cascading up, we CANNOT just buffer
  // structural child pointer changes. The buffer only holds Value deltas!
  // Therefore, if newLeft != leftChild or newRight != rightChild, we MUST path-copy
  // THIS node. We cannot use the buffer for structural changes.

  if (newLeft != nodes[resolved].leftChild || newRight != nodes[resolved].rightChild) {
    Node newNode;
    newNode.leftChild = newLeft;
    newNode.rightChild = newRight;

    // Flush old values
    ValueType latestSum, latestLazy;
    applyBuffer(latestSum, latestLazy, nodes[resolved], version - 1);

    newNode.baseSum = latestSum + deltaSum;
    newNode.baseLazy = latestLazy; // deltaLazy is 0 for parent cascade
    newNode.bufferSize = 0;
    newNode.successorNode = noNode;
    newNode.successorVersion = 0;

    nodes.push_back(newNode);
    std::size_t newIndex = nodes.size() - 1;

    undoLog.push_back({resolved, nodes[resolved]});
    nodes[resolved].successorNode = newIndex;
    nodes[resolved].successorVersion = version;
    return newIndex;
  }

  // If structural pointers didn't change (because child updates were absorbed
  // by child buffers), we can safely absorb the deltaSum into OUR buffer!
  return modifyNode(nodeIndex, version, deltaSum, 0, undoLog);
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

HybridPersistentSegmentTree::ValueType HybridPersistentSegmentTree::query(
    std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft, std::size_t segmentRight,
    std::size_t queryLeft, std::size_t queryRight, ValueType inheritedLazy) const {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }

  std::size_t resolved = resolveNode(nodeIndex, version);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    return getSum(resolved, version) + inheritedLazy * segmentLength(segmentLeft, segmentRight);
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;
  ValueType sum, lazy;
  applyBuffer(sum, lazy, nodes[resolved], version);
  ValueType nextLazy = inheritedLazy + lazy;

  return query(nodes[resolved].leftChild, version, segmentLeft, middle, queryLeft, queryRight,
               nextLazy) +
         query(nodes[resolved].rightChild, version, middle + 1, segmentRight, queryLeft, queryRight,
               nextLazy);
}

/*
=========================================================
Validation
=========================================================
*/

void HybridPersistentSegmentTree::validateInitialized() const {
  if (roots.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void HybridPersistentSegmentTree::validateVersion(std::size_t version) const {
  if (version >= roots.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void HybridPersistentSegmentTree::validateRange(std::size_t left, std::size_t right) const {
  if (arraySize == 0) {
    throw std::runtime_error("Tree is empty.");
  }
  if (left > right) {
    throw std::invalid_argument("Left index is greater than right index.");
  }
  if (right >= arraySize) {
    throw std::out_of_range("Range exceeds array size.");
  }
}

} // namespace valseg
