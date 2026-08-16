#include <valseg/checkpointing_segment_tree.hpp>

#include <algorithm>
#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

CheckpointingSegmentTree::CheckpointingSegmentTree() : currentVersion(0), arraySize(0) {}

CheckpointingSegmentTree::CheckpointingSegmentTree(const std::vector<ValueType>& values)
    : currentVersion(0), arraySize(0) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void CheckpointingSegmentTree::initialize(const std::vector<ValueType>& values) {
  // If the underlying ephemeral tree fails to initialize, the state remains clean
  // because we haven't mutated this object's fields yet.
  LazySegmentTree newLatest(values);

  latestTree = std::move(newLatest);
  arraySize = values.size();
  currentVersion = 0;

  eventLog.clear();
  checkpoints.clear();

  // A size 0 array handles gracefully but shouldn't be checkpointed.
  if (arraySize > 0) {
    // Version 0 is the base snapshot.
    // The copy assignment operator of LazySegmentTree ensures a deep copy of its nodes.
    checkpoints.push_back({0, latestTree});
  }
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

  if (value == 0) {
    // Zero deltas do not modify the ephemeral tree.
    // We just log an empty event to maintain version parity without creating a checkpoint.
    eventLog.push_back({left, right, 0});
    return ++currentVersion;
  }

  // Act on the ephemeral tree. This provides the fast O(log n) update.
  latestTree.rangeAdd(left, right, value);

  eventLog.push_back({left, right, value});
  ++currentVersion;

  // Periodically snapshot the full ephemeral tree to bound replay cost.
  if (currentVersion % CHECKPOINT_INTERVAL == 0) {
    checkpoints.push_back({currentVersion, latestTree});
  }

  return currentVersion;
}

CheckpointingSegmentTree::ValueType
CheckpointingSegmentTree::rangeSum(std::size_t version, std::size_t left, std::size_t right) {
  validateInitialized();
  validateVersion(version);
  validateRange(left, right);

  // Fast path: if the user asks for the absolute latest version, we can just
  // query the live ephemeral tree directly, completely avoiding copy/replay overhead.
  if (version == currentVersion) {
    return latestTree.rangeSum(left, right);
  }

  // Find the nearest checkpoint preceding or exactly matching the requested version.
  // Because checkpoints are added sequentially, we can binary search or reverse iterate.
  auto it = std::upper_bound(checkpoints.begin(), checkpoints.end(), version,
                             [](std::size_t v, const Checkpoint& cp) { return v < cp.version; });

  // 'it' points to the first checkpoint strictly greater than version.
  // So 'it - 1' is the nearest preceding checkpoint.
  --it;

  // Make a temporary deep copy of the checkpointed ephemeral tree.
  // This is the massive O(n) penalty of the checkpointing strategy.
  LazySegmentTree tempTree = it->tree;

  // Replay all events from the log that occurred after this checkpoint
  // up to the requested version.
  for (std::size_t i = it->version; i < version; ++i) {
    const UpdateEvent& ev = eventLog[i];
    if (ev.value != 0) {
      tempTree.rangeAdd(ev.left, ev.right, ev.value);
    }
  }

  return tempTree.rangeSum(left, right);
}

/*
=========================================================
Accessors
=========================================================
*/

std::size_t CheckpointingSegmentTree::versionCount() const {
  if (arraySize == 0)
    return 0;
  return currentVersion + 1;
}

std::size_t CheckpointingSegmentTree::size() const {
  return arraySize;
}

std::size_t CheckpointingSegmentTree::nodeCount() const {
  if (arraySize == 0)
    return 0;

  // A LazySegmentTree over N elements has exactly 2N - 1 nodes.
  std::size_t nodesPerTree = 2 * arraySize - 1;

  // 1 node for the live latestTree + N nodes per checkpoint.
  std::size_t totalTreeNodes = nodesPerTree + (checkpoints.size() * nodesPerTree);

  // Event logs act as pseudo-nodes for memory comparison. We consider each UpdateEvent
  // to be roughly equivalent to allocating a node in terms of space overhead.
  return totalTreeNodes + eventLog.size();
}

/*
=========================================================
Validation
=========================================================
*/

void CheckpointingSegmentTree::validateInitialized() const {
  if (arraySize == 0 && eventLog.empty() && checkpoints.empty()) {
    throw std::runtime_error("Tree has no versions.");
  }
}

void CheckpointingSegmentTree::validateVersion(std::size_t version) const {
  if (version > currentVersion) {
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
