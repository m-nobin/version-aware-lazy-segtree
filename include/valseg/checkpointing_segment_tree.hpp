#ifndef VALSEG_CHECKPOINTING_SEGMENT_TREE_HPP
#define VALSEG_CHECKPOINTING_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Checkpointing segment tree (benchmark baseline).
 *
 * Supports the same operation model as PersistentLazySegmentTree:
 *  - Range Addition Updates on the latest version only
 *  - Range Sum Queries against any published version
 *
 * This baseline is the event-sourcing strategy of snapshotting databases and
 * has no structural sharing at all. It keeps one ephemeral lazy segment tree
 * for the latest version, an append-only log of every update, and a full
 * copy of the ephemeral tree (a checkpoint) at version 0 and at every
 * version that is a multiple of the checkpoint interval K. A historical
 * query reads the nearest checkpoint at or before the requested version and
 * replays the logged updates between them, projecting each one onto the
 * queried range. It exists to measure what structural sharing buys against
 * the industry-standard alternative: with K = 1 every version is a full copy
 * (approximating the FullCopyPersistentSegmentTree cost model, plus the log);
 * with K -> infinity only version 0 is stored and every historical query
 * replays the whole log.
 *
 * The ephemeral tree stores exactly 2n - 1 nodes; a node holds only its
 * segment sum and lazy tag (two 8-byte fields, 16 bytes per node). Every
 * checkpoint is a copy of that vector. A log entry stores the update range
 * and delta (three 8-byte fields, 24 bytes per entry).
 *
 * Time Complexity, for a tree over n elements with checkpoint interval K:
 *  Build:            O(n)
 *  Range Add:        O(log n) on the ephemeral tree, plus O(n) to copy it on
 *                    every K-th version: amortized O(log n + n / K)
 *  Zero-delta Add:   O(1), logging the entry without touching the tree,
 *                    plus the same O(n) checkpoint on every K-th version
 *  Latest Sum:       O(log n) on the ephemeral tree
 *  Historical Sum:   O(log n + K), one query on the nearest checkpoint plus
 *                    up to K - 1 logged entries; K = 1 gives O(log n) and
 *                    K -> infinity gives O(log n + U) after U updates
 *
 * Retained space after U updates (zero-delta ones included):
 * U log entries plus (floor(U / K) + 2)(2n - 1) tree nodes — the ephemeral
 * tree, the version-0 checkpoint and one checkpoint per K versions — i.e.
 * O(U + n + nU / K).
 */
class CheckpointingSegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct a tree with no versions.
   */
  CheckpointingSegmentTree();

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values             Initial array; may be empty.
   * @param checkpointInterval K: a full copy of the tree is stored at every
   *                           version that is a multiple of K. K = 1 keeps a
   *                           full copy per version; a value above the number
   *                           of updates keeps only the version-0 copy.
   *
   * @throws std::invalid_argument  checkpointInterval is 0.
   */
  explicit CheckpointingSegmentTree(const std::vector<ValueType>& values,
                                    std::size_t checkpointInterval = 500);

  /**
   * @brief Build version 0, replacing all previous versions, checkpoints and
   *        log entries.
   *
   * Replacement state is built first and swapped in only after the build
   * succeeds, so a failed initialization leaves the tree unchanged.
   *
   * @param values             Initial array; may be empty.
   * @param checkpointInterval K, as for the constructor.
   *
   * @throws std::invalid_argument  checkpointInterval is 0.
   */
  void initialize(const std::vector<ValueType>& values, std::size_t checkpointInterval = 500);

  /**
   * @brief Add value to every element in [left, right] of the latest version.
   *
   * Publishes exactly one new version. Every update, zero-delta ones
   * included, appends one log entry; a non-zero value also updates the
   * ephemeral tree in place. When the new version number is a multiple of
   * the checkpoint interval a full copy of the ephemeral tree is stored as
   * well, so checkpoints sit exactly at multiples of K regardless of the
   * delta. A failed update publishes no version, appends no log entry and
   * stores no checkpoint.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   * @param value Value to add.
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right] in the given version.
   *
   * The latest version is read from the ephemeral tree. Any other version is
   * read from the nearest checkpoint at or before it, adding for every
   * logged update between the two the delta times the number of elements it
   * shares with [left, right]. Queries allocate nothing and mutate nothing.
   *
   * @param version Version to query.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   * @return Sum of the elements in [left, right] of that version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::out_of_range      Invalid version number.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  /**
   * @brief Number of published versions.
   *
   * @return Number of published versions, or zero before initialization.
   */
  std::size_t versionCount() const;

  /**
   * @brief Number of elements in every version.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const;

  /**
   * @brief Total number of retained records: tree nodes plus log entries.
   *
   * Counts every node of the ephemeral tree and of every checkpoint
   * (2n - 1 each) plus every log entry, so it equals
   * (checkpointCount() + 1)(2n - 1) + U after U updates, zero-delta ones
   * included, on a non-empty array and 0 on an empty one. Read-only
   * evidence for the checkpoint-schedule tests and memory benchmarks; tree
   * nodes are 16 bytes and log entries 24 bytes.
   *
   * @return Number of nodes retained by the structure.
   */
  std::size_t nodeCount() const;

  /**
   * @brief Number of stored checkpoints, including the version-0 one.
   *
   * Equals floor(U / K) + 1 after U updates; the version-0 checkpoint of an
   * empty array holds no nodes.
   *
   * @return Number of stored full-tree checkpoints.
   */
  std::size_t checkpointCount() const;

private:
  /**
   * @brief Node of the ephemeral tree and of every checkpoint copy.
   *
   * lazy is already included in the node's own sum and not in either
   * child's sum, the same invariant as PersistentLazySegmentTree.
   */
  struct Node {
    ValueType sum;
    ValueType lazy;
  };

  // The node layout is part of the memory-benchmark contract: one sum and
  // one lazy tag with no padding, 16 bytes on 64-bit targets.
  static_assert(sizeof(Node) == 2 * sizeof(ValueType),
                "Node must pack one sum and one lazy tag without padding");

  /**
   * @brief One logged update.
   */
  struct UpdateEvent {
    std::size_t left;
    std::size_t right;
    ValueType value;
  };

  static_assert(sizeof(UpdateEvent) == 2 * sizeof(std::size_t) + sizeof(ValueType),
                "UpdateEvent must pack a range and a delta without padding");

  /**
   * @brief Full copy of the ephemeral tree as of the given version.
   */
  struct Checkpoint {
    std::size_t version;
    std::vector<Node> nodes;
  };

  /**
   * Ephemeral tree of the latest version, 2n - 1 nodes. Node i's left child
   * is i + 1 and its right child follows the left subtree, so a segment over
   * m elements occupies the 2m - 1 slots starting at its own node.
   */
  std::vector<Node> live;

  /**
   * Append-only update log; events[i] produced version i + 1.
   */
  std::vector<UpdateEvent> events;

  /**
   * Checkpoints in increasing version order; the first is version 0, so an
   * initialized tree always has at least one.
   */
  std::vector<Checkpoint> checkpoints;

  std::size_t interval;
  std::size_t arraySize;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  /**
   * Number of elements in [segmentLeft, segmentRight] as a ValueType.
   */
  static ValueType segmentLength(std::size_t segmentLeft, std::size_t segmentRight);

  /**
   * Slot of the right child of the node at nodeIndex covering
   * [segmentLeft, ...] whose left child covers [segmentLeft, middle]; the
   * left child is always nodeIndex + 1.
   */
  static std::size_t rightChild(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t middle);

  static void build(const std::vector<ValueType>& values, std::vector<Node>& nodes,
                    std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight);

  static void update(std::vector<Node>& nodes, std::size_t nodeIndex, std::size_t segmentLeft,
                     std::size_t segmentRight, std::size_t queryLeft, std::size_t queryRight,
                     ValueType value);

  static ValueType query(const std::vector<Node>& nodes, std::size_t nodeIndex,
                         std::size_t segmentLeft, std::size_t segmentRight, std::size_t queryLeft,
                         std::size_t queryRight, ValueType inheritedLazy);

  void validateInitialized() const;
  void validateVersion(std::size_t version) const;
  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
