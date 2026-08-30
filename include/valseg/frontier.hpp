#ifndef VALSEG_FRONTIER_HPP
#define VALSEG_FRONTIER_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Executable definitions of the update frontier of a segment tree over
 *        the canonical partition (docs/proof.md, section 10).
 *
 * For an array of n elements and an inclusive range [left, right], the
 * update recursion of every structure in this repository visits the canonical
 * nodes whose interval meets the range and whose proper ancestors are all
 * partially covered. Those nodes split into the partially covered ones, where
 * the recursion continues, and the fully covered ones, where it stops (the
 * canonical decomposition of the range). This header computes:
 *
 *  - frontierCounts: both counts by running the recursion, which is the
 *    definition of the visited frontier F;
 *  - closedFormFrontier: F from the depths of three lowest common ancestors
 *    and the turns of the two boundary paths, the closed form the proof
 *    derives;
 *  - intersectingNodes: the number of canonical nodes meeting the range, the
 *    records a tagless path-copying update allocates;
 *  - maximumFrontier: the worst case 4h - 3 for a tree of height h >= 2;
 *  - frontierSum: the exact sum of F over a family of ranges given by a
 *    counting function, from which an expected frontier follows without
 *    enumerating ranges;
 *  - PushCountingModel: the push frontier P of the copy-on-push strategy, the
 *    number of partially covered visited nodes whose retained tag is not the
 *    identity at the time of the update.
 *
 * Every function is exact for every n >= 1 and every valid range; nothing here
 * is a bound unless its name says so. The tests in tests/frontier_test.cpp
 * compare each function with the arena growth of the implemented structures.
 */

/**
 * @brief Partial and fully covered visited-node counts of one range update.
 */
struct FrontierCounts {
  /** @brief Visited nodes whose interval meets the range without lying inside it. */
  std::size_t partial;

  /** @brief Visited nodes whose interval lies inside the range: the canonical decomposition. */
  std::size_t decomposition;

  /**
   * @brief The visited frontier F, the number of update invocations.
   *
   * @return partial + decomposition.
   */
  std::size_t visited() const {
    return partial + decomposition;
  }
};

namespace detail {

/**
 * @brief Count partial and decomposition invocations of the update recursion.
 *
 * @param segmentLeft  Segment start.
 * @param segmentRight Segment end.
 * @param left         Range start.
 * @param right        Range end.
 * @param counts       Accumulated counts.
 */
inline void countFrontier(std::size_t segmentLeft, std::size_t segmentRight, std::size_t left,
                          std::size_t right, FrontierCounts& counts) {
  if (left <= segmentLeft && segmentRight <= right) {
    ++counts.decomposition;
    return;
  }
  ++counts.partial;
  const std::size_t middle = (segmentLeft + segmentRight) / 2;
  if (left <= middle) {
    countFrontier(segmentLeft, middle, left, right, counts);
  }
  if (right > middle) {
    countFrontier(middle + 1, segmentRight, left, right, counts);
  }
}

/**
 * @brief Count canonical nodes of a segment that meet the range.
 *
 * @param segmentLeft  Segment start.
 * @param segmentRight Segment end.
 * @param left         Range start.
 * @param right        Range end.
 *
 * @return The count.
 */
inline std::size_t countIntersecting(std::size_t segmentLeft, std::size_t segmentRight,
                                     std::size_t left, std::size_t right) {
  if (segmentLeft == segmentRight) {
    return 1;
  }
  const std::size_t middle = (segmentLeft + segmentRight) / 2;
  std::size_t count = 1;
  if (left <= middle) {
    count += countIntersecting(segmentLeft, middle, left, right);
  }
  if (right > middle) {
    count += countIntersecting(middle + 1, segmentRight, left, right);
  }
  return count;
}

/**
 * @brief Number of canonical nodes containing both indices: one plus the depth
 *        of their lowest common ancestor.
 *
 * @param n      Number of elements.
 * @param first  One index.
 * @param second Another index.
 *
 * @return The count.
 */
inline std::size_t nodesContainingBoth(std::size_t n, std::size_t first, std::size_t second) {
  std::size_t segmentLeft = 0;
  std::size_t segmentRight = n - 1;
  std::size_t count = 1;
  while (segmentLeft != segmentRight) {
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    if (first <= middle && second <= middle) {
      segmentRight = middle;
    } else if (first > middle && second > middle) {
      segmentLeft = middle + 1;
    } else {
      break;
    }
    ++count;
  }
  return count;
}

/**
 * @brief Number of decomposition nodes hanging off one boundary path.
 *
 * The node whose split is the boundary contributes one unless it also
 * contains the other boundary, and every node above it that contains this
 * boundary, not the other, and keeps the inner index on the side nearer the
 * split contributes one more.
 *
 * @param n                   Number of elements.
 * @param outer               Index just outside the range at this boundary.
 * @param inner               Index just inside the range at this boundary.
 * @param otherBoundaryExists Whether the range has a second interior boundary.
 * @param otherOuter          The other boundary's outside index, if any.
 *
 * @return The count.
 */
inline std::size_t decompositionAlongBoundary(std::size_t n, std::size_t outer, std::size_t inner,
                                              bool otherBoundaryExists, std::size_t otherOuter) {
  std::size_t segmentLeft = 0;
  std::size_t segmentRight = n - 1;
  std::size_t count = 0;
  while (true) {
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    const bool containsOther =
        otherBoundaryExists && segmentLeft <= otherOuter && otherOuter <= segmentRight;
    const bool innerOnLeft = inner <= middle;
    const bool outerOnLeft = outer <= middle;
    if (innerOnLeft != outerOnLeft) {
      // The split is the boundary itself: the child on the range side is
      // fully covered unless it also contains the other boundary.
      return count + (containsOther ? 0 : 1);
    }
    if (!containsOther) {
      // The child away from the boundary is fully covered exactly when the
      // inner index (the range side) is on the side nearer the split.
      const bool rangeIsLeftOfBoundary = inner < outer;
      if ((rangeIsLeftOfBoundary && !innerOnLeft) || (!rangeIsLeftOfBoundary && innerOnLeft)) {
        ++count;
      }
    }
    if (innerOnLeft) {
      segmentRight = middle;
    } else {
      segmentLeft = middle + 1;
    }
  }
}

} // namespace detail

/**
 * @brief Partial and decomposition counts of the update recursion on [left, right].
 *
 * This is the definition of F executed: the same recursion as every update
 * in the repository, counting invocations instead of appending records.
 *
 * @param n     Number of elements, at least one.
 * @param left  Left index (inclusive), at most right.
 * @param right Right index (inclusive), smaller than n.
 *
 * @return The two counts; visited() is F(n, left, right).
 */
inline FrontierCounts frontierCounts(std::size_t n, std::size_t left, std::size_t right) {
  FrontierCounts counts{0, 0};
  detail::countFrontier(0, n - 1, left, right, counts);
  return counts;
}

/**
 * @brief The visited frontier F(n, left, right) by the closed form of
 *        docs/proof.md, Proposition 10.2.
 *
 * With P_L the canonical nodes containing both left - 1 and left (empty when
 * left is 0), P_R those containing both right and right + 1 (empty when
 * right is n - 1), the partial count is |P_L ∪ P_R| and the decomposition
 * count is the number of fully covered children hanging off the two
 * boundary paths, or one for the full range.
 *
 * @param n     Number of elements, at least one.
 * @param left  Left index (inclusive), at most right.
 * @param right Right index (inclusive), smaller than n.
 *
 * @return F(n, left, right).
 */
inline std::size_t closedFormFrontier(std::size_t n, std::size_t left, std::size_t right) {
  const bool leftBoundary = left > 0;
  const bool rightBoundary = right + 1 < n;
  if (!leftBoundary && !rightBoundary) {
    return 1;
  }
  std::size_t partial = 0;
  std::size_t decomposition = 0;
  if (leftBoundary) {
    partial += detail::nodesContainingBoth(n, left - 1, left);
    decomposition +=
        detail::decompositionAlongBoundary(n, left - 1, left, rightBoundary, right + 1);
  }
  if (rightBoundary) {
    partial += detail::nodesContainingBoth(n, right, right + 1);
    decomposition += detail::decompositionAlongBoundary(n, right + 1, right, leftBoundary,
                                                        leftBoundary ? left - 1 : 0);
  }
  if (leftBoundary && rightBoundary) {
    partial -= detail::nodesContainingBoth(n, left - 1, right + 1);
  }
  return partial + decomposition;
}

/**
 * @brief Number of canonical nodes whose interval meets [left, right].
 *
 * A tagless path-copying update (PointOnlyPersistentSegmentTree) allocates
 * exactly this many records: every intersecting node is visited and copied.
 * It equals partial + 2k - decomposition for a range of k elements, since
 * each decomposition node of length m roots a subtree of 2m - 1 nodes.
 *
 * @param n     Number of elements, at least one.
 * @param left  Left index (inclusive), at most right.
 * @param right Right index (inclusive), smaller than n.
 *
 * @return The intersecting-node count.
 */
inline std::size_t intersectingNodes(std::size_t n, std::size_t left, std::size_t right) {
  return detail::countIntersecting(0, n - 1, left, right);
}

/**
 * @brief Height of the canonical tree over n elements: ceil(log2 n).
 *
 * @param n Number of elements, at least one.
 *
 * @return The height; zero for a single element.
 */
inline std::size_t treeHeight(std::size_t n) {
  std::size_t height = 0;
  std::size_t capacity = 1;
  while (capacity < n) {
    capacity *= 2;
    ++height;
  }
  return height;
}

/**
 * @brief The largest visited frontier over every range of a perfect tree of
 *        the given height: 1, 2 and 4h - 3 for h = 0, h = 1 and h >= 2.
 *
 * For n that is not a power of two the same value bounds F from above with
 * h = ceil(log2 n); tests/frontier_test.cpp records for which n it is attained.
 *
 * @param height Tree height.
 *
 * @return The maximum frontier.
 */
inline std::size_t maximumFrontier(std::size_t height) {
  if (height == 0) {
    return 1;
  }
  if (height == 1) {
    return 2;
  }
  return 4 * height - 3;
}

/**
 * @brief How many ranges of a family meet and contain a canonical interval.
 */
struct RangeFamilyCounts {
  /** @brief Ranges of the family that meet the interval. */
  std::size_t intersecting;

  /** @brief Ranges of the family that contain the interval. */
  std::size_t containing;
};

/**
 * @brief Exact sum of F over a family of ranges, without enumerating them.
 *
 * A canonical node is partial for a range exactly when the range meets it
 * without containing it, and belongs to the decomposition exactly when the
 * range contains it but not its parent. Summing those two indicator counts
 * over the 2n - 1 canonical nodes gives the sum of F over the family in
 * O(n) evaluations of the counting function.
 *
 * @tparam Counter Callable (segmentLeft, segmentRight) -> RangeFamilyCounts.
 * @param n       Number of elements, at least one.
 * @param counter Counts of the family for one canonical interval.
 *
 * @return Sum over the family of F(n, left, right).
 */
template <class Counter> std::size_t frontierSum(std::size_t n, Counter counter) {
  struct Frame {
    std::size_t segmentLeft;
    std::size_t segmentRight;
    std::size_t parentContaining;
  };
  std::vector<Frame> stack;
  stack.push_back(Frame{0, n - 1, 0});
  std::size_t sum = 0;
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();
    const RangeFamilyCounts counts = counter(frame.segmentLeft, frame.segmentRight);
    sum += (counts.intersecting - counts.containing) + (counts.containing - frame.parentContaining);
    if (frame.segmentLeft != frame.segmentRight) {
      const std::size_t middle = (frame.segmentLeft + frame.segmentRight) / 2;
      stack.push_back(Frame{frame.segmentLeft, middle, counts.containing});
      stack.push_back(Frame{middle + 1, frame.segmentRight, counts.containing});
    }
  }
  return sum;
}

/**
 * @brief Counting function for all n(n + 1) / 2 ranges of an n-element array.
 *
 * @param n Number of elements, at least one.
 *
 * @return A callable usable with frontierSum.
 */
inline auto allRangesCounter(std::size_t n) {
  return [n](std::size_t segmentLeft, std::size_t segmentRight) {
    const std::size_t total = n * (n + 1) / 2;
    const std::size_t entirelyLeft = segmentLeft * (segmentLeft + 1) / 2;
    const std::size_t tail = n - 1 - segmentRight;
    const std::size_t entirelyRight = tail * (tail + 1) / 2;
    return RangeFamilyCounts{total - entirelyLeft - entirelyRight,
                             (segmentLeft + 1) * (n - segmentRight)};
  };
}

/**
 * @brief Counting function for the n - width + 1 windows of a fixed width.
 *
 * @param n     Number of elements, at least one.
 * @param width Window width, between one and n.
 *
 * @return A callable usable with frontierSum.
 */
inline auto fixedWidthCounter(std::size_t n, std::size_t width) {
  return [n, width](std::size_t segmentLeft, std::size_t segmentRight) {
    const std::size_t lastStart = n - width;
    auto windowsIn = [lastStart](std::size_t low, std::size_t high) {
      // Windows starting in [low, high] clipped to the valid starts.
      const std::size_t clippedHigh = high < lastStart ? high : lastStart;
      return low <= clippedHigh ? clippedHigh - low + 1 : 0;
    };
    const std::size_t firstMeeting = segmentLeft + 1 > width ? segmentLeft + 1 - width : 0;
    const std::size_t intersecting = windowsIn(firstMeeting, segmentRight);
    std::size_t containing = 0;
    if (segmentRight - segmentLeft + 1 <= width) {
      const std::size_t firstContaining = segmentRight + 1 > width ? segmentRight + 1 - width : 0;
      containing = windowsIn(firstContaining, segmentLeft);
    }
    return RangeFamilyCounts{intersecting, containing};
  };
}

/**
 * @brief Tag-position model of the copy-on-push strategy, counting pushes.
 *
 * Keeps one tag per canonical node, exactly as CopyOnPushPersistentTree does
 * across versions, without records or aggregates. An update pushes at every
 * partially covered visited node whose tag is not the identity (composing the
 * tag into both children) and composes its action at every decomposition
 * node. The return value of apply is the push frontier P of that update, and
 * docs/proof.md Proposition 10.6 states that the copy-on-push update appends
 * exactly F + 2P records.
 *
 * @tparam Policy Aggregate/action policy from policy.hpp.
 */
template <class Policy> class PushCountingModel {
public:
  /** @brief Action type of the policy. */
  using Action = typename Policy::Action;

  /**
   * @brief Model over n elements with every tag the identity.
   *
   * @param n Number of elements, at least one.
   */
  explicit PushCountingModel(std::size_t n) : arraySize(n), tags(4 * n, Policy::actionIdentity()) {}

  /**
   * @brief Apply an action to [left, right] and count the pushes it causes.
   *
   * @param left   Left index (inclusive), at most right.
   * @param right  Right index (inclusive), smaller than the element count.
   * @param action Action applied; the identity causes no pushes and no change.
   *
   * @return The push frontier P of this update.
   */
  std::size_t apply(std::size_t left, std::size_t right, const Action& action) {
    if (action == Policy::actionIdentity()) {
      return 0;
    }
    return update(0, 0, arraySize - 1, left, right, action);
  }

private:
  std::size_t arraySize;
  std::vector<Action> tags;

  std::size_t update(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t left, std::size_t right, const Action& action) {
    if (left <= segmentLeft && segmentRight <= right) {
      tags[node] = Policy::compose(action, tags[node]);
      return 0;
    }
    std::size_t pushes = 0;
    if (!(tags[node] == Policy::actionIdentity())) {
      tags[2 * node + 1] = Policy::compose(tags[node], tags[2 * node + 1]);
      tags[2 * node + 2] = Policy::compose(tags[node], tags[2 * node + 2]);
      tags[node] = Policy::actionIdentity();
      pushes = 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    if (left <= middle) {
      pushes += update(2 * node + 1, segmentLeft, middle, left, right, action);
    }
    if (right > middle) {
      pushes += update(2 * node + 2, middle + 1, segmentRight, left, right, action);
    }
    return pushes;
  }
};

} // namespace valseg

#endif
