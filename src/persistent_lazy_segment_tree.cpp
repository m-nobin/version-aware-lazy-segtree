#include <valseg/detail/checked_size.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/policy.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

PersistentLazySegmentTree::PersistentLazySegmentTree() : arraySize(0) {}

PersistentLazySegmentTree::PersistentLazySegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void PersistentLazySegmentTree::initialize(const std::vector<ValueType>& values) {
  // Build replacement state separately so a failed build leaves the
  // currently published versions unchanged.
  std::vector<Node> newNodes;
  std::vector<std::size_t> newRoots;

  if (values.empty()) {
    newRoots.push_back(noNode);
  } else {
    // A segment tree over n leaves stores exactly 2 * n - 1 nodes.
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

std::size_t PersistentLazySegmentTree::rangeAdd(std::size_t left, std::size_t right,
                                                ValueType value) {
  validateInitialized();
  validateRange(left, right);

  if (value == 0) {
    // Nothing changes, so the new version shares the entire latest tree.
    const std::size_t latestRoot = roots.back();
    roots.push_back(latestRoot);
    return roots.size() - 1;
  }

  // Publish the new root only after the complete update succeeds; on
  // failure, roll the arena back to the checkpoint so no partial state
  // remains observable.
  const std::size_t checkpoint = nodes.size();

  try {
    const std::size_t newRoot = update(roots.back(), 0, arraySize - 1, left, right, value);
    roots.push_back(newRoot);
  } catch (...) {
    nodes.resize(checkpoint);
    throw;
  }

  return roots.size() - 1;
}

PersistentLazySegmentTree::ValueType PersistentLazySegmentTree::rangeSum(std::size_t version,
                                                                         std::size_t left,
                                                                         std::size_t right) const {
  validateInitialized();
  validateVersion(version);
  validateRange(left, right);

  return query(roots[version], 0, arraySize - 1, left, right, 0);
}

/*
=========================================================
Accessors
=========================================================
*/

std::size_t PersistentLazySegmentTree::versionCount() const {
  return roots.size();
}

std::size_t PersistentLazySegmentTree::size() const {
  return arraySize;
}

std::size_t PersistentLazySegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t PersistentLazySegmentTree::build(const std::vector<ValueType>& values,
                                             std::vector<Node>& arena, std::size_t segmentLeft,
                                             std::size_t segmentRight) {
  if (segmentLeft == segmentRight) {
    arena.push_back(Node{noNode, noNode, values[segmentLeft], 0});
    return arena.size() - 1;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  std::size_t leftRoot = build(values, arena, segmentLeft, middle);
  std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);

  arena.push_back(
      Node{leftRoot, rightRoot, checkedAdd(arena[leftRoot].sum, arena[rightRoot].sum), 0});

  return arena.size() - 1;
}

/*
=========================================================
Segment Arithmetic
=========================================================
*/

std::size_t PersistentLazySegmentTree::segmentLength(std::size_t segmentLeft,
                                                     std::size_t segmentRight) {
  return detail::inclusiveLength(segmentLeft, segmentRight);
}

/*
=========================================================
Persistent Range Update
=========================================================
*/

std::size_t PersistentLazySegmentTree::update(std::size_t nodeIndex, std::size_t segmentLeft,
                                              std::size_t segmentRight, std::size_t queryLeft,
                                              std::size_t queryRight, ValueType value) {
  // Copy the node by value: appending copies below may reallocate the
  // arena and invalidate references into it.
  const Node current = nodes[nodeIndex];

  const std::size_t length = segmentLength(segmentLeft, segmentRight);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // Full coverage: the node's own sum absorbs the delta and the lazy tag
    // defers it for descendants, so both shared children stay untouched.
    nodes.push_back(Node{current.leftChild, current.rightChild,
                         SumAddPolicy::apply(value, current.sum, length),
                         checkedAdd(current.lazy, value)});
    return nodes.size() - 1;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  std::size_t newLeft = current.leftChild;
  std::size_t newRight = current.rightChild;

  if (queryLeft <= middle) {
    newLeft = update(current.leftChild, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    newRight = update(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  // The copied parent keeps its own lazy tag. Child sums exclude that tag,
  // so the invariant sum = left.sum + right.sum + lazy * length
  // reconstructs the exact sum without pushing into shared children.
  nodes.push_back(
      Node{newLeft, newRight,
           SumAddPolicy::apply(current.lazy, checkedAdd(nodes[newLeft].sum, nodes[newRight].sum),
                               length),
           current.lazy});

  return nodes.size() - 1;
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

PersistentLazySegmentTree::ValueType
PersistentLazySegmentTree::query(std::size_t nodeIndex, std::size_t segmentLeft,
                                 std::size_t segmentRight, std::size_t queryLeft,
                                 std::size_t queryRight, ValueType inheritedLazy) const {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }

  const Node& current = nodes[nodeIndex];

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // The node's own lazy value is already included in its sum, so full
    // coverage adds only the lazy values inherited from ancestors.
    return SumAddPolicy::apply(inheritedLazy, current.sum,
                               segmentLength(segmentLeft, segmentRight));
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  // Lazy values are accumulated on the way down instead of pushed into
  // children because published nodes are immutable.
  const ValueType nextLazy = checkedAdd(inheritedLazy, current.lazy);

  return checkedAdd(
      query(current.leftChild, segmentLeft, middle, queryLeft, queryRight, nextLazy),
      query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, nextLazy));
}

/*
=========================================================
Validation
=========================================================
*/

void PersistentLazySegmentTree::validateInitialized() const {
  if (roots.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void PersistentLazySegmentTree::validateVersion(std::size_t version) const {
  if (version >= roots.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void PersistentLazySegmentTree::validateRange(std::size_t left, std::size_t right) const {
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
