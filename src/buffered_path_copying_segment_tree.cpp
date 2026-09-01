#include <valseg/buffered_path_copying_segment_tree.hpp>
#include <valseg/detail/checked_size.hpp>
#include <valseg/policy.hpp>

#include <algorithm>
#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

BufferedPathCopyingSegmentTree::BufferedPathCopyingSegmentTree() : arraySize(0) {}

BufferedPathCopyingSegmentTree::BufferedPathCopyingSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void BufferedPathCopyingSegmentTree::initialize(const std::vector<ValueType>& values) {
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
  bufferedThisUpdate.clear();
}

/*
=========================================================
Public Operations
=========================================================
*/

std::size_t BufferedPathCopyingSegmentTree::rangeAdd(std::size_t left, std::size_t right,
                                                     ValueType value) {
  validateInitialized();
  validateRange(left, right);

  if (value == 0) {
    const std::size_t latestRoot = roots.back();
    roots.push_back(latestRoot);
    return roots.size() - 1;
  }

  const std::size_t version = roots.size();
  const std::size_t checkpoint = nodes.size();
  bufferedThisUpdate.clear();

  try {
    const std::size_t newRoot = update(roots.back(), version, 0, arraySize - 1, left, right, value);
    roots.push_back(newRoot);
  } catch (...) {
    // Pop the entries appended in place, then drop the copied nodes. Only
    // pre-existing nodes are ever buffered, but popping first keeps the
    // rollback valid even for an index at or beyond the checkpoint.
    for (const std::size_t nodeIndex : bufferedThisUpdate) {
      --nodes[nodeIndex].bufferSize;
    }
    nodes.resize(checkpoint);
    throw;
  }

  return roots.size() - 1;
}

BufferedPathCopyingSegmentTree::ValueType
BufferedPathCopyingSegmentTree::rangeSum(std::size_t version, std::size_t left,
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

std::size_t BufferedPathCopyingSegmentTree::versionCount() const {
  return roots.size();
}

std::size_t BufferedPathCopyingSegmentTree::size() const {
  return arraySize;
}

std::size_t BufferedPathCopyingSegmentTree::nodeCount() const {
  return nodes.size();
}

/*
=========================================================
Build
=========================================================
*/

std::size_t BufferedPathCopyingSegmentTree::build(const std::vector<ValueType>& values,
                                                  std::vector<Node>& arena, std::size_t segmentLeft,
                                                  std::size_t segmentRight) {
  if (segmentLeft == segmentRight) {
    arena.push_back(Node{noNode, noNode, values[segmentLeft], 0, 0, {}});
    return arena.size() - 1;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  std::size_t leftRoot = build(values, arena, segmentLeft, middle);
  std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);

  arena.push_back(Node{leftRoot,
                       rightRoot,
                       checkedAdd(arena[leftRoot].baseSum, arena[rightRoot].baseSum),
                       0,
                       0,
                       {}});
  return arena.size() - 1;
}

/*
=========================================================
Segment Arithmetic
=========================================================
*/

std::size_t BufferedPathCopyingSegmentTree::segmentLength(std::size_t segmentLeft,
                                                          std::size_t segmentRight) {
  return detail::inclusiveLength(segmentLeft, segmentRight);
}

/*
=========================================================
Versioned Node Reads
=========================================================
*/

BufferedPathCopyingSegmentTree::Snapshot
BufferedPathCopyingSegmentTree::snapshotAt(const Node& node, std::size_t version) {
  // Base fields plus every buffered entry tagged with this version or an
  // older one; entries appended by later updates are invisible here.
  Snapshot snapshot{node.baseSum, node.baseLazy};
  for (std::size_t i = 0; i < node.bufferSize; ++i) {
    if (node.buffer[i].version <= version) {
      snapshot.sum = checkedAdd(snapshot.sum, node.buffer[i].deltaSum);
      snapshot.lazy = checkedAdd(snapshot.lazy, node.buffer[i].deltaLazy);
    }
  }
  return snapshot;
}

/*
=========================================================
Buffered Modification
=========================================================
*/

std::size_t BufferedPathCopyingSegmentTree::modify(std::size_t nodeIndex, std::size_t version,
                                                   ValueType deltaSum, ValueType deltaLazy) {
  const Snapshot latest = snapshotAt(nodes[nodeIndex], version);
  const ValueType nextSum = checkedAdd(latest.sum, deltaSum);
  const ValueType nextLazy = checkedAdd(latest.lazy, deltaLazy);
  if (nodes[nodeIndex].bufferSize < bufferCapacity) {
    // Record the node before touching it so a failed record leaves nothing
    // to undo; then append the entry in place, tagged with the new version.
    bufferedThisUpdate.push_back(nodeIndex);
    Node& node = nodes[nodeIndex];
    node.buffer[node.bufferSize] = Modification{version, deltaSum, deltaLazy};
    ++node.bufferSize;
    return nodeIndex;
  }

  // Full buffer: copy the node by value (push_back may reallocate the arena),
  // flush every entry into the copy's base fields and start it with an empty
  // buffer. The old node keeps its buffer for the versions that reach it.
  const Node current = nodes[nodeIndex];
  nodes.push_back(Node{current.leftChild, current.rightChild, nextSum, nextLazy, 0, {}});
  return nodes.size() - 1;
}

/*
=========================================================
Buffered Persistent Range Update
=========================================================
*/

std::size_t BufferedPathCopyingSegmentTree::update(std::size_t nodeIndex, std::size_t version,
                                                   std::size_t segmentLeft,
                                                   std::size_t segmentRight, std::size_t queryLeft,
                                                   std::size_t queryRight, ValueType value) {
  const std::size_t length = segmentLength(segmentLeft, segmentRight);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // Full coverage: the node's own sum absorbs the delta and the lazy tag
    // defers it for descendants, so both shared children stay untouched.
    return modify(nodeIndex, version, checkedMultiply(value, checkedLength(length)), value);
  }

  // Copy the node by value: appending copies below may reallocate the
  // arena and invalidate references into it.
  const Node current = nodes[nodeIndex];

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  std::size_t newLeft = current.leftChild;
  std::size_t newRight = current.rightChild;

  if (queryLeft <= middle) {
    newLeft = update(current.leftChild, version, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    newRight =
        update(current.rightChild, version, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  // Partial coverage adds value to every element of the overlap and nothing
  // to this node's lazy tag.
  const std::size_t overlap =
      segmentLength(std::max(segmentLeft, queryLeft), std::min(segmentRight, queryRight));
  const ValueType deltaSum = checkedMultiply(value, checkedLength(overlap));

  if (newLeft == current.leftChild && newRight == current.rightChild) {
    // Both children absorbed their share in place, so this node can try to
    // do the same; a full buffer copies it with the unchanged child indices.
    return modify(nodeIndex, version, deltaSum, 0);
  }

  // A child was copied. Its new index cannot be buffered, so this node is
  // copied too, with every entry flushed (all of them predate this version)
  // and the delta applied; the copy keeps its own lazy tag.
  const Snapshot latest = snapshotAt(current, version);
  nodes.push_back(Node{newLeft, newRight, checkedAdd(latest.sum, deltaSum), latest.lazy, 0, {}});
  return nodes.size() - 1;
}

/*
=========================================================
Read-Only Historical Query
=========================================================
*/

BufferedPathCopyingSegmentTree::ValueType BufferedPathCopyingSegmentTree::query(
    std::size_t nodeIndex, std::size_t version, std::size_t segmentLeft, std::size_t segmentRight,
    std::size_t queryLeft, std::size_t queryRight, ValueType inheritedLazy) const {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }

  const Node& current = nodes[nodeIndex];
  const Snapshot state = snapshotAt(current, version);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // The node's own lazy value is already included in its sum, so full
    // coverage adds only the lazy values inherited from ancestors.
    return SumAddPolicy::apply(inheritedLazy, state.sum, segmentLength(segmentLeft, segmentRight));
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  // Lazy values are accumulated on the way down instead of pushed into
  // children because children are shared with other versions.
  const ValueType nextLazy = checkedAdd(inheritedLazy, state.lazy);

  return checkedAdd(
      query(current.leftChild, version, segmentLeft, middle, queryLeft, queryRight, nextLazy),
      query(current.rightChild, version, middle + 1, segmentRight, queryLeft, queryRight,
            nextLazy));
}

/*
=========================================================
Validation
=========================================================
*/

void BufferedPathCopyingSegmentTree::validateInitialized() const {
  if (roots.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void BufferedPathCopyingSegmentTree::validateVersion(std::size_t version) const {
  if (version >= roots.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void BufferedPathCopyingSegmentTree::validateRange(std::size_t left, std::size_t right) const {
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
