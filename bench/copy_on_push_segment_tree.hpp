#ifndef VALSEG_BENCH_COPY_ON_PUSH_SEGMENT_TREE_HPP
#define VALSEG_BENCH_COPY_ON_PUSH_SEGMENT_TREE_HPP

#include <valseg/detail/checked_size.hpp>
#include <valseg/detail/sum_add_domain.hpp>
#include <valseg/policy.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace valseg::bench {

/**
 * Path copying with copy-on-push: the textbook way to make a lazy segment
 * tree persistent.
 *
 * A non-persistent lazy segment tree pushes a node's tag into its children
 * before descending past it. Keeping that step and adding path copying is the
 * first thing an implementer reaches for, and it is correct: pushing writes
 * into children, published children are immutable, so the push copies them.
 *
 * That is the only difference from PersistentLazySegmentTree. Both store the
 * same 32-byte node under the same convention (a node's sum already includes
 * its own tag), both answer queries by accumulating tags on the way down, and
 * both short-circuit a zero delta to a shared root. So a difference in the
 * measurements is attributable to the tag policy and to nothing else, which
 * is the point of having this baseline at all: it is the alternative the
 * project rejected, measured rather than argued about.
 *
 * It lives under bench/ rather than include/valseg/ because it is a
 * measurement subject, not part of the library. Every campaign replays it
 * against the same cross-structure checksum as the seven library structures,
 * and the CTest smoke run does the same at n = 256.
 *
 * Numeric domain: every stored long long element, canonical segment sum,
 * retained lazy tag and evaluated arithmetic intermediate must be exactly
 * representable. An out-of-domain initialization, update or query throws
 * std::overflow_error; failed writes publish no version or node. Its O(log n)
 * update bound applies when a constant-time magnitude envelope proves this
 * domain; otherwise an O(n) read-only exact preflight runs first.
 */
class CopyOnPushSegmentTree {
public:
  using ValueType = long long;

  /**
   * @brief Build version 0 from an array.
   *
   * @param values Initial array; may be empty.
   * @throws std::overflow_error A canonical segment sum is not representable;
   *                             the previous versions are left unchanged.
   */
  void initialize(const std::vector<ValueType>& values) {
    const detail::SumAddDomainGuard newNumericDomain(values);
    std::vector<Node> newNodes;
    std::vector<std::size_t> newRoots;
    if (values.empty()) {
      newRoots.push_back(noNode);
    } else {
      newNodes.reserve(detail::canonicalNodeCount(values.size()));
      newRoots.push_back(build(values, newNodes, 0, values.size() - 1));
    }
    nodes.swap(newNodes);
    roots.swap(newRoots);
    arraySize = values.size();
    numericDomain = newNumericDomain;
  }

  /**
   * @brief Publish a new version with `value` added over [left, right].
   *
   * @param left  Left index, inclusive.
   * @param right Right index, inclusive.
   * @param value Delta to add.
   * @return Index of the published version.
   * @throws std::runtime_error   No version exists, or the structure is empty.
   * @throws std::invalid_argument left is greater than right.
   * @throws std::out_of_range     right is not smaller than size().
   * @throws std::overflow_error   The update leaves the numeric domain; no
   *                               version or node is published.
   */
  std::size_t rangeAdd(std::size_t left, std::size_t right, ValueType value) {
    validate(left, right);
    if (value == 0) {
      roots.push_back(roots.back());
      return roots.size() - 1;
    }
    const auto nextMagnitudeBound = numericDomain.validateRangeAdd(
        arraySize, left, right, value, [this](std::vector<ValueType>& values) {
          materialize(roots.back(), 0, arraySize - 1, 0, values);
        });
    const std::size_t checkpoint = nodes.size();
    try {
      const std::size_t newRoot = update(roots.back(), 0, arraySize - 1, left, right, value);
      roots.push_back(newRoot);
    } catch (...) {
      nodes.resize(checkpoint);
      throw;
    }
    numericDomain.commit(nextMagnitudeBound);
    return roots.size() - 1;
  }

  /**
   * @brief Sum over [left, right] as of a published version.
   *
   * @param version Version to read.
   * @param left    Left index, inclusive.
   * @param right   Right index, inclusive.
   * @return The historical sum.
   * @throws std::out_of_range version does not exist, or right is too large.
   * @throws std::overflow_error The exact requested sum is not representable.
   */
  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const {
    if (version >= roots.size()) {
      throw std::out_of_range("Invalid version number.");
    }
    validate(left, right);
    return query(roots[version], 0, arraySize - 1, left, right, 0);
  }

  /**
   * @brief Published versions.
   *
   * @return Version count, including version 0.
   */
  std::size_t versionCount() const {
    return roots.size();
  }

  /**
   * @brief Element count.
   *
   * @return Array size.
   */
  std::size_t size() const {
    return arraySize;
  }

  /**
   * @brief Retained nodes across every version.
   *
   * @return Arena size; multiply by 32 for the payload.
   */
  std::size_t nodeCount() const {
    return nodes.size();
  }

private:
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    ValueType sum;
    ValueType lazy;
  };

  // The same 32-byte layout as PersistentLazySegmentTree::Node, on purpose.
  static_assert(sizeof(std::size_t) != 8 || sizeof(Node) == 32,
                "CopyOnPushSegmentTree::Node is documented as 32 bytes on 64-bit targets.");

  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  std::vector<Node> nodes;
  std::vector<std::size_t> roots;
  std::size_t arraySize = 0;
  detail::SumAddDomainGuard numericDomain;

  static std::size_t span(std::size_t left, std::size_t right) {
    return detail::inclusiveLength(left, right);
  }

  static std::size_t build(const std::vector<ValueType>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight) {
    if (segmentLeft == segmentRight) {
      arena.push_back(Node{noNode, noNode, values[segmentLeft], 0});
      return arena.size() - 1;
    }
    const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
    const std::size_t leftRoot = build(values, arena, segmentLeft, middle);
    const std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);
    arena.push_back(
        Node{leftRoot, rightRoot, checkedAdd(arena[leftRoot].sum, arena[rightRoot].sum), 0});
    return arena.size() - 1;
  }

  /**
   * Copy `source`, add `tag` over a segment of `length` elements, and keep the
   * tag so the copy's descendants still inherit it. This is the push: it
   * allocates a node the non-pushed policy does not.
   */
  std::size_t pushInto(std::size_t source, ValueType tag, std::size_t length) {
    const Node child = nodes[source];
    nodes.push_back(Node{child.leftChild, child.rightChild,
                         SumAddPolicy::apply(tag, child.sum, length), checkedAdd(child.lazy, tag)});
    return nodes.size() - 1;
  }

  std::size_t update(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t queryLeft, std::size_t queryRight, ValueType value) {
    const Node current = nodes[nodeIndex];
    const std::size_t length = span(segmentLeft, segmentRight);

    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      nodes.push_back(Node{current.leftChild, current.rightChild,
                           SumAddPolicy::apply(value, current.sum, length),
                           checkedAdd(current.lazy, value)});
      return nodes.size() - 1;
    }

    const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);

    std::size_t leftChild = current.leftChild;
    std::size_t rightChild = current.rightChild;
    if (current.lazy != 0) {
      leftChild = pushInto(leftChild, current.lazy, span(segmentLeft, middle));
      rightChild = pushInto(rightChild, current.lazy, span(middle + 1, segmentRight));
    }

    std::size_t newLeft = leftChild;
    std::size_t newRight = rightChild;
    if (queryLeft <= middle) {
      newLeft = update(leftChild, segmentLeft, middle, queryLeft, queryRight, value);
    }
    if (queryRight > middle) {
      newRight = update(rightChild, middle + 1, segmentRight, queryLeft, queryRight, value);
    }

    // The tag was pushed, so the copied parent carries none and its sum is
    // exactly the two child sums.
    nodes.push_back(
        Node{newLeft, newRight, checkedAdd(nodes[newLeft].sum, nodes[newRight].sum), 0});
    return nodes.size() - 1;
  }

  ValueType query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight, ValueType inherited) const {
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return 0;
    }
    const Node& current = nodes[nodeIndex];
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      return SumAddPolicy::apply(inherited, current.sum, span(segmentLeft, segmentRight));
    }
    const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
    const ValueType next = checkedAdd(inherited, current.lazy);
    return checkedAdd(
        query(current.leftChild, segmentLeft, middle, queryLeft, queryRight, next),
        query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, next));
  }

  void materialize(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                   ValueType inherited, std::vector<ValueType>& values) const {
    const Node& current = nodes[nodeIndex];
    if (segmentLeft == segmentRight) {
      values.push_back(SumAddPolicy::apply(inherited, current.sum, 1));
      return;
    }
    const std::size_t middle = detail::midpoint(segmentLeft, segmentRight);
    const ValueType next = checkedAdd(inherited, current.lazy);
    materialize(current.leftChild, segmentLeft, middle, next, values);
    materialize(current.rightChild, middle + 1, segmentRight, next, values);
  }

  void validate(std::size_t left, std::size_t right) const {
    if (roots.empty()) {
      throw std::runtime_error("Tree has no versions.");
    }
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
};

} // namespace valseg::bench

#endif
