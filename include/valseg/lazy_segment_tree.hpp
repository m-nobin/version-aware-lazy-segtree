#ifndef VALSEG_LAZY_SEGMENT_TREE_HPP
#define VALSEG_LAZY_SEGMENT_TREE_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Ordinary Lazy Segment Tree.
 *
 * Supports:
 *  - Range Addition Updates
 *  - Range Sum Queries
 *
 * This implementation is NOT persistent.
 * Updates modify the current tree directly.
 *
 * Time Complexity:
 *  Build:      O(n)
 *  Range Add:  O(log n)
 *  Range Sum:  O(log n)
 *
 * Numeric domain: values model mathematical signed integers in a long long
 * representation. Initialization and updates require every canonical segment
 * sum, retained lazy tag and evaluated arithmetic intermediate to be
 * representable. Queries require the same of the requested sum. Otherwise
 * std::overflow_error is thrown, and a failed initialization or update leaves
 * the prior state unchanged.
 */
class LazySegmentTree {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct an empty tree.
   */
  LazySegmentTree();

  /**
   * @brief Construct a tree over an initial array.
   *
   * @param values Initial array; an empty array leaves the tree empty.
   *
   * @throws std::overflow_error A canonical segment sum is not representable.
   */
  explicit LazySegmentTree(const std::vector<ValueType>& values);

  /**
   * @brief Build the segment tree, discarding all previous state.
   *
   * @param values Initial array; an empty array leaves the tree empty.
   *
   * @throws std::overflow_error A canonical segment sum is not representable;
   *                             the previous state is left unchanged.
   */
  void initialize(const std::vector<ValueType>& values);

  /**
   * @brief Add value to every element in [left, right].
   *
   * Mutates the current tree in place; no previous state is retained.
   *
   * @param left  Left index of the updated range (inclusive).
   * @param right Right index of the updated range (inclusive).
   * @param value Value added to every element of the range.
   *
   * @throws std::runtime_error     The tree is empty.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   * @throws std::overflow_error    The update leaves the numeric domain; the
   *                                tree remains unchanged.
   */
  void rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right].
   *
   * The query is allocation-free and does not push or mutate lazy state.
   *
   * @param left  Left index of the queried range (inclusive).
   * @param right Right index of the queried range (inclusive).
   *
   * @return Sum over [left, right] in the current state.
   *
   * @throws std::runtime_error     The tree is empty.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   * @throws std::overflow_error    The exact requested sum is not representable.
   */
  ValueType rangeSum(std::size_t left, std::size_t right) const;

  /**
   * @brief Number of elements.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const;

private:
  std::vector<ValueType> tree;

  std::vector<ValueType> lazy;

  std::size_t arraySize;

  /*
  ============================================
  Internal Functions
  ============================================
  */

  static void build(const std::vector<ValueType>& values, std::vector<ValueType>& sums,
                    std::size_t node, std::size_t segmentLeft, std::size_t segmentRight);

  ValueType validateUpdate(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                           std::size_t queryLeft, std::size_t queryRight, ValueType value) const;

  void update(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
              std::size_t queryLeft, std::size_t queryRight, ValueType value);

  ValueType query(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight, ValueType inheritedLazy) const;

  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
