#ifndef LAZY_SEGMENT_TREE_HPP
#define LAZY_SEGMENT_TREE_HPP

#include <vector>
#include <cstddef>
#include <stdexcept>

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
class LazySegmentTree
{
public:

    using ValueType = long long;

    LazySegmentTree();

    explicit LazySegmentTree(
        const std::vector<ValueType>& values);

    /**
     * @brief Build the segment tree.
     */
    void initialize(
        const std::vector<ValueType>& values);

    /**
     * @brief Add value to every element in [left, right].
     */
    void rangeAdd(
        std::size_t left,
        std::size_t right,
        ValueType value);

    /**
     * @brief Return the sum over [left, right].
     */
    ValueType rangeSum(
        std::size_t left,
        std::size_t right);

    /**
     * @brief Number of elements.
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

    void build(
        const std::vector<ValueType>& values,
        std::size_t node,
        std::size_t segmentLeft,
        std::size_t segmentRight);

    void push(
        std::size_t node,
        std::size_t segmentLeft,
        std::size_t segmentRight);

    void update(
        std::size_t node,
        std::size_t segmentLeft,
        std::size_t segmentRight,
        std::size_t queryLeft,
        std::size_t queryRight,
        ValueType value);

    ValueType query(
        std::size_t node,
        std::size_t segmentLeft,
        std::size_t segmentRight,
        std::size_t queryLeft,
        std::size_t queryRight);

    void validateRange(
        std::size_t left,
        std::size_t right) const;
};

#endif