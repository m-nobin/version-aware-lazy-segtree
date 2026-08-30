#ifndef VALSEG_BENCH_ADAPTERS_HPP
#define VALSEG_BENCH_ADAPTERS_HPP

#include <valseg/buffered_path_copying_segment_tree.hpp>
#include <valseg/checkpointing_segment_tree.hpp>
#include <valseg/fat_node_persistent_segment_tree.hpp>
#include <valseg/full_copy_persistent_segment_tree.hpp>
#include <valseg/lazy_segment_tree.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/point_only_persistent_segment_tree.hpp>

#include <cstddef>
#include <type_traits>
#include <vector>

#include "copy_on_push_segment_tree.hpp"
#include "workloads.hpp"

namespace valseg::bench {

/**
 * Adapters give the seven persistent structures and the non-persistent control
 * one shape the driver can replay an operation stream against. They are
 * templates, not virtual bases: the hot loop must not pay for dispatch it is
 * trying to measure.
 *
 * LazySegmentTree needs its own adapter because it is genuinely a different
 * interface, not a variation on one: rangeSum takes two arguments instead of
 * three and is non-const, because a query pushes lazy tags. It is a
 * latest-state control, so it answers every query against the newest state
 * whatever version the stream names.
 */

/**
 * @brief Adapter for a structure with the (version, left, right) query shape.
 *
 * @tparam Tree One of the seven persistent structures.
 */
template <typename Tree> class PersistentAdapter {
public:
  /**
   * @brief Whether the structure answers historical queries.
   */
  static constexpr bool historical = true;

  /**
   * @brief Construct an adapter.
   *
   * @param checkpointInterval K; used only by CheckpointingSegmentTree.
   */
  explicit PersistentAdapter(std::size_t checkpointInterval) : interval(checkpointInterval) {}

  /**
   * @brief Build version 0.
   *
   * @param values Initial array.
   */
  void build(const std::vector<ValueType>& values) {
    if constexpr (std::is_same_v<Tree, CheckpointingSegmentTree>) {
      tree.initialize(values, interval);
    } else {
      tree.initialize(values);
    }
  }

  /**
   * @brief Apply one range add.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   * @param delta Value to add.
   */
  void update(std::size_t left, std::size_t right, ValueType delta) {
    tree.rangeAdd(left, right, delta);
  }

  /**
   * @brief Answer one range sum.
   *
   * @param version Version to read.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   * @return The sum.
   */
  ValueType query(std::size_t version, std::size_t left, std::size_t right) {
    return tree.rangeSum(version, left, right);
  }

  /**
   * @brief Retained records.
   *
   * @return Nodes, plus log entries for CheckpointingSegmentTree.
   */
  std::size_t nodes() const {
    return tree.nodeCount();
  }

  /**
   * @brief Retained payload in bytes, using each header's documented layout.
   *
   * Allocator overhead and RSS are not included; this is the payload the
   * structure is documented to keep.
   *
   * @return Retained bytes.
   */
  std::size_t bytes() const {
    if constexpr (std::is_same_v<Tree, CheckpointingSegmentTree>) {
      // nodeCount() mixes two record types: (checkpoints + 1) copies of the
      // 2n - 1 tree at 16 bytes a node, and one 24-byte log entry per update.
      const std::size_t treeNodes =
          tree.size() == 0 ? 0 : (tree.checkpointCount() + 1) * (2 * tree.size() - 1);
      const std::size_t entries = tree.nodeCount() - treeNodes;
      return treeNodes * 16 + entries * 24;
    } else if constexpr (std::is_same_v<Tree, PersistentLazySegmentTree> ||
                         std::is_same_v<Tree, CopyOnPushSegmentTree>) {
      // Identical node layout, on purpose: the two differ in tag policy only,
      // so retained bytes differ only by the node count each policy produces.
      return tree.nodeCount() * 32;
    } else if constexpr (std::is_same_v<Tree, BufferedPathCopyingSegmentTree>) {
      return tree.nodeCount() * 88;
    } else if constexpr (std::is_same_v<Tree, FatNodePersistentSegmentTree>) {
      return tree.nodeCount() * 128;
    } else {
      // FullCopyPersistentSegmentTree and PointOnlyPersistentSegmentTree:
      // two child indices and one sum.
      return tree.nodeCount() * 24;
    }
  }

  /**
   * @brief Published versions.
   *
   * @return Version count.
   */
  std::size_t versions() const {
    return tree.versionCount();
  }

private:
  Tree tree;
  std::size_t interval;
};

/**
 * @brief Adapter for the non-persistent control.
 */
class LazyAdapter {
public:
  /**
   * @brief The control answers no historical query.
   */
  static constexpr bool historical = false;

  /**
   * @brief Construct an adapter.
   *
   * @param checkpointInterval Ignored; present so the driver stays uniform.
   */
  explicit LazyAdapter(std::size_t checkpointInterval) {
    (void)checkpointInterval;
  }

  /**
   * @brief Build the tree.
   *
   * @param values Initial array.
   */
  void build(const std::vector<ValueType>& values) {
    tree.initialize(values);
    published = 1;
  }

  /**
   * @brief Apply one range add.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   * @param delta Value to add.
   */
  void update(std::size_t left, std::size_t right, ValueType delta) {
    tree.rangeAdd(left, right, delta);
    ++published;
  }

  /**
   * @brief Answer one range sum against the latest state.
   *
   * @param version Ignored: no earlier state is retained.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   * @return The sum in the latest state.
   */
  ValueType query(std::size_t version, std::size_t left, std::size_t right) {
    (void)version;
    return tree.rangeSum(left, right);
  }

  /**
   * @brief Retained slots.
   *
   * @return The 4n sum slots and 4n lazy slots the tree allocates, counted as
   *         4n nodes of one sum and one tag each.
   */
  std::size_t nodes() const {
    return 4 * tree.size();
  }

  /**
   * @brief Retained payload in bytes.
   *
   * @return 16 bytes per slot: one sum and one lazy tag.
   */
  std::size_t bytes() const {
    return nodes() * 16;
  }

  /**
   * @brief States the tree has passed through.
   *
   * @return One more than the number of updates applied; only the last is
   *         readable.
   */
  std::size_t versions() const {
    return published;
  }

private:
  LazySegmentTree tree;
  std::size_t published = 0;
};

} // namespace valseg::bench

#endif
