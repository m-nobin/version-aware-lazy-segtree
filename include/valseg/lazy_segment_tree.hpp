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
   */
  explicit LazySegmentTree(const std::vector<ValueType>& values);

  /**
   * @brief Build the segment tree, discarding all previous state.
   *
   * @param values Initial array; an empty array leaves the tree empty.
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
   */
  void rangeAdd(std::size_t left, std::size_t right, ValueType value);

  /**
   * @brief Return the sum over [left, right].
   *
   * Not const: the query pushes pending lazy values into the children it
   * visits. This is the one interface difference from every persistent
   * structure in this repository, whose rangeSum takes a version and is const.
   *
   * @param left  Left index of the queried range (inclusive).
   * @param right Right index of the queried range (inclusive).
   *
   * @return Sum over [left, right] in the current state.
   *
   * @throws std::runtime_error     The tree is empty.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  ValueType rangeSum(std::size_t left, std::size_t right);

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

  void build(const std::vector<ValueType>& values, std::size_t node, std::size_t segmentLeft,
             std::size_t segmentRight);

  void push(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight);

  void update(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
              std::size_t queryLeft, std::size_t queryRight, ValueType value);

  ValueType query(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight);

  void validateRange(std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
