#include <valseg/detail/checked_size.hpp>
#include <valseg/detail/validation.hpp>
#include <valseg/lazy_segment_tree.hpp>
#include <valseg/policy.hpp>

#include <stdexcept>

namespace valseg {

/*
=========================================================
Constructors
=========================================================
*/

LazySegmentTree::LazySegmentTree() : arraySize(0) {}

LazySegmentTree::LazySegmentTree(const std::vector<ValueType>& values) {
  initialize(values);
}

/*
=========================================================
Initialization
=========================================================
*/

void LazySegmentTree::initialize(const std::vector<ValueType>& values) {
  const detail::SumAddDomainGuard newNumericDomain(values);
  const std::size_t storageCount = detail::lazyStorageCount(values.size());
  std::vector<ValueType> newTree(storageCount, 0);
  std::vector<ValueType> newLazy(storageCount, 0);
  if (!values.empty()) {
    build(values, newTree, 0, 0, values.size() - 1);
  }
  tree.swap(newTree);
  lazy.swap(newLazy);
  arraySize = values.size();
  numericDomain = newNumericDomain;
}

/*
=========================================================
Public Operations
=========================================================
*/

void LazySegmentTree::rangeAdd(std::size_t left, std::size_t right, ValueType value) {
  validateRange(left, right);
  if (value == 0) {
    return;
  }
  const auto nextMagnitudeBound = numericDomain.validateRangeAdd(
      arraySize, left, right, value,
      [this](std::vector<ValueType>& values) { materialize(0, 0, arraySize - 1, 0, values); });
  static_cast<void>(validateUpdate(0, 0, arraySize - 1, left, right, value));
  update(0, 0, arraySize - 1, left, right, value);
  numericDomain.commit(nextMagnitudeBound);
}

LazySegmentTree::ValueType LazySegmentTree::rangeSum(std::size_t left, std::size_t right) const {
  validateRange(left, right);

  return query(0, 0, arraySize - 1, left, right, 0);
}

std::size_t LazySegmentTree::size() const {
  return arraySize;
}

/*
=========================================================
Build
=========================================================
*/

void LazySegmentTree::build(const std::vector<ValueType>& values, std::vector<ValueType>& sums,
                            std::size_t node, std::size_t segmentLeft, std::size_t segmentRight) {
  if (segmentLeft == segmentRight) {
    sums[node] = values[segmentLeft];
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  build(values, sums, 2 * node + 1, segmentLeft, middle);

  build(values, sums, 2 * node + 2, middle + 1, segmentRight);

  sums[node] = checkedAdd(sums[2 * node + 1], sums[2 * node + 2]);
}

/*
=========================================================
Update Preflight
=========================================================
*/

LazySegmentTree::ValueType
LazySegmentTree::validateUpdate(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                                std::size_t queryLeft, std::size_t queryRight,
                                ValueType value) const {
  const std::size_t length = detail::inclusiveLength(segmentLeft, segmentRight);
  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    static_cast<void>(checkedAdd(lazy[node], value));
    return SumAddPolicy::apply(value, tree[node], length);
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  ValueType leftSum = tree[2 * node + 1];
  ValueType rightSum = tree[2 * node + 2];
  if (queryLeft <= middle) {
    leftSum = validateUpdate(2 * node + 1, segmentLeft, middle, queryLeft, queryRight, value);
  }
  if (queryRight > middle) {
    rightSum = validateUpdate(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight, value);
  }
  return SumAddPolicy::apply(lazy[node], checkedAdd(leftSum, rightSum), length);
}

/*
=========================================================
Range Update
=========================================================
*/

void LazySegmentTree::update(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                             std::size_t queryLeft, std::size_t queryRight, ValueType value) {
  const std::size_t length = detail::inclusiveLength(segmentLeft, segmentRight);
  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    tree[node] = SumAddPolicy::apply(value, tree[node], length);
    lazy[node] = checkedAdd(lazy[node], value);
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

  if (queryLeft <= middle) {
    update(2 * node + 1, segmentLeft, middle, queryLeft, queryRight, value);
  }

  if (queryRight > middle) {
    update(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight, value);
  }

  tree[node] =
      SumAddPolicy::apply(lazy[node], checkedAdd(tree[2 * node + 1], tree[2 * node + 2]), length);
}

/*
=========================================================
Range Query
=========================================================
*/

LazySegmentTree::ValueType LazySegmentTree::query(std::size_t node, std::size_t segmentLeft,
                                                  std::size_t segmentRight, std::size_t queryLeft,
                                                  std::size_t queryRight,
                                                  ValueType inheritedLazy) const {
  if (segmentRight < queryLeft || segmentLeft > queryRight) {
    return 0;
  }

  if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
    return SumAddPolicy::apply(inheritedLazy, tree[node],
                               detail::inclusiveLength(segmentLeft, segmentRight));
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const ValueType nextLazy = checkedAdd(inheritedLazy, lazy[node]);

  return checkedAdd(query(2 * node + 1, segmentLeft, middle, queryLeft, queryRight, nextLazy),
                    query(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight, nextLazy));
}

void LazySegmentTree::materialize(std::size_t node, std::size_t segmentLeft,
                                  std::size_t segmentRight, ValueType inheritedLazy,
                                  std::vector<ValueType>& values) const {
  if (segmentLeft == segmentRight) {
    values.push_back(SumAddPolicy::apply(inheritedLazy, tree[node], 1));
    return;
  }

  const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
  const ValueType nextLazy = checkedAdd(inheritedLazy, lazy[node]);
  materialize(2 * node + 1, segmentLeft, middle, nextLazy, values);
  materialize(2 * node + 2, middle + 1, segmentRight, nextLazy, values);
}

/*
=========================================================
Validation
=========================================================
*/

void LazySegmentTree::validateRange(std::size_t left, std::size_t right) const {
  detail::validateNonEmpty(arraySize);
  detail::validateRange(arraySize, left, right);
}

} // namespace valseg
