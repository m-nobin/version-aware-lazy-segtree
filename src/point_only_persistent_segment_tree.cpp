#include <valseg/point_only_persistent_segment_tree.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

PointOnlyPersistentSegmentTree::PointOnlyPersistentSegmentTree() : arraySize(0) {}

PointOnlyPersistentSegmentTree::PointOnlyPersistentSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void PointOnlyPersistentSegmentTree::initialize(const std::vector<ValueType>& values) {
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

std::size_t PointOnlyPersistentSegmentTree::rangeAdd(std::size_t left, std::size_t right,
                                                     ValueType value) {
  validateInitialized();
  validateRange(left, right);

  if (value == 0) {
    const std::size_t latestRoot = roots.back();
    roots.push_back(latestRoot);
    return roots.size() - 1;
  }

  const std::size_t checkpoint = nodes.size();

  try {
    const std::size_t newRoot = pointOnlyUpdate(roots.back(), 0, arraySize - 1, left, right, value);
    roots.push_back(newRoot);
  } catch (...) {
    nodes.resize(checkpoint);
    throw;
  }

  return roots.size() - 1;
}

PointOnlyPersistentSegmentTree::ValueType
PointOnlyPersistentSegmentTree::rangeSum(std::size_t version, std::size_t left,
                                         std::size_t right) const {
  validateInitialized();
  validateVersion(version);
  validateRange(left, right);

  return query(roots[version], 0, arraySize - 1, left, right);
}

/*
=========================================================
Accessors
=========================================================
*/

std::size_t PointOnlyPersistentSegmentTree::versionCount() const {
  return roots.size();
}

std::size_t PointOnlyPersistentSegmentTree::size() const {
  return arraySize;
}

std::size_t PointOnlyPersistentSegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t PointOnlyPersistentSegmentTree::build(const std::vector<ValueType>& values,
                                                  std::vector<Node>& arena, std::size_t segmentLeft,
                                                  std::size_t segmentRight) {
  if (segmentLeft == segmentRight) {
    arena.push_back(Node{noNode, noNode, values[segmentLeft]});
    return arena.size() - 1;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;
  std::size_t leftRoot = build(values, arena, segmentLeft, middle);
  std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);

  arena.push_back(Node{leftRoot, rightRoot, arena[leftRoot].sum + arena[rightRoot].sum});
  return arena.size() - 1;
}

/*
=========================================================
Segment Arithmetic
=========================================================
*/

PointOnlyPersistentSegmentTree::ValueType
PointOnlyPersistentSegmentTree::segmentLength(std::size_t segmentLeft, std::size_t segmentRight) {
  return static_cast<ValueType>(segmentRight) - static_cast<ValueType>(segmentLeft) + 1;
}

/*
=========================================================
Point-Only Persistent Range Update
=========================================================
*/

std::size_t
PointOnlyPersistentSegmentTree::pointOnlyUpdate(std::size_t nodeIndex, std::size_t segmentLeft,
                                                std::size_t segmentRight, std::size_t queryLeft,
                                                std::size_t queryRight, ValueType value) {
  const Node current = nodes[nodeIndex];

  if (segmentLeft == segmentRight) {
    // Leaf node: falls within query range (guaranteed by recursion guards),
    // so we path-copy this leaf and add the update delta.
    nodes.push_back(Node{noNode, noNode, current.sum + value});
    return nodes.size() - 1;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  std::size_t newLeft = current.leftChild;
  std::size_t newRight = current.rightChild;

  // Since we have no lazy tags, we must unconditionally recurse all the way
  // to the leaves for any child that overlaps with [queryLeft, queryRight].
  if (queryLeft <= middle) {
    newLeft = pointOnlyUpdate(current.leftChild, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    newRight =
        pointOnlyUpdate(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  // The sum of the newly created parent is simply the sum of its children.
  nodes.push_back(Node{newLeft, newRight, nodes[newLeft].sum + nodes[newRight].sum});
  return nodes.size() - 1;
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

PointOnlyPersistentSegmentTree::ValueType
PointOnlyPersistentSegmentTree::query(std::size_t nodeIndex, std::size_t segmentLeft,
                                      std::size_t segmentRight, std::size_t queryLeft,
                                      std::size_t queryRight) const {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }

  const Node& current = nodes[nodeIndex];

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    return current.sum;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  return query(current.leftChild, segmentLeft, middle, queryLeft, queryRight) +
         query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight);
}

/*
=========================================================
Validation
=========================================================
*/

void PointOnlyPersistentSegmentTree::validateInitialized() const {
  if (roots.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void PointOnlyPersistentSegmentTree::validateVersion(std::size_t version) const {
  if (version >= roots.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void PointOnlyPersistentSegmentTree::validateRange(std::size_t left, std::size_t right) const {
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
