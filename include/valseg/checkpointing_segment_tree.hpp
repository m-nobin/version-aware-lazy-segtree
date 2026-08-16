#ifndef VALSEG_CHECKPOINTING_SEGMENT_TREE_HPP
#define VALSEG_CHECKPOINTING_SEGMENT_TREE_HPP

#include <valseg/lazy_segment_tree.hpp>

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Rebuild / Checkpointing Segment Tree (Benchmarking Baseline).
 *
 * This structure serves as the Tier 1 baseline representing standard industry
 * event-sourcing and periodic snapshotting databases.
 *
 * It abandons structural sharing. Instead, it maintains:
 * 1. A highly optimized ephemeral `LazySegmentTree` for the latest version.
 * 2. An append-only log of every update event.
 * 3. Periodic full deep-copies (checkpoints) of the tree every K updates.
 *
 * Updates are extremely fast (standard O(log n)), but historical queries
 * are slow: they must find the nearest preceding checkpoint, make a temporary
 * copy, and replay the event log forward O(K) times to reconstruct the past.
 *
 * Time Complexity:
 *  Build:            O(n)
 *  Range Add:        O(log n) (plus amortized O(n/K) for checkpoints)
 *  Historical Sum:   O(n + K log n) (deep copy checkpoint + replay K events)
 */
class CheckpointingSegmentTree {
public:
  using ValueType = long long;

  CheckpointingSegmentTree();

  explicit CheckpointingSegmentTree(const std::vector<ValueType>& values);

  void initialize(const std::vector<ValueType>& values);

  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right);

  std::size_t versionCount() const;

  std::size_t size() const;

  // Estimates memory usage based on checkpoints and log size for benchmarking parity.
  std::size_t nodeCount() const;

private:
  // K interval: Take a full snapshot every 500 versions.
  // 500 is chosen to provide a realistic database-like log-replay balance.
  static constexpr std::size_t CHECKPOINT_INTERVAL = 500;

  struct UpdateEvent {
    std::size_t left;
    std::size_t right;
    ValueType value;
  };

  struct Checkpoint {
    std::size_t version;
    LazySegmentTree tree;
  };

  std::vector<UpdateEvent> eventLog;
  std::vector<Checkpoint> checkpoints;

  // The actively mutated tree representing the "latest" state.
  LazySegmentTree latestTree;

  std::size_t currentVersion;
  std::size_t arraySize;

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
