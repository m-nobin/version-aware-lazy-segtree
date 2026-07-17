#include <valseg/lazy_segment_tree.hpp>

/*
=========================================================
Constructors
=========================================================
*/

LazySegmentTree::LazySegmentTree()
    : arraySize(0)
{
}

LazySegmentTree::LazySegmentTree(
    const std::vector<ValueType>& values)
{
    initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void LazySegmentTree::initialize(
    const std::vector<ValueType>& values)
{
    arraySize = values.size();

    tree.assign(4 * arraySize, 0);
    lazy.assign(4 * arraySize, 0);

    if (arraySize > 0)
    {
        build(values, 0, 0, arraySize - 1);
    }
}

/*
=========================================================
Public Operations
=========================================================
*/

void LazySegmentTree::rangeAdd(
    std::size_t left,
    std::size_t right,
    ValueType value)
{
    validateRange(left, right);

    update(
        0,
        0,
        arraySize - 1,
        left,
        right,
        value);
}

LazySegmentTree::ValueType
LazySegmentTree::rangeSum(
    std::size_t left,
    std::size_t right)
{
    validateRange(left, right);

    return query(
        0,
        0,
        arraySize - 1,
        left,
        right);
}

std::size_t LazySegmentTree::size() const
{
    return arraySize;
}

/*
=========================================================
Build
=========================================================
*/

void LazySegmentTree::build(
    const std::vector<ValueType>& values,
    std::size_t node,
    std::size_t segmentLeft,
    std::size_t segmentRight)
{
    if (segmentLeft == segmentRight)
    {
        tree[node] = values[segmentLeft];
        return;
    }

    std::size_t middle =
        (segmentLeft + segmentRight) / 2;

    build(values,
          2 * node + 1,
          segmentLeft,
          middle);

    build(values,
          2 * node + 2,
          middle + 1,
          segmentRight);

    tree[node] =
        tree[2 * node + 1] +
        tree[2 * node + 2];
}

/*
=========================================================
Lazy Push
=========================================================
*/

void LazySegmentTree::push(
    std::size_t node,
    std::size_t segmentLeft,
    std::size_t segmentRight)
{
    if (lazy[node] == 0)
        return;

    tree[node] +=
        (segmentRight - segmentLeft + 1)
        * lazy[node];

    if (segmentLeft != segmentRight)
    {
        lazy[2 * node + 1] += lazy[node];
        lazy[2 * node + 2] += lazy[node];
    }

    lazy[node] = 0;
}

/*
=========================================================
Range Update
=========================================================
*/

void LazySegmentTree::update(
    std::size_t node,
    std::size_t segmentLeft,
    std::size_t segmentRight,
    std::size_t queryLeft,
    std::size_t queryRight,
    ValueType value)
{
    push(node, segmentLeft, segmentRight);

    if (segmentRight < queryLeft ||
        segmentLeft > queryRight)
    {
        return;
    }

    if (queryLeft <= segmentLeft &&
        segmentRight <= queryRight)
    {
        lazy[node] += value;

        push(node,
             segmentLeft,
             segmentRight);

        return;
    }

    std::size_t middle =
        (segmentLeft + segmentRight) / 2;

    update(
        2 * node + 1,
        segmentLeft,
        middle,
        queryLeft,
        queryRight,
        value);

    update(
        2 * node + 2,
        middle + 1,
        segmentRight,
        queryLeft,
        queryRight,
        value);

    tree[node] =
        tree[2 * node + 1] +
        tree[2 * node + 2];
}

/*
=========================================================
Range Query
=========================================================
*/

LazySegmentTree::ValueType
LazySegmentTree::query(
    std::size_t node,
    std::size_t segmentLeft,
    std::size_t segmentRight,
    std::size_t queryLeft,
    std::size_t queryRight)
{
    push(node, segmentLeft, segmentRight);

    if (segmentRight < queryLeft ||
        segmentLeft > queryRight)
    {
        return 0;
    }

    if (queryLeft <= segmentLeft &&
        segmentRight <= queryRight)
    {
        return tree[node];
    }

    std::size_t middle =
        (segmentLeft + segmentRight) / 2;

    return
        query(
            2 * node + 1,
            segmentLeft,
            middle,
            queryLeft,
            queryRight)
        +
        query(
            2 * node + 2,
            middle + 1,
            segmentRight,
            queryLeft,
            queryRight);
}

/*
=========================================================
Validation
=========================================================
*/

void LazySegmentTree::validateRange(
    std::size_t left,
    std::size_t right) const
{
    if (arraySize == 0)
    {
        throw std::runtime_error(
            "Tree is empty.");
    }

    if (left > right)
    {
        throw std::invalid_argument(
            "Left index is greater than right index.");
    }

    if (right >= arraySize)
    {
        throw std::out_of_range(
            "Range exceeds array size.");
    }
}