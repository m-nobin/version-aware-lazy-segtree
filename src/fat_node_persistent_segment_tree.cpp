#include <valseg/fat_node_persistent_segment_tree.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

FatNodePersistentSegmentTree::FatNodePersistentSegmentTree() : arraySize(0) {}

FatNodePersistentSegmentTree::FatNodePersistentSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void FatNodePersistentSegmentTree::initialize(const std::vector<ValueType>& values) {
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

std::size_t FatNodePersistentSegmentTree::rangeAdd(std::size_t left, std::size_t right,
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
    update(roots.back(), nextVersion, 0, arraySize - 1, left, right, value, undoLog);
    // The root might have split during the update, so we resolve it.
    roots.push_back(resolveNode(roots.back(), nextVersion));
  } catch (...) {
    // 1. Shrink the arena to discard any nodes appended due to splitting.
    nodes.resize(checkpoint);
    // 2. Rollback in-place mutations (fat node history appends) in reverse order.
    for (auto it = undoLog.rbegin(); it != undoLog.rend(); ++it) {
      nodes[it->nodeIndex] = it->state;
    }
    throw;
  }

  return roots.size() - 1;
}

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::rangeSum(std::size_t version, std::size_t left,
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

std::size_t FatNodePersistentSegmentTree::versionCount() const {
  return roots.size();
}

std::size_t FatNodePersistentSegmentTree::size() const {
  return arraySize;
}

std::size_t FatNodePersistentSegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t FatNodePersistentSegmentTree::build(const std::vector<ValueType>& values,
                                                std::vector<Node>& arena, std::size_t segmentLeft,
                                                std::size_t segmentRight) {
  Node node;
  node.sumCount = 1;
  node.lazyCount = 1;
  node.lazys[0] = {0, 0};
  node.successorNode = noNode;
  node.successorVersion = 0;

  if (segmentLeft == segmentRight) {
    node.leftChild = noNode;
    node.rightChild = noNode;
    node.sums[0] = {0, values[segmentLeft]};
    arena.push_back(node);
    return arena.size() - 1;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;
  node.leftChild = build(values, arena, segmentLeft, middle);
  node.rightChild = build(values, arena, middle + 1, segmentRight);
  node.sums[0] = {0, arena[node.leftChild].sums[0].value + arena[node.rightChild].sums[0].value};

  arena.push_back(node);
  return arena.size() - 1;
}

/*
=========================================================
Segment Arithmetic
=========================================================
*/

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::segmentLength(std::size_t segmentLeft, std::size_t segmentRight) {
  return static_cast<ValueType>(segmentRight) - static_cast<ValueType>(segmentLeft) + 1;
}

/*
=========================================================
Fat-Node Traversal & Access
=========================================================
*/

std::size_t FatNodePersistentSegmentTree::resolveNode(std::size_t nodeIndex,
                                                      std::size_t version) const {
  std::size_t current = nodeIndex;
  while (current != noNode && nodes[current].successorNode != noNode &&
         version >= nodes[current].successorVersion) {
    current = nodes[current].successorNode;
  }
  return current;
}

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::getSum(std::size_t nodeIndex, std::size_t version) const {
  if (nodeIndex == noNode) {
    return 0;
  }
  std::size_t resolved = resolveNode(nodeIndex, version);
  const Node& node = nodes[resolved];

  for (int i = static_cast<int>(node.sumCount) - 1; i >= 0; --i) {
    if (node.sums[i].version <= version) {
      return node.sums[i].value;
    }
  }
  return 0;
}

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::getLazy(std::size_t nodeIndex, std::size_t version) const {
  if (nodeIndex == noNode) {
    return 0;
  }
  std::size_t resolved = resolveNode(nodeIndex, version);
  const Node& node = nodes[resolved];

  for (int i = static_cast<int>(node.lazyCount) - 1; i >= 0; --i) {
    if (node.lazys[i].version <= version) {
      return node.lazys[i].value;
    }
  }
  return 0;
}

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::getLatestSum(std::size_t resolvedNodeIndex) const {
  const Node& node = nodes[resolvedNodeIndex];
  if (node.sumCount == 0)
    return 0;
  return node.sums[node.sumCount - 1].value;
}

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::getLatestLazy(std::size_t resolvedNodeIndex) const {
  const Node& node = nodes[resolvedNodeIndex];
  if (node.lazyCount == 0)
    return 0;
  return node.lazys[node.lazyCount - 1].value;
}

/*
=========================================================
Fat-Node Mutation (Appending / Splitting)
=========================================================
*/

void FatNodePersistentSegmentTree::appendSumAndLazy(std::size_t nodeIndex, std::size_t version,
                                                    ValueType sumVal, ValueType lazyVal,
                                                    bool updateLazy,
                                                    std::vector<UndoRecord>& undoLog) {
  std::size_t resolved = resolveNode(nodeIndex, version);
  undoLog.push_back({resolved, nodes[resolved]});

  bool sumFull = (nodes[resolved].sumCount == HISTORY_CAPACITY);
  bool lazyFull = updateLazy && (nodes[resolved].lazyCount == HISTORY_CAPACITY);

  if (sumFull || lazyFull) {
    Node newNode;
    newNode.leftChild = nodes[resolved].leftChild;
    newNode.rightChild = nodes[resolved].rightChild;
    newNode.sumCount = 1;
    newNode.sums[0] = {version, sumVal};
    newNode.lazyCount = 1;
    newNode.lazys[0] = {version, lazyVal};
    newNode.successorNode = noNode;
    newNode.successorVersion = 0;

    nodes.push_back(newNode);
    std::size_t newIndex = nodes.size() - 1;

    nodes[resolved].successorNode = newIndex;
    nodes[resolved].successorVersion = version;
  } else {
    nodes[resolved].sums[nodes[resolved].sumCount++] = {version, sumVal};
    if (updateLazy) {
      nodes[resolved].lazys[nodes[resolved].lazyCount++] = {version, lazyVal};
    }
  }
}

/*
=========================================================
Fat-Node Persistent Range Update
=========================================================
*/

FatNodePersistentSegmentTree::ValueType
FatNodePersistentSegmentTree::update(std::size_t nodeIndex, std::size_t version,
                                     std::size_t segmentLeft, std::size_t segmentRight,
                                     std::size_t queryLeft, std::size_t queryRight, ValueType value,
                                     std::vector<UndoRecord>& undoLog) {

  std::size_t resolved = resolveNode(nodeIndex, version);
  ValueType currentSum = getLatestSum(resolved);
  ValueType currentLazy = getLatestLazy(resolved);
  ValueType length = segmentLength(segmentLeft, segmentRight);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    ValueType newSum = currentSum + value * length;
    ValueType newLazy = currentLazy + value;
    appendSumAndLazy(nodeIndex, version, newSum, newLazy, true, undoLog);
    return newSum;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;
  ValueType leftSum = getSum(nodes[resolved].leftChild, version);
  ValueType rightSum = getSum(nodes[resolved].rightChild, version);

  if (queryLeft <= middle) {
    leftSum = update(nodes[resolved].leftChild, version, segmentLeft, middle, queryLeft, queryRight,
                     value, undoLog);
  }
  if (queryRight > middle) {
    rightSum = update(nodes[resolved].rightChild, version, middle + 1, segmentRight, queryLeft,
                      queryRight, value, undoLog);
  }

  ValueType newSum = leftSum + rightSum + currentLazy * length;
  appendSumAndLazy(nodeIndex, version, newSum, currentLazy, false, undoLog);
  return newSum;
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

FatNodePersistentSegmentTree::ValueType FatNodePersistentSegmentTree::query(
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
  ValueType nextLazy = inheritedLazy + getLazy(resolved, version);

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

void FatNodePersistentSegmentTree::validateInitialized() const {
  if (roots.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void FatNodePersistentSegmentTree::validateVersion(std::size_t version) const {
  if (version >= roots.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void FatNodePersistentSegmentTree::validateRange(std::size_t left, std::size_t right) const {
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
