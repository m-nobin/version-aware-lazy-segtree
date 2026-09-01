#include <valseg/checkpointing_segment_tree.hpp>
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

CheckpointingSegmentTree::CheckpointingSegmentTree() : interval(0), arraySize(0) {}

CheckpointingSegmentTree::CheckpointingSegmentTree(const std::vector<ValueType>& values,
                                                   std::size_t checkpointInterval)
    : interval(0), arraySize(0) {
  initialize(values, checkpointInterval);
}

/*
=========================================================
Initialization
=========================================================
*/

void CheckpointingSegmentTree::initialize(const std::vector<ValueType>& values,
                                          std::size_t checkpointInterval) {
  if (checkpointInterval == 0) {
    throw std::invalid_argument("Checkpoint interval must be positive.");
  }

  const detail::SumAddDomainGuard newNumericDomain(values);

  std::vector<Node> newLive;
  if (!values.empty()) {
    newLive.resize(detail::canonicalNodeCount(values.size()));
    build(values, newLive, 0, 0, values.size() - 1);
  }

  // Version 0 is always checkpointed so every version has a checkpoint at or
  // before it; for an empty array the checkpoint holds no nodes.
  std::vector<Checkpoint> newCheckpoints;
  newCheckpoints.push_back(Checkpoint{0, newLive});

  live.swap(newLive);
  checkpoints.swap(newCheckpoints);
  events.clear();
  interval = checkpointInterval;
  arraySize = values.size();
  numericDomain = newNumericDomain;
}

/*
=========================================================
Public Operations
=========================================================
*/

std::size_t CheckpointingSegmentTree::rangeAdd(std::size_t left, std::size_t right,
                                               ValueType value) {
  validateInitialized();
  validateRange(left, right);

  const std::size_t version = detail::checkedSizeAdd(events.size(), 1);
  const bool takeCheckpoint = version % interval == 0;
  detail::SumAddDomainGuard::Bound nextMagnitudeBound = 0;

  if (value != 0) {
    nextMagnitudeBound = numericDomain.validateRangeAdd(
        arraySize, left, right, value, [this](std::vector<ValueType>& values) {
          materialize(live, 0, 0, arraySize - 1, 0, values);
        });
    static_cast<void>(validateUpdate(live, 0, 0, arraySize - 1, left, right, value));
  }

  // Allocate first and mutate last, so a failed update publishes nothing:
  // the checkpoint is copied before the update and receives it below.
  events.push_back(UpdateEvent{left, right, value});
  if (takeCheckpoint) {
    try {
      checkpoints.push_back(Checkpoint{version, live});
    } catch (...) {
      events.pop_back();
      throw;
    }
  }

  if (value != 0) {
    update(live, 0, 0, arraySize - 1, left, right, value);
    if (takeCheckpoint) {
      update(checkpoints.back().nodes, 0, 0, arraySize - 1, left, right, value);
    }
    numericDomain.commit(nextMagnitudeBound);
  }

  return version;
}

CheckpointingSegmentTree::ValueType
CheckpointingSegmentTree::rangeSum(std::size_t version, std::size_t left, std::size_t right) const {
  validateInitialized();
  validateVersion(version);
  validateRange(left, right);

  if (version == events.size()) {
    return query(live, 0, 0, arraySize - 1, left, right, 0);
  }

  // Checkpoints sit exactly at the multiples of the interval, so the nearest
  // one at or before the version is checkpoints[version / interval].
  const Checkpoint& base = checkpoints[version / interval];

  ValueType sum = query(base.nodes, 0, 0, arraySize - 1, left, right, 0);

  // Replay: events[i] produced version i + 1, so the entries after the
  // checkpoint and up to the version are events[base.version, version).
  // Each adds its delta once per element it shares with [left, right].
  for (std::size_t i = base.version; i < version; ++i) {
    const UpdateEvent& event = events[i];
    const std::size_t overlapLeft = std::max(left, event.left);
    const std::size_t overlapRight = std::min(right, event.right);
    if (overlapLeft <= overlapRight) {
      sum = SumAddPolicy::apply(event.value, sum, segmentLength(overlapLeft, overlapRight));
    }
  }

  return sum;
}

/*
=========================================================
Accessors
=========================================================
*/

std::size_t CheckpointingSegmentTree::versionCount() const {
  return checkpoints.empty() ? 0 : detail::checkedSizeAdd(events.size(), 1);
}

std::size_t CheckpointingSegmentTree::size() const {
  return arraySize;
}

std::size_t CheckpointingSegmentTree::nodeCount() const {
  return detail::checkedSizeAdd(
      detail::checkedSizeAdd(live.size(),
                             detail::checkedSizeMultiply(checkpoints.size(), live.size())),
      events.size());
}

std::size_t CheckpointingSegmentTree::checkpointCount() const {
  return checkpoints.size();
}

/*
=========================================================
Layout Helpers
=========================================================
*/

std::size_t CheckpointingSegmentTree::segmentLength(std::size_t segmentLeft,
                                                    std::size_t segmentRight) {
  return detail::inclusiveLength(segmentLeft, segmentRight);
}

std::size_t CheckpointingSegmentTree::rightChild(std::size_t nodeIndex, std::size_t segmentLeft,
                                                 std::size_t middle) {
  // The left subtree over [segmentLeft, middle] occupies the
  // 2(middle - segmentLeft + 1) - 1 slots directly after nodeIndex, so the
  // right child comes right after them.
  return detail::checkedSizeAdd(
      nodeIndex, detail::checkedSizeMultiply(2, detail::inclusiveLength(segmentLeft, middle)));
}

/*
=========================================================
Build
=========================================================
*/

void CheckpointingSegmentTree::build(const std::vector<ValueType>& values, std::vector<Node>& nodes,
                                     std::size_t nodeIndex, std::size_t segmentLeft,
                                     std::size_t segmentRight) {
  if (segmentLeft == segmentRight) {
    nodes[nodeIndex] = Node{values[segmentLeft], 0};
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const std::size_t leftIndex = nodeIndex + 1;
  const std::size_t rightIndex = rightChild(nodeIndex, segmentLeft, middle);

  build(values, nodes, leftIndex, segmentLeft, middle);
  build(values, nodes, rightIndex, middle + 1, segmentRight);

  nodes[nodeIndex] = Node{checkedAdd(nodes[leftIndex].sum, nodes[rightIndex].sum), 0};
}

/*
=========================================================
In-Place Range Update
=========================================================
*/

void CheckpointingSegmentTree::update(std::vector<Node>& nodes, std::size_t nodeIndex,
                                      std::size_t segmentLeft, std::size_t segmentRight,
                                      std::size_t queryLeft, std::size_t queryRight,
                                      ValueType value) {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return;
  }

  Node& current = nodes[nodeIndex];

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    // Full coverage: the node's own sum absorbs the delta and the lazy tag
    // records it for the children, which are never pushed to.
    current.sum = SumAddPolicy::apply(value, current.sum, segmentLength(segmentLeft, segmentRight));
    current.lazy = checkedAdd(current.lazy, value);
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const std::size_t leftIndex = nodeIndex + 1;
  const std::size_t rightIndex = rightChild(nodeIndex, segmentLeft, middle);

  update(nodes, leftIndex, segmentLeft, middle, queryLeft, queryRight, value);
  update(nodes, rightIndex, middle + 1, segmentRight, queryLeft, queryRight, value);

  // Child sums exclude this node's tag, so sum = left.sum + right.sum + lazy * length.
  current.sum =
      SumAddPolicy::apply(current.lazy, checkedAdd(nodes[leftIndex].sum, nodes[rightIndex].sum),
                          segmentLength(segmentLeft, segmentRight));
}

CheckpointingSegmentTree::ValueType CheckpointingSegmentTree::validateUpdate(
    const std::vector<Node>& nodes, std::size_t nodeIndex, std::size_t segmentLeft,
    std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight, ValueType value) {
  const Node& current = nodes[nodeIndex];
  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    static_cast<void>(checkedAdd(current.lazy, value));
    return SumAddPolicy::apply(value, current.sum, segmentLength(segmentLeft, segmentRight));
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const std::size_t leftIndex = nodeIndex + 1;
  const std::size_t rightIndex = rightChild(nodeIndex, segmentLeft, middle);
  ValueType leftSum = nodes[leftIndex].sum;
  ValueType rightSum = nodes[rightIndex].sum;
  if (queryLeft <= middle) {
    leftSum = validateUpdate(nodes, leftIndex, segmentLeft, middle, queryLeft, queryRight, value);
  }
  if (queryRight > middle) {
    rightSum =
        validateUpdate(nodes, rightIndex, middle + 1, segmentRight, queryLeft, queryRight, value);
  }
  return SumAddPolicy::apply(current.lazy, checkedAdd(leftSum, rightSum),
                             segmentLength(segmentLeft, segmentRight));
}

/*
=========================================================
Read-Only Query
=========================================================
*/

CheckpointingSegmentTree::ValueType
CheckpointingSegmentTree::query(const std::vector<Node>& nodes, std::size_t nodeIndex,
                                std::size_t segmentLeft, std::size_t segmentRight,
                                std::size_t queryLeft, std::size_t queryRight,
                                ValueType inheritedLazy) {
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
  // children, so queries never write to the tree.
  const ValueType nextLazy = checkedAdd(inheritedLazy, current.lazy);

  return checkedAdd(
      query(nodes, nodeIndex + 1, segmentLeft, middle, queryLeft, queryRight, nextLazy),
      query(nodes, rightChild(nodeIndex, segmentLeft, middle), middle + 1, segmentRight, queryLeft,
            queryRight, nextLazy));
}

void CheckpointingSegmentTree::materialize(const std::vector<Node>& nodes, std::size_t nodeIndex,
                                           std::size_t segmentLeft, std::size_t segmentRight,
                                           ValueType inheritedLazy,
                                           std::vector<ValueType>& values) {
  const Node& current = nodes[nodeIndex];
  if (segmentLeft == segmentRight) {
    values.push_back(SumAddPolicy::apply(inheritedLazy, current.sum, 1));
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const ValueType nextLazy = checkedAdd(inheritedLazy, current.lazy);
  materialize(nodes, nodeIndex + 1, segmentLeft, middle, nextLazy, values);
  materialize(nodes, rightChild(nodeIndex, segmentLeft, middle), middle + 1, segmentRight, nextLazy,
              values);
}

/*
=========================================================
Validation
=========================================================
*/

void CheckpointingSegmentTree::validateInitialized() const {
  if (checkpoints.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void CheckpointingSegmentTree::validateVersion(std::size_t version) const {
  if (version > events.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void CheckpointingSegmentTree::validateRange(std::size_t left, std::size_t right) const {
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
