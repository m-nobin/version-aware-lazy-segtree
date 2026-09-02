#include <valseg/detail/checked_size.hpp>
#include <valseg/detail/validation.hpp>
#include <valseg/point_only_persistent_segment_tree.hpp>
#include <valseg/policy.hpp>

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
    newNodes.reserve(detail::canonicalNodeCount(values.size()));
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

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  std::size_t leftRoot = build(values, arena, segmentLeft, middle);
  std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);

  arena.push_back(Node{leftRoot, rightRoot, checkedAdd(arena[leftRoot].sum, arena[rightRoot].sum)});
  return arena.size() - 1;
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
  // Copy the node by value: appending copies below may reallocate the
  // arena and invalidate references into it.
  const Node current = nodes[nodeIndex];

  if (segmentLeft == segmentRight) {
    // Leaf: the recursion guards below only descend into segments that
    // intersect [queryLeft, queryRight], so this leaf is inside the range.
    // Copy it with the delta applied.
    nodes.push_back(Node{noNode, noNode, checkedAdd(current.sum, value)});
    return nodes.size() - 1;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  std::size_t newLeft = current.leftChild;
  std::size_t newRight = current.rightChild;

  // The defining property of this baseline: with no lazy tag there is no
  // way to stop at a fully covered node, so every child that intersects
  // [queryLeft, queryRight] is descended into, all the way to its leaves.
  // Children outside the range keep their old index and stay shared.
  if (queryLeft <= middle) {
    newLeft = pointOnlyUpdate(current.leftChild, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    newRight =
        pointOnlyUpdate(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  // The sum of the new node is the sum of its (new or shared) children.
  nodes.push_back(Node{newLeft, newRight, checkedAdd(nodes[newLeft].sum, nodes[newRight].sum)});
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

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  return checkedAdd(query(current.leftChild, segmentLeft, middle, queryLeft, queryRight),
                    query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight));
}

/*
=========================================================
Validation
=========================================================
*/

void PointOnlyPersistentSegmentTree::validateInitialized() const {
  detail::validateInitialized(roots.size());
}

void PointOnlyPersistentSegmentTree::validateVersion(std::size_t version) const {
  detail::validateVersion(version, roots.size());
}

void PointOnlyPersistentSegmentTree::validateRange(std::size_t left, std::size_t right) const {
  detail::validateNonEmpty(arraySize);
  detail::validateRange(arraySize, left, right);
}

} // namespace valseg
