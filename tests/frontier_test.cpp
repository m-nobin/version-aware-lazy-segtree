// Executable evidence for docs/proof.md section 10, the frontier identities.
//
// Every "exact" statement in that section is checked here against the arena
// growth of the implemented structures: exhaustively over every range for
// small n, and on seeded histories for larger n with the seed recorded in
// the failure message. The lower-bound counterexample of section 10.6 is a
// test-local model that stores tags on edges instead of nodes; it agrees
// with the element-wise oracle and allocates fewer records than the subject
// on every range that is not the whole array.

#include <valseg/frontier.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/point_only_persistent_segment_tree.hpp>
#include <valseg/policy.hpp>
#include <valseg/policy_trees.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../bench/copy_on_push_segment_tree.hpp"
#include "policy_oracle.hpp"

namespace {

using valseg::allRangesCounter;
using valseg::closedFormFrontier;
using valseg::CopyOnPushPersistentTree;
using valseg::fixedWidthCounter;
using valseg::FrontierCounts;
using valseg::frontierCounts;
using valseg::frontierSum;
using valseg::intersectingNodes;
using valseg::maximumFrontier;
using valseg::MinAddPolicy;
using valseg::PersistentLazySegmentTree;
using valseg::PointOnlyPersistentSegmentTree;
using valseg::PushCountingModel;
using valseg::RetainedTagPersistentTree;
using valseg::SumAddPolicy;
using valseg::treeHeight;
using valseg::bench::CopyOnPushSegmentTree;
using valseg::testing::ElementWiseOracle;

using WideAffine = valseg::AffineSumModPolicy<998244353ULL>;

TEST(FrontierDomain, RejectsEmptyReversedAndOutOfBoundsInputs) {
  EXPECT_THROW(frontierCounts(0, 0, 0), std::invalid_argument);
  EXPECT_THROW(closedFormFrontier(0, 0, 0), std::invalid_argument);
  EXPECT_THROW(intersectingNodes(0, 0, 0), std::invalid_argument);
  EXPECT_THROW(frontierCounts(4, 3, 2), std::invalid_argument);
  EXPECT_THROW(closedFormFrontier(4, 0, 4), std::out_of_range);
  EXPECT_THROW(intersectingNodes(4, 4, 4), std::out_of_range);
  EXPECT_THROW(frontierSum(0, allRangesCounter(1)), std::invalid_argument);
}

TEST(FrontierDomain, HeightAndRangeFamiliesCheckTheirRepresentabilityDomains) {
  const std::size_t maximum = std::numeric_limits<std::size_t>::max();
  EXPECT_THROW(treeHeight(0), std::invalid_argument);
  EXPECT_EQ(treeHeight(maximum), std::numeric_limits<std::size_t>::digits);
  EXPECT_THROW(maximumFrontier(maximum), std::overflow_error);
  EXPECT_THROW(allRangesCounter(0), std::invalid_argument);
  EXPECT_THROW(allRangesCounter(maximum), std::overflow_error);
  EXPECT_THROW(fixedWidthCounter(0, 1), std::invalid_argument);
  EXPECT_THROW(fixedWidthCounter(8, 0), std::invalid_argument);
  EXPECT_THROW(fixedWidthCounter(8, 9), std::invalid_argument);

  const auto maximumWidth = fixedWidthCounter(maximum, maximum);
  const valseg::RangeFamilyCounts counts = maximumWidth(0, maximum - 1);
  EXPECT_EQ(counts.intersecting, 1U);
  EXPECT_EQ(counts.containing, 1U);
  EXPECT_EQ(frontierCounts(maximum, maximum - 1, maximum - 1).visited(),
            closedFormFrontier(maximum, maximum - 1, maximum - 1));
  EXPECT_EQ(intersectingNodes(maximum, maximum - 1, maximum - 1),
            frontierCounts(maximum, maximum - 1, maximum - 1).visited());
  EXPECT_THROW(intersectingNodes(maximum, 0, maximum - 1), std::overflow_error);
}

TEST(FrontierDomain, PushCountingModelChecksStorageAndUpdateRanges) {
  const std::size_t maximum = std::numeric_limits<std::size_t>::max();
  EXPECT_THROW(PushCountingModel<SumAddPolicy>(0), std::invalid_argument);
  EXPECT_THROW(PushCountingModel<SumAddPolicy>(maximum / 4 + 1), std::overflow_error);

  PushCountingModel<SumAddPolicy> model(8);
  EXPECT_THROW(model.apply(4, 3, 1), std::invalid_argument);
  EXPECT_THROW(model.apply(0, 8, 1), std::out_of_range);
}

std::pair<std::size_t, std::size_t> randomRange(std::size_t n, std::mt19937_64& rng) {
  std::size_t left = rng() % n;
  std::size_t right = rng() % n;
  if (left > right) {
    std::swap(left, right);
  }
  return {left, right};
}

std::vector<long long> ones(std::size_t n) {
  return std::vector<long long>(n, 1);
}

// The counterexample to frontier optimality in representation model R
// (docs/proof.md, section 10.6): tags live on the edges to the children, so
// a fully covered child is never copied; its parent, which is partially
// covered and copied anyway, composes the action into that edge. Each record
// still represents one canonical interval and holds O(1) state plus two
// child references. Records per update are the partial count, or one for
// the whole array. Like the subject, it retains older tags outside newer
// ones, so it is used with SumAdd only.
template <class Policy> class EdgeTagModel {
public:
  using Aggregate = typename Policy::Aggregate;
  using Action = typename Policy::Action;

  explicit EdgeTagModel(const std::vector<Aggregate>& values) : arraySize(values.size()) {
    roots.push_back(build(values, 0, arraySize - 1));
  }

  std::size_t rangeApply(std::size_t left, std::size_t right, const Action& action) {
    const std::size_t before = nodes.size();
    roots.push_back(update(roots.back(), 0, arraySize - 1, left, right, action));
    return nodes.size() - before;
  }

  Aggregate rangeAggregate(std::size_t version, std::size_t left, std::size_t right) const {
    return query(roots[version], 0, arraySize - 1, left, right, Policy::actionIdentity());
  }

  std::size_t nodeCount() const {
    return nodes.size();
  }

private:
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    Aggregate aggregate;
    Action leftTag;
    Action rightTag;
  };

  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  std::vector<Node> nodes;
  std::vector<std::size_t> roots;
  std::size_t arraySize;

  std::size_t build(const std::vector<Aggregate>& values, std::size_t segmentLeft,
                    std::size_t segmentRight) {
    if (segmentLeft == segmentRight) {
      nodes.push_back(Node{noNode, noNode, values[segmentLeft], Policy::actionIdentity(),
                           Policy::actionIdentity()});
      return nodes.size() - 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    const std::size_t leftRoot = build(values, segmentLeft, middle);
    const std::size_t rightRoot = build(values, middle + 1, segmentRight);
    nodes.push_back(Node{leftRoot, rightRoot,
                         Policy::combine(nodes[leftRoot].aggregate, nodes[rightRoot].aggregate),
                         Policy::actionIdentity(), Policy::actionIdentity()});
    return nodes.size() - 1;
  }

  std::size_t update(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t left, std::size_t right, const Action& action) {
    Node next = nodes[nodeIndex];
    const std::size_t length = segmentRight - segmentLeft + 1;
    if (segmentLeft == segmentRight) {
      next.aggregate = Policy::apply(action, next.aggregate, 1);
      nodes.push_back(next);
      return nodes.size() - 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    if (left <= segmentLeft && segmentRight <= right) {
      // The whole node is covered: the only case with no partial parent to
      // hold the action, so both edges take it.
      next.leftTag = Policy::compose(action, next.leftTag);
      next.rightTag = Policy::compose(action, next.rightTag);
      next.aggregate = Policy::apply(action, next.aggregate, length);
      nodes.push_back(next);
      return nodes.size() - 1;
    }
    if (left <= middle) {
      if (left <= segmentLeft && middle <= right) {
        next.leftTag = Policy::compose(action, next.leftTag);
      } else {
        next.leftChild = update(next.leftChild, segmentLeft, middle, left, right, action);
      }
    }
    if (right > middle) {
      if (left <= middle + 1 && segmentRight <= right) {
        next.rightTag = Policy::compose(action, next.rightTag);
      } else {
        next.rightChild = update(next.rightChild, middle + 1, segmentRight, left, right, action);
      }
    }
    next.aggregate = Policy::combine(
        Policy::apply(next.leftTag, nodes[next.leftChild].aggregate, middle - segmentLeft + 1),
        Policy::apply(next.rightTag, nodes[next.rightChild].aggregate, segmentRight - middle));
    nodes.push_back(next);
    return nodes.size() - 1;
  }

  Aggregate query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t left, std::size_t right, const Action& inherited) const {
    if (segmentRight < left || segmentLeft > right) {
      return Policy::aggregateIdentity();
    }
    const Node& current = nodes[nodeIndex];
    if (left <= segmentLeft && segmentRight <= right) {
      return Policy::apply(inherited, current.aggregate, segmentRight - segmentLeft + 1);
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    return Policy::combine(query(current.leftChild, segmentLeft, middle, left, right,
                                 Policy::compose(inherited, current.leftTag)),
                           query(current.rightChild, middle + 1, segmentRight, left, right,
                                 Policy::compose(inherited, current.rightTag)));
  }
};

// ---------------------------------------------------------------------------
// F: definition, closed form and the subject's arena growth
// ---------------------------------------------------------------------------

TEST(Frontier, ClosedFormMatchesTheRecursionOnEveryRange) {
  for (std::size_t n = 1; n <= 64; ++n) {
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        ASSERT_EQ(closedFormFrontier(n, left, right), frontierCounts(n, left, right).visited())
            << "n=" << n << " [" << left << ", " << right << "]";
      }
    }
  }
}

TEST(Frontier, SubjectAppendsExactlyTheVisitedFrontierOnEveryRange) {
  for (std::size_t n = 1; n <= 32; ++n) {
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        PersistentLazySegmentTree production(ones(n));
        RetainedTagPersistentTree<MinAddPolicy> generic(ones(n));
        const std::size_t expected = frontierCounts(n, left, right).visited();
        production.rangeAdd(left, right, 3);
        generic.rangeApply(left, right, 3);
        ASSERT_EQ(production.nodeCount() - (2 * n - 1), expected)
            << "n=" << n << " [" << left << ", " << right << "]";
        ASSERT_EQ(generic.nodeCount() - (2 * n - 1), expected)
            << "n=" << n << " [" << left << ", " << right << "]";
      }
    }
  }
}

TEST(Frontier, SubjectArenaIsBuildPlusFrontierSumOnSeededHistory) {
  const std::size_t n = 1000;
  const std::uint64_t seed = 20261007;
  std::mt19937_64 rng(seed);
  PersistentLazySegmentTree tree(ones(n));
  std::size_t frontierTotal = 0;
  for (int step = 0; step < 2000; ++step) {
    const auto [left, right] = randomRange(n, rng);
    const long long delta = (step % 17 == 0) ? 0 : static_cast<long long>(rng() % 7) - 3;
    const std::size_t before = tree.nodeCount();
    tree.rangeAdd(left, right, delta);
    const std::size_t expected = delta == 0 ? 0 : frontierCounts(n, left, right).visited();
    ASSERT_EQ(tree.nodeCount() - before, expected)
        << "seed=" << seed << " step=" << step << " [" << left << ", " << right << "]";
    frontierTotal += expected;
  }
  EXPECT_EQ(tree.nodeCount(), 2 * n - 1 + frontierTotal);
}

// ---------------------------------------------------------------------------
// Extrema and expectations
// ---------------------------------------------------------------------------

std::size_t largestFrontier(std::size_t n) {
  std::size_t largest = 0;
  for (std::size_t left = 0; left < n; ++left) {
    for (std::size_t right = left; right < n; ++right) {
      const std::size_t value = frontierCounts(n, left, right).visited();
      largest = value > largest ? value : largest;
    }
  }
  return largest;
}

TEST(Frontier, MaximumIsFourHeightMinusThreeAndTheMinimumIsOne) {
  for (std::size_t height = 0; height <= 7; ++height) {
    const std::size_t n = std::size_t{1} << height;
    EXPECT_EQ(largestFrontier(n), maximumFrontier(height)) << "n=" << n;
    EXPECT_EQ(frontierCounts(n, 0, n - 1).visited(), 1u);
  }
  // For other n the value for h = ceil(log2 n) is an upper bound that is never
  // attained (Proposition 10.3: attaining it needs the rightmost leaf at depth
  // h, which only a power of two has). The exact maximum for those n was
  // tabulated to 64 and lies in {4k - 1, 4k} with k = floor(log2 n); that
  // pattern is recorded here as an observation, not a theorem.
  for (std::size_t n = 3; n <= 64; ++n) {
    const std::size_t largest = largestFrontier(n);
    const std::size_t height = treeHeight(n);
    const bool powerOfTwo = (n & (n - 1)) == 0;
    ASSERT_LE(largest, maximumFrontier(height)) << "n=" << n;
    if (powerOfTwo) {
      EXPECT_EQ(largest, maximumFrontier(height)) << "n=" << n;
    } else {
      const std::size_t floorHeight = height - 1;
      EXPECT_LT(largest, maximumFrontier(height)) << "n=" << n;
      EXPECT_TRUE(largest == 4 * floorHeight - 1 || largest == 4 * floorHeight) << "n=" << n;
    }
  }
}

TEST(Frontier, SumOverAllRangesAndFixedWidthWindowsMatchesEnumeration) {
  for (std::size_t n = 1; n <= 40; ++n) {
    std::size_t enumerated = 0;
    std::vector<std::size_t> byWidth(n + 1, 0);
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        const std::size_t value = frontierCounts(n, left, right).visited();
        enumerated += value;
        byWidth[right - left + 1] += value;
      }
    }
    ASSERT_EQ(frontierSum(n, allRangesCounter(n)), enumerated) << "n=" << n;
    for (std::size_t width = 1; width <= n; ++width) {
      ASSERT_EQ(frontierSum(n, fixedWidthCounter(n, width)), byWidth[width])
          << "n=" << n << " width=" << width;
    }
  }
}

// ---------------------------------------------------------------------------
// Point materialization: intersecting nodes
// ---------------------------------------------------------------------------

TEST(Frontier, PointOnlyAppendsExactlyTheIntersectingNodesOnEveryRange) {
  for (std::size_t n = 1; n <= 32; ++n) {
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        PointOnlyPersistentSegmentTree tree(ones(n));
        tree.rangeAdd(left, right, 2);
        const FrontierCounts counts = frontierCounts(n, left, right);
        const std::size_t width = right - left + 1;
        const std::size_t expected = intersectingNodes(n, left, right);
        ASSERT_EQ(tree.nodeCount() - (2 * n - 1), expected)
            << "n=" << n << " [" << left << ", " << right << "]";
        ASSERT_EQ(expected, counts.partial + 2 * width - counts.decomposition)
            << "n=" << n << " [" << left << ", " << right << "]";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// P and the copy-on-push identity F + 2P
// ---------------------------------------------------------------------------

TEST(Frontier, CopyOnPushAppendsFrontierPlusTwicePushesOnEveryPairOfRanges) {
  for (std::size_t n = 1; n <= 8; ++n) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        ranges.emplace_back(left, right);
      }
    }
    for (const auto& first : ranges) {
      for (const auto& second : ranges) {
        CopyOnPushSegmentTree tree;
        tree.initialize(ones(n));
        PushCountingModel<SumAddPolicy> model(n);
        std::size_t before = tree.nodeCount();
        tree.rangeAdd(first.first, first.second, 1);
        std::size_t pushes = model.apply(first.first, first.second, 1);
        ASSERT_EQ(pushes, 0u);
        ASSERT_EQ(tree.nodeCount() - before,
                  frontierCounts(n, first.first, first.second).visited());
        before = tree.nodeCount();
        tree.rangeAdd(second.first, second.second, 1);
        pushes = model.apply(second.first, second.second, 1);
        ASSERT_EQ(tree.nodeCount() - before,
                  frontierCounts(n, second.first, second.second).visited() + 2 * pushes)
            << "n=" << n << " [" << first.first << ", " << first.second << "] then ["
            << second.first << ", " << second.second << "]";
      }
    }
  }
}

template <class Tree, class Policy>
void checkPushIdentityOnSeededHistory(std::size_t n, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::vector<typename Policy::Aggregate> initial(n, 1);
  Tree tree(initial);
  PushCountingModel<Policy> model(n);
  for (int step = 0; step < 1500; ++step) {
    const auto [left, right] = randomRange(n, rng);
    typename Policy::Action action = Policy::actionIdentity();
    if (step % 13 != 0) {
      if constexpr (std::is_same_v<typename Policy::Action, long long>) {
        // Small deltas so tags cancel to zero often, exercising the "no push
        // on an identity tag" branch.
        action = static_cast<long long>(rng() % 5) - 2;
      } else {
        action = typename Policy::Action{rng(), rng()};
      }
    }
    const std::size_t before = tree.nodeCount();
    tree.rangeApply(left, right, action);
    const std::size_t pushes = model.apply(left, right, action);
    const FrontierCounts counts = frontierCounts(n, left, right);
    const bool identity = action == Policy::actionIdentity();
    ASSERT_LE(pushes, counts.partial);
    ASSERT_EQ(tree.nodeCount() - before, identity ? 0 : counts.visited() + 2 * pushes)
        << "seed=" << seed << " n=" << n << " step=" << step << " [" << left << ", " << right
        << "]";
  }
}

TEST(Frontier, CopyOnPushIdentityHoldsOnSeededHistoriesForEveryPolicy) {
  for (const std::size_t n : std::vector<std::size_t>{1, 2, 5, 16, 64, 500}) {
    checkPushIdentityOnSeededHistory<CopyOnPushPersistentTree<SumAddPolicy>, SumAddPolicy>(
        n, 20261100 + n);
    checkPushIdentityOnSeededHistory<CopyOnPushPersistentTree<MinAddPolicy>, MinAddPolicy>(
        n, 20261200 + n);
    checkPushIdentityOnSeededHistory<CopyOnPushPersistentTree<WideAffine>, WideAffine>(n, 20261300 +
                                                                                              n);
  }
}

// Every partial node of the probe update [1, n - 2] carries a tag: tag the
// left boundary path bottom-up, then the right one, then the root; no setup
// update descends past a tagged node, so none pushes. The probe then pays
// P = 2h - 1 pushes on top of F = 4h - 3 visits, the 8h - 5 bound.
TEST(Frontier, PushFrontierWorstCaseIsAttained) {
  for (std::size_t height = 2; height <= 7; ++height) {
    const std::size_t n = std::size_t{1} << height;
    CopyOnPushSegmentTree tree;
    tree.initialize(ones(n));
    PushCountingModel<SumAddPolicy> model(n);
    auto apply = [&](std::size_t left, std::size_t right) {
      tree.rangeAdd(left, right, 1);
      model.apply(left, right, 1);
    };
    for (std::size_t width = 2; width <= n / 2; width *= 2) {
      apply(0, width - 1);
    }
    for (std::size_t width = 2; width <= n / 2; width *= 2) {
      apply(n - width, n - 1);
    }
    apply(0, n - 1);

    const std::size_t before = tree.nodeCount();
    tree.rangeAdd(1, n - 2, 1);
    const std::size_t pushes = model.apply(1, n - 2, 1);
    const FrontierCounts counts = frontierCounts(n, 1, n - 2);
    EXPECT_EQ(counts.visited(), maximumFrontier(height)) << "n=" << n;
    EXPECT_EQ(counts.partial, 2 * height - 1) << "n=" << n;
    EXPECT_EQ(pushes, counts.partial) << "n=" << n;
    EXPECT_EQ(tree.nodeCount() - before, 8 * height - 5) << "n=" << n;
  }
}

// ---------------------------------------------------------------------------
// The lower-bound counterexample inside model R
// ---------------------------------------------------------------------------

TEST(Frontier, EdgeTagModelAgreesWithTheOracleAndAllocatesThePartialCount) {
  for (std::size_t n = 1; n <= 16; ++n) {
    for (std::size_t left = 0; left < n; ++left) {
      for (std::size_t right = left; right < n; ++right) {
        EdgeTagModel<SumAddPolicy> model(ones(n));
        const FrontierCounts counts = frontierCounts(n, left, right);
        const std::size_t expected = counts.partial == 0 ? 1 : counts.partial;
        ASSERT_EQ(model.rangeApply(left, right, 5), expected)
            << "n=" << n << " [" << left << ", " << right << "]";
        ASSERT_LE(expected, counts.visited());
        if (!(left == 0 && right == n - 1)) {
          ASSERT_LT(expected, counts.visited())
              << "n=" << n << " [" << left << ", " << right << "]";
        }
      }
    }
  }

  for (const std::size_t n : std::vector<std::size_t>{1, 2, 7, 64, 300}) {
    const std::uint64_t seed = 20261400 + n;
    std::mt19937_64 rng(seed);
    std::vector<long long> initial(n);
    for (auto& value : initial) {
      value = static_cast<long long>(rng() % 101) - 50;
    }
    EdgeTagModel<SumAddPolicy> model(initial);
    ElementWiseOracle<SumAddPolicy> oracle(initial);
    std::size_t subjectRecords = 0;
    std::size_t edgeRecords = 0;
    for (int step = 0; step < 800; ++step) {
      const auto [left, right] = randomRange(n, rng);
      if (rng() % 2 == 0) {
        const long long delta = static_cast<long long>(rng() % 21) - 10;
        const FrontierCounts counts = frontierCounts(n, left, right);
        const std::size_t appended = model.rangeApply(left, right, delta);
        oracle.rangeApply(left, right, delta);
        ASSERT_EQ(appended, counts.partial == 0 ? 1 : counts.partial)
            << "seed=" << seed << " step=" << step;
        subjectRecords += counts.visited();
        edgeRecords += appended;
      } else {
        const std::size_t version = rng() % oracle.versionCount();
        ASSERT_EQ(model.rangeAggregate(version, left, right),
                  oracle.rangeAggregate(version, left, right))
            << "seed=" << seed << " step=" << step << " v" << version << " [" << left << ", "
            << right << "]";
      }
    }
    EXPECT_LE(edgeRecords, subjectRecords) << "n=" << n;
  }
}

} // namespace
