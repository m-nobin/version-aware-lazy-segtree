#include <valseg/detail/checked_size.hpp>
#include <valseg/detail/validation.hpp>
#include <valseg/full_copy_persistent_segment_tree.hpp>
#include <valseg/policy.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

FullCopyPersistentSegmentTree::FullCopyPersistentSegmentTree() : arraySize(0) {}

FullCopyPersistentSegmentTree::FullCopyPersistentSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void FullCopyPersistentSegmentTree::initialize(const std::vector<ValueType>& values) {
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

std::size_t FullCopyPersistentSegmentTree::rangeAdd(std::size_t left, std::size_t right,
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
    const std::size_t newRoot = fullCopyUpdate(roots.back(), 0, arraySize - 1, left, right, value);
    roots.push_back(newRoot);
  } catch (...) {
    nodes.resize(checkpoint);
    throw;
  }

  return roots.size() - 1;
}

FullCopyPersistentSegmentTree::ValueType
FullCopyPersistentSegmentTree::rangeSum(std::size_t version, std::size_t left,
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

std::size_t FullCopyPersistentSegmentTree::versionCount() const {
  return roots.size();
}

std::size_t FullCopyPersistentSegmentTree::size() const {
  return arraySize;
}

std::size_t FullCopyPersistentSegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t FullCopyPersistentSegmentTree::build(const std::vector<ValueType>& values,
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
Full Copy Range Update
=========================================================
*/

std::size_t FullCopyPersistentSegmentTree::fullCopyUpdate(std::size_t nodeIndex,
                                                          std::size_t segmentLeft,
                                                          std::size_t segmentRight,
                                                          std::size_t queryLeft,
                                                          std::size_t queryRight, ValueType value) {
  // Copy the node by value: appending copies below may reallocate the
  // arena and invalidate references into it.
  const Node current = nodes[nodeIndex];

  if (segmentLeft == segmentRight) {
    // Base case: we are at a leaf. If it falls inside the update range,
    // add the delta; otherwise, just copy it exactly.
    ValueType newValue = current.sum;
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      newValue = checkedAdd(newValue, value);
    }
    nodes.push_back(Node{noNode, noNode, newValue});
    return nodes.size() - 1;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  // The defining property of this baseline: children are never reused.
  // Both sides are copied unconditionally, so every version owns 2n - 1 nodes.
  std::size_t newLeft =
      fullCopyUpdate(current.leftChild, segmentLeft, middle, queryLeft, queryRight, value);
  std::size_t newRight =
      fullCopyUpdate(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, value);

  // The sum of the new node is simply the sum of its newly created children.
  nodes.push_back(Node{newLeft, newRight, checkedAdd(nodes[newLeft].sum, nodes[newRight].sum)});
  return nodes.size() - 1;
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

FullCopyPersistentSegmentTree::ValueType
FullCopyPersistentSegmentTree::query(std::size_t nodeIndex, std::size_t segmentLeft,
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

void FullCopyPersistentSegmentTree::validateInitialized() const {
  detail::validateInitialized(roots.size());
}

void FullCopyPersistentSegmentTree::validateVersion(std::size_t version) const {
  detail::validateVersion(version, roots.size());
}

void FullCopyPersistentSegmentTree::validateRange(std::size_t left, std::size_t right) const {
  detail::validateNonEmpty(arraySize);
  detail::validateRange(arraySize, left, right);
}

} // namespace valseg
