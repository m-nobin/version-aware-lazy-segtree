#include <valseg/fat_node_persistent_segment_tree.hpp>

#include <algorithm>
#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

FatNodePersistentSegmentTree::FatNodePersistentSegmentTree() : arraySize(0), treeHeight(0) {}

FatNodePersistentSegmentTree::FatNodePersistentSegmentTree(const std::vector<ValueType>& values)
    : arraySize(0), treeHeight(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void FatNodePersistentSegmentTree::initialize(const std::vector<ValueType>& values) {
  // Build replacement state separately so a failed build leaves the
  // currently published versions unchanged.
  std::vector<Node> newNodes;
  std::vector<std::size_t> newRoots;

  if (values.empty()) {
    newRoots.push_back(noNode);
  } else {
    // A segment tree over n leaves stores exactly 2 * n - 1 nodes.
    newNodes.reserve(2 * values.size() - 1);
    newRoots.push_back(build(values, newNodes, 0, values.size() - 1));
  }

  std::size_t height = 0;
  while ((static_cast<std::size_t>(1) << height) < values.size()) {
    ++height;
  }

  nodes.swap(newNodes);
  roots.swap(newRoots);
  arraySize = values.size();
  treeHeight = height;
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
    // Nothing changes, so the new version shares the entire latest tree.
    const std::size_t latestRoot = roots.back();
    roots.push_back(latestRoot);
    return roots.size() - 1;
  }

  // Unlike path copying, an update mutates existing nodes in place by
  // appending states, so a half-finished update could not be undone by
  // shrinking the arena. Instead, every allocation the update could need
  // is made here, before the first mutation: from this point on nothing
  // below can throw, and a failed reservation leaves the tree untouched.
  reserveForUpdate();

  const std::size_t version = roots.size();
  const std::size_t newRoot = update(roots.back(), version, 0, arraySize - 1, left, right, value);
  roots.push_back(newRoot);

  return version;
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
  Node node{};
  node.count = 1;

  if (segmentLeft == segmentRight) {
    node.history[0] = Modification{0, noNode, noNode, values[segmentLeft], 0};
    arena.push_back(node);
    return arena.size() - 1;
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  std::size_t leftRoot = build(values, arena, segmentLeft, middle);
  std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);

  node.history[0] = Modification{
      0, leftRoot, rightRoot, arena[leftRoot].history[0].sum + arena[rightRoot].history[0].sum, 0};
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
  // Indices always fit in ValueType, so the arithmetic stays in the signed type.
  return static_cast<ValueType>(segmentRight) - static_cast<ValueType>(segmentLeft) + 1;
}

/*
=========================================================
Fat-Node State Access
=========================================================
*/

const FatNodePersistentSegmentTree::Modification&
FatNodePersistentSegmentTree::stateAt(const Node& node, std::size_t version) {
  // Stamps are strictly increasing, so the state in effect at `version` is
  // the last one whose stamp is not greater than it. Callers only pass
  // nodes that are live at `version`, so the creation state (slot 0) is
  // always a candidate and the search never lands before it.
  const auto end = node.history.begin() + static_cast<std::ptrdiff_t>(node.count);
  const auto after = std::upper_bound(
      node.history.begin(), end, version,
      [](std::size_t stamp, const Modification& state) { return stamp < state.version; });
  return *(after - 1);
}

const FatNodePersistentSegmentTree::Modification&
FatNodePersistentSegmentTree::latestState(const Node& node) {
  return node.history[node.count - 1];
}

/*
=========================================================
Fat-Node Mutation (Appending / Copying)
=========================================================
*/

void FatNodePersistentSegmentTree::reserveForUpdate() {
  // Every visited node may overflow into a fresh copy, and a range update
  // visits at most 4 nodes per level. Growing geometrically keeps the
  // amortized cost per appended node constant, as push_back would.
  const std::size_t visitLimit = 4 * (treeHeight + 1);
  if (nodes.capacity() - nodes.size() < visitLimit) {
    nodes.reserve(std::max(2 * nodes.capacity(), nodes.size() + visitLimit));
  }
  if (roots.size() == roots.capacity()) {
    roots.reserve(2 * roots.capacity());
  }
}

std::size_t FatNodePersistentSegmentTree::record(std::size_t nodeIndex, const Modification& state) {
  if (nodes[nodeIndex].count < HISTORY_CAPACITY) {
    // Fat-node append: the node keeps its index, so its parent's existing
    // child index stays valid for the new version.
    Node& node = nodes[nodeIndex];
    node.history[node.count] = state;
    ++node.count;
    return nodeIndex;
  }

  // Node copying: the full node stays as the history of older versions and
  // a fresh node carries the new state. The caller (the parent, or
  // rangeAdd for the root) records the fresh index for this version.
  Node fresh{};
  fresh.count = 1;
  fresh.history[0] = state;
  nodes.push_back(fresh);
  return nodes.size() - 1;
}

/*
=========================================================
Fat-Node Persistent Range Update
=========================================================
*/

std::size_t FatNodePersistentSegmentTree::update(std::size_t nodeIndex, std::size_t version,
                                                 std::size_t segmentLeft, std::size_t segmentRight,
                                                 std::size_t queryLeft, std::size_t queryRight,
                                                 ValueType value) {
  // Copy the latest state by value: appending fresh nodes below may
  // reallocate the arena and invalidate references into it. Updates only
  // apply to the latest version, so the latest state is the one to extend.
  Modification next = latestState(nodes[nodeIndex]);
  next.version = version;

  const ValueType length = segmentLength(segmentLeft, segmentRight);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // Full coverage: the node's own sum absorbs the delta and the lazy tag
    // defers it for descendants, so both children stay untouched.
    next.sum += value * length;
    next.lazy += value;
    return record(nodeIndex, next);
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  // A child that overflows returns a fresh index, which this node's new
  // state records; a child that appends in place keeps its index.
  if (queryLeft <= middle) {
    next.leftChild =
        update(next.leftChild, version, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    next.rightChild =
        update(next.rightChild, version, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  // Child sums exclude this node's lazy tag, so the invariant
  // sum = left.sum + right.sum + lazy * length reconstructs the exact sum.
  next.sum = latestState(nodes[next.leftChild]).sum + latestState(nodes[next.rightChild]).sum +
             next.lazy * length;

  return record(nodeIndex, next);
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

  const Modification& current = stateAt(nodes[nodeIndex], version);

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // The node's own lazy value is already included in its sum, so full
    // coverage adds only the lazy values inherited from ancestors.
    return current.sum + inheritedLazy * segmentLength(segmentLeft, segmentRight);
  }

  std::size_t middle = (segmentLeft + segmentRight) / 2;

  // Lazy values are accumulated on the way down instead of pushed into
  // children because published states are immutable.
  const ValueType nextLazy = inheritedLazy + current.lazy;

  return query(current.leftChild, version, segmentLeft, middle, queryLeft, queryRight, nextLazy) +
         query(current.rightChild, version, middle + 1, segmentRight, queryLeft, queryRight,
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
