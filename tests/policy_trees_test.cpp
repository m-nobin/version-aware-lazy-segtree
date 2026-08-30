// Executable evidence for docs/proof.md section 9, the observational
// commutativity boundary.
//
// What is checked, and against what:
//  - the SumAdd instantiations of the generic subject and the generic
//    copy-on-push ablation match PersistentLazySegmentTree and
//    bench/CopyOnPushSegmentTree in arena size after every update and in
//    every answer on a seeded history, so the theorem is about the measured
//    structures and not about a lookalike;
//  - every supported (policy, structure) pair agrees with the element-wise
//    oracle on seeded histories; the seed and a replayable operation log are
//    printed on any mismatch;
//  - the tree-order reference model of Lemma 9.2 reproduces the oracle for
//    the commuting policies and reproduces the hand-traced AffineSum
//    counterexample, which is the negative witness; the same trace runs on
//    the subject template under a test-local policy with a falsified
//    capability fact, to tie Lemma 9.2 to the code. The production policy is
//    rejected by the compile-fail test registered in tests/CMakeLists.txt;
//  - the arbitrary-action controls (copy-on-push, point materialization and
//    the pushed lazy tree) return the chronological answer on that same
//    witness trace and on seeded AffineSum histories;
//  - the generic types keep the shared validation contract, the identity-
//    action fast path and failed-update isolation, including a policy
//    overflow thrown from inside a descent.

#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/policy.hpp>
#include <valseg/policy_trees.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../bench/copy_on_push_segment_tree.hpp"
#include "policy_oracle.hpp"

namespace {

using valseg::AffineSumModPolicy;
using valseg::CopyOnPushPersistentTree;
using valseg::MinAddPolicy;
using valseg::PersistentLazySegmentTree;
using valseg::PointMaterializedPersistentTree;
using valseg::PushedLazyTree;
using valseg::RetainedTagPersistentTree;
using valseg::SumAddPolicy;
using valseg::bench::CopyOnPushSegmentTree;
using valseg::testing::ElementWiseOracle;

using Affine = AffineSumModPolicy<13>;
using WideAffine = AffineSumModPolicy<998244353ULL>;

constexpr std::size_t kSizes[] = {1, 2, 5, 16, 64};
constexpr std::size_t kSeedsPerSize = 2;
constexpr std::size_t kOperationsPerRun = 600;
constexpr std::size_t kIdentityPeriod = 53; // exercises the shared-root fast path

// Bounded so that no SumAdd intermediate leaves long long: |value| <= 50,
// |delta| <= 20, at most 600 updates and n <= 64.
template <class Policy> typename Policy::Aggregate randomValue(std::mt19937_64& rng) {
  if constexpr (std::is_same_v<typename Policy::Aggregate, long long>) {
    return static_cast<long long>(rng() % 101) - 50;
  } else {
    return rng() % 1000;
  }
}

template <class Policy> typename Policy::Action randomAction(std::mt19937_64& rng) {
  if constexpr (std::is_same_v<typename Policy::Action, long long>) {
    return static_cast<long long>(rng() % 41) - 20;
  } else {
    return typename Policy::Action{rng(), rng()};
  }
}

template <class Action> std::string show(const Action& action) {
  if constexpr (std::is_integral_v<Action>) {
    return std::to_string(action);
  } else {
    return "(" + std::to_string(action.scale) + ", " + std::to_string(action.shift) + ")";
  }
}

template <class Policy>
std::vector<typename Policy::Aggregate> randomArray(std::size_t n, std::mt19937_64& rng) {
  std::vector<typename Policy::Aggregate> values(n);
  for (auto& value : values) {
    value = randomValue<Policy>(rng);
  }
  return values;
}

std::pair<std::size_t, std::size_t> randomRange(std::size_t n, std::mt19937_64& rng) {
  std::size_t left = rng() % n;
  std::size_t right = rng() % n;
  if (left > right) {
    std::swap(left, right);
  }
  return {left, right};
}

// Reference model of Lemma 9.2 (tree-order semantics). It keeps, for every
// canonical node, the chronological composition of the actions placed on it,
// and evaluates an element by applying the tags on its root-to-leaf path
// innermost first. It has no aggregates, no persistence and no sharing: it
// is the order the retained-tag subject applies actions in, and nothing else.
template <class Policy> class TreeOrderModel {
public:
  using Aggregate = typename Policy::Aggregate;
  using Action = typename Policy::Action;

  explicit TreeOrderModel(std::size_t n) : arraySize(n), tags(4 * n, Policy::actionIdentity()) {}

  void place(std::size_t left, std::size_t right, const Action& action) {
    place(0, 0, arraySize - 1, left, right, action);
  }

  Aggregate element(std::size_t index, Aggregate initial) const {
    std::vector<std::size_t> path;
    std::size_t node = 0;
    std::size_t segmentLeft = 0;
    std::size_t segmentRight = arraySize - 1;
    while (true) {
      path.push_back(node);
      if (segmentLeft == segmentRight) {
        break;
      }
      const std::size_t middle = (segmentLeft + segmentRight) / 2;
      if (index <= middle) {
        node = 2 * node + 1;
        segmentRight = middle;
      } else {
        node = 2 * node + 2;
        segmentLeft = middle + 1;
      }
    }
    Aggregate value = initial;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      value = Policy::apply(tags[*it], value, 1);
    }
    return value;
  }

private:
  std::size_t arraySize;
  std::vector<Action> tags;

  void place(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
             std::size_t queryLeft, std::size_t queryRight, const Action& action) {
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return;
    }
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      tags[node] = Policy::compose(action, tags[node]);
      return;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    place(2 * node + 1, segmentLeft, middle, queryLeft, queryRight, action);
    place(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight, action);
  }
};

// ---------------------------------------------------------------------------
// Agreement with the production SumAdd structures
// ---------------------------------------------------------------------------

template <class Generic, class Production>
void checkRecordForRecord(Generic& generic, Production& production, std::size_t n,
                          std::mt19937_64& rng) {
  ASSERT_EQ(generic.nodeCount(), production.nodeCount());
  for (std::size_t step = 0; step < 300; ++step) {
    const auto [left, right] = randomRange(n, rng);
    const long long delta = (step % kIdentityPeriod == 0) ? 0 : randomAction<SumAddPolicy>(rng);
    ASSERT_EQ(generic.rangeApply(left, right, delta), production.rangeAdd(left, right, delta));
    ASSERT_EQ(generic.nodeCount(), production.nodeCount())
        << "arena sizes diverged after rangeAdd(" << left << ", " << right << ", " << delta << ")";
  }
  ASSERT_EQ(generic.versionCount(), production.versionCount());
  for (std::size_t version = 0; version < production.versionCount(); ++version) {
    for (int probe = 0; probe < 4; ++probe) {
      const auto [left, right] = randomRange(n, rng);
      EXPECT_EQ(generic.rangeAggregate(version, left, right),
                production.rangeSum(version, left, right));
    }
  }
}

TEST(PolicyTrees, RetainedTagSumAddMatchesPersistentLazySegmentTreeNodeForNode) {
  std::mt19937_64 rng(20261005);
  const auto initial = randomArray<SumAddPolicy>(64, rng);
  RetainedTagPersistentTree<SumAddPolicy> generic(initial);
  PersistentLazySegmentTree production(initial);
  checkRecordForRecord(generic, production, 64, rng);
}

TEST(PolicyTrees, CopyOnPushSumAddMatchesBenchAblationNodeForNode) {
  std::mt19937_64 rng(20261006);
  const auto initial = randomArray<SumAddPolicy>(64, rng);
  CopyOnPushPersistentTree<SumAddPolicy> generic(initial);
  CopyOnPushSegmentTree production;
  production.initialize(initial);
  checkRecordForRecord(generic, production, 64, rng);
}

// ---------------------------------------------------------------------------
// Seeded differential validation against the element-wise oracle
// ---------------------------------------------------------------------------

template <class P, template <class> class T> struct Case {
  using Policy = P;
  using Tree = T<P>;
};

struct CaseNames {
  template <class C> static std::string GetName(int) {
    using Policy = typename C::Policy;
    using Tree = typename C::Tree;
    std::string name;
    if constexpr (std::is_same_v<Policy, SumAddPolicy>) {
      name = "SumAdd";
    } else if constexpr (std::is_same_v<Policy, MinAddPolicy>) {
      name = "MinAdd";
    } else {
      name = "AffineSum";
    }
    if constexpr (std::is_same_v<Tree, RetainedTagPersistentTree<Policy>>) {
      return name + "RetainedTag";
    } else if constexpr (std::is_same_v<Tree, CopyOnPushPersistentTree<Policy>>) {
      return name + "CopyOnPush";
    } else {
      return name + "PointMaterialized";
    }
  }
};

template <class Case> class PersistentPolicyTreeTest : public ::testing::Test {};

using PersistentCases = ::testing::Types<
    Case<SumAddPolicy, RetainedTagPersistentTree>, Case<MinAddPolicy, RetainedTagPersistentTree>,
    Case<SumAddPolicy, CopyOnPushPersistentTree>, Case<MinAddPolicy, CopyOnPushPersistentTree>,
    Case<WideAffine, CopyOnPushPersistentTree>, Case<SumAddPolicy, PointMaterializedPersistentTree>,
    Case<MinAddPolicy, PointMaterializedPersistentTree>,
    Case<WideAffine, PointMaterializedPersistentTree>>;
TYPED_TEST_SUITE(PersistentPolicyTreeTest, PersistentCases, CaseNames);

template <class Policy, class Tree> void runPersistentCampaign(std::size_t n, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  const auto initial = randomArray<Policy>(n, rng);
  Tree tree(initial);
  ElementWiseOracle<Policy> oracle(initial);

  std::ostringstream log;
  log << "seed=" << seed << " n=" << n << "\nreplayable operations:\n";
  std::size_t updates = 0;
  for (std::size_t step = 0; step < kOperationsPerRun; ++step) {
    const auto [left, right] = randomRange(n, rng);
    if (rng() % 2 == 0) {
      const typename Policy::Action action =
          (updates % kIdentityPeriod == 0) ? Policy::actionIdentity() : randomAction<Policy>(rng);
      ++updates;
      log << "  rangeApply(" << left << ", " << right << ", " << show(action) << ")\n";
      ASSERT_EQ(tree.rangeApply(left, right, action), oracle.rangeApply(left, right, action))
          << log.str();
    } else {
      const std::size_t version = rng() % oracle.versionCount();
      log << "  rangeAggregate(v" << version << ", " << left << ", " << right << ")\n";
      ASSERT_EQ(tree.rangeAggregate(version, left, right),
                oracle.rangeAggregate(version, left, right))
          << log.str();
    }
  }
  ASSERT_EQ(tree.versionCount(), oracle.versionCount());
}

TYPED_TEST(PersistentPolicyTreeTest, AgreesWithElementWiseOracleOnSeededHistories) {
  for (const std::size_t n : kSizes) {
    for (std::size_t seedIndex = 0; seedIndex < kSeedsPerSize; ++seedIndex) {
      const std::uint64_t seed = 20261000ULL + n * 100 + seedIndex;
      SCOPED_TRACE("n=" + std::to_string(n) + " seed=" + std::to_string(seed));
      runPersistentCampaign<typename TypeParam::Policy, typename TypeParam::Tree>(n, seed);
    }
  }
}

template <class Policy> void runLazyCampaign(std::size_t n, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  const auto initial = randomArray<Policy>(n, rng);
  PushedLazyTree<Policy> tree(initial);
  ElementWiseOracle<Policy> oracle(initial);

  std::ostringstream log;
  log << "seed=" << seed << " n=" << n << "\nreplayable operations:\n";
  for (std::size_t step = 0; step < kOperationsPerRun; ++step) {
    const auto [left, right] = randomRange(n, rng);
    if (rng() % 2 == 0) {
      const auto action = randomAction<Policy>(rng);
      log << "  rangeApply(" << left << ", " << right << ", " << show(action) << ")\n";
      tree.rangeApply(left, right, action);
      oracle.rangeApply(left, right, action);
    } else {
      log << "  rangeAggregate(" << left << ", " << right << ")\n";
      ASSERT_EQ(tree.rangeAggregate(left, right),
                oracle.rangeAggregate(oracle.versionCount() - 1, left, right))
          << log.str();
    }
  }
}

TEST(PolicyTrees, PushedLazyTreeAgreesWithOracleForEveryPolicy) {
  for (const std::size_t n : kSizes) {
    SCOPED_TRACE("n=" + std::to_string(n));
    runLazyCampaign<SumAddPolicy>(n, 20262000 + n);
    runLazyCampaign<MinAddPolicy>(n, 20262100 + n);
    runLazyCampaign<WideAffine>(n, 20262200 + n);
  }
}

// ---------------------------------------------------------------------------
// Tree order versus chronological order
// ---------------------------------------------------------------------------

template <class Policy> void checkTreeOrderMatchesChronology(std::size_t n, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  const auto initial = randomArray<Policy>(n, rng);
  TreeOrderModel<Policy> model(n);
  ElementWiseOracle<Policy> oracle(initial);
  for (int step = 0; step < 200; ++step) {
    const auto [left, right] = randomRange(n, rng);
    const auto action = randomAction<Policy>(rng);
    model.place(left, right, action);
    oracle.rangeApply(left, right, action);
  }
  for (std::size_t index = 0; index < n; ++index) {
    ASSERT_EQ(model.element(index, initial[index]),
              oracle.rangeAggregate(oracle.versionCount() - 1, index, index))
        << "seed=" << seed << " n=" << n << " index=" << index;
  }
}

// For SumAdd and MinAdd the depth order of Lemma 9.2 and the chronological
// order give the same element: the combinatorial core of Theorem 9.4.
TEST(PolicyTrees, TreeOrderEqualsChronologicalOrderForCommutingPolicies) {
  for (const std::size_t n : kSizes) {
    checkTreeOrderMatchesChronology<SumAddPolicy>(n, 20263000 + n);
    checkTreeOrderMatchesChronology<MinAddPolicy>(n, 20263100 + n);
  }
}

// The minimal counterexample of docs/proof.md section 9.5: n = 2, initial
// [0, 0], double everything (root tag), then increment element 0 (leaf
// tag). Tree order applies the root's doubling outside the increment and
// gives 2; the chronological answer is 1.
TEST(PolicyTrees, AffineSumMinimalTraceSeparatesTreeOrderFromChronology) {
  const Affine::Action doubleIt{2, 0};
  const Affine::Action increment{1, 1};

  TreeOrderModel<Affine> treeOrder(2);
  treeOrder.place(0, 1, doubleIt);
  treeOrder.place(0, 0, increment);
  EXPECT_EQ(treeOrder.element(0, 0), 2u);
  EXPECT_EQ(treeOrder.element(1, 0), 0u);

  ElementWiseOracle<Affine> chronological({0, 0});
  chronological.rangeApply(0, 1, doubleIt);
  chronological.rangeApply(0, 0, increment);
  EXPECT_EQ(chronological.rangeAggregate(2, 0, 0), 1u);
  EXPECT_EQ(chronological.rangeAggregate(2, 0, 1), 1u);
}

// Lemma 9.2 executed on the subject itself. The policy below inherits the
// affine arithmetic and falsifies its capability fact, so the tag-retaining
// template accepts it; the assertions check that the code computes the
// tree-order value (2) and grows the arena by one record for the root update
// and two for the leaf update. This is a check of what the subject computes,
// not correctness evidence: the production policy is rejected at compile time.
struct MislabeledAffine : Affine {
  static constexpr bool kInducedActionsCommute = true;
};

TEST(PolicyTrees, RetainedTagSubjectComputesTheTreeOrderOnTheWitnessTrace) {
  const Affine::Action doubleIt{2, 0};
  const Affine::Action increment{1, 1};

  RetainedTagPersistentTree<MislabeledAffine> subject({0, 0});
  EXPECT_EQ(subject.nodeCount(), 3u);
  subject.rangeApply(0, 1, doubleIt);
  EXPECT_EQ(subject.nodeCount(), 4u);
  subject.rangeApply(0, 0, increment);
  EXPECT_EQ(subject.nodeCount(), 6u);

  TreeOrderModel<Affine> treeOrder(2);
  treeOrder.place(0, 1, doubleIt);
  treeOrder.place(0, 0, increment);
  EXPECT_EQ(subject.rangeAggregate(2, 0, 0), 2u);
  EXPECT_EQ(subject.rangeAggregate(2, 0, 0), treeOrder.element(0, 0));
  EXPECT_EQ(subject.rangeAggregate(2, 0, 1), 2u);
  EXPECT_EQ(subject.rangeAggregate(1, 0, 1), 0u);
}

// The same trace on every arbitrary-action control returns the
// chronological answer. Copy-on-push pays for it structurally: descending
// past the tagged root pushes it into two copied children before the leaf
// and the root are copied, four records where the retained-tag subject
// would append two.
TEST(PolicyTrees, ArbitraryActionControlsReturnTheChronologicalAnswerOnTheWitnessTrace) {
  const Affine::Action doubleIt{2, 0};
  const Affine::Action increment{1, 1};

  CopyOnPushPersistentTree<Affine> copyOnPush({0, 0});
  copyOnPush.rangeApply(0, 1, doubleIt);
  EXPECT_EQ(copyOnPush.nodeCount(), 4u);
  copyOnPush.rangeApply(0, 0, increment);
  EXPECT_EQ(copyOnPush.nodeCount(), 8u);
  EXPECT_EQ(copyOnPush.rangeAggregate(2, 0, 0), 1u);
  EXPECT_EQ(copyOnPush.rangeAggregate(2, 0, 1), 1u);
  EXPECT_EQ(copyOnPush.rangeAggregate(1, 0, 1), 0u);

  PointMaterializedPersistentTree<Affine> materialized({0, 0});
  materialized.rangeApply(0, 1, doubleIt);
  materialized.rangeApply(0, 0, increment);
  EXPECT_EQ(materialized.rangeAggregate(2, 0, 0), 1u);

  PushedLazyTree<Affine> lazy({0, 0});
  lazy.rangeApply(0, 1, doubleIt);
  lazy.rangeApply(0, 0, increment);
  EXPECT_EQ(lazy.rangeAggregate(0, 0), 1u);
}

// A hand-checked MinAdd history on the subject: the aggregate ignores the
// segment length, so code that silently multiplies by it fails here.
TEST(PolicyTrees, RetainedTagMinAddHandTrace) {
  RetainedTagPersistentTree<MinAddPolicy> tree({5, 3, 8, 1});
  EXPECT_EQ(tree.nodeCount(), 7u);

  EXPECT_EQ(tree.rangeApply(0, 3, -2), 1u); // [3, 1, 6, -1]
  EXPECT_EQ(tree.nodeCount(), 8u);
  EXPECT_EQ(tree.rangeApply(1, 1, 10), 2u); // [3, 11, 6, -1]
  EXPECT_EQ(tree.nodeCount(), 11u);

  EXPECT_EQ(tree.rangeAggregate(0, 0, 1), 3);
  EXPECT_EQ(tree.rangeAggregate(1, 1, 2), 1);
  EXPECT_EQ(tree.rangeAggregate(2, 1, 2), 6);
  EXPECT_EQ(tree.rangeAggregate(2, 0, 3), -1);
  EXPECT_EQ(tree.rangeAggregate(2, 1, 1), 11);
}

// ---------------------------------------------------------------------------
// Shared validation contract, identity fast path and failed-update isolation
// ---------------------------------------------------------------------------

TEST(PolicyTrees, GenericTypesKeepTheSharedValidationContract) {
  RetainedTagPersistentTree<MinAddPolicy> minTree;
  EXPECT_THROW(minTree.rangeApply(0, 0, 1), std::runtime_error);
  EXPECT_THROW(minTree.rangeAggregate(0, 0, 0), std::runtime_error);
  minTree.initialize({});
  EXPECT_EQ(minTree.versionCount(), 1u);
  EXPECT_THROW(minTree.rangeApply(0, 0, 1), std::runtime_error);
  minTree.initialize({4, 2, 9});
  EXPECT_THROW(minTree.rangeApply(2, 1, 1), std::invalid_argument);
  EXPECT_THROW(minTree.rangeApply(0, 3, 1), std::out_of_range);
  EXPECT_THROW(minTree.rangeAggregate(1, 0, 2), std::out_of_range);
  EXPECT_THROW(minTree.rangeAggregate(0, 2, 1), std::invalid_argument);
  EXPECT_THROW(minTree.rangeAggregate(0, 0, 3), std::out_of_range);
  EXPECT_EQ(minTree.versionCount(), 1u);
  EXPECT_EQ(minTree.nodeCount(), 5u);
  EXPECT_EQ(minTree.rangeApply(0, 2, 0), 1u);
  EXPECT_EQ(minTree.versionCount(), 2u);
  EXPECT_EQ(minTree.nodeCount(), 5u);
  EXPECT_EQ(minTree.rangeAggregate(1, 0, 2), 2);

  CopyOnPushPersistentTree<WideAffine> affineTree;
  const WideAffine::Action probe{3, 4};
  EXPECT_THROW(affineTree.rangeApply(0, 0, probe), std::runtime_error);
  affineTree.initialize({1, 2, 3});
  EXPECT_THROW(affineTree.rangeApply(0, 3, probe), std::out_of_range);
  EXPECT_THROW(affineTree.rangeAggregate(1, 0, 2), std::out_of_range);
  EXPECT_EQ(affineTree.rangeApply(0, 2, WideAffine::actionIdentity()), 1u);
  EXPECT_EQ(affineTree.nodeCount(), 5u);
  EXPECT_EQ(affineTree.rangeAggregate(1, 0, 2), 6u);

  PointMaterializedPersistentTree<SumAddPolicy> pointTree;
  EXPECT_THROW(pointTree.rangeAggregate(0, 0, 0), std::runtime_error);
  pointTree.initialize({1, 2, 3});
  EXPECT_THROW(pointTree.rangeApply(2, 1, 1), std::invalid_argument);
  EXPECT_EQ(pointTree.rangeApply(0, 2, 0), 1u);
  EXPECT_EQ(pointTree.nodeCount(), 5u);

  PushedLazyTree<MinAddPolicy> lazy;
  EXPECT_THROW(lazy.rangeApply(0, 0, 1), std::runtime_error);
  lazy.initialize({4, 2, 9});
  EXPECT_THROW(lazy.rangeAggregate(0, 3), std::out_of_range);
  EXPECT_EQ(lazy.rangeAggregate(0, 2), 2);
}

// A policy exception thrown from inside a descent publishes nothing: the
// arena is rolled back to its checkpoint and every version is unchanged.
template <class Tree> void checkOverflowIsolation() {
  const long long maximum = std::numeric_limits<long long>::max();
  Tree tree({maximum - 10, 0, 5});
  tree.rangeApply(0, 0, 1); // [maximum - 9, 0, 5], sum maximum - 4
  const std::size_t versions = tree.versionCount();
  const std::size_t records = tree.nodeCount();

  // Adding 2 to all three elements would push the root sum past the limit.
  EXPECT_THROW(tree.rangeApply(0, 2, 2), std::overflow_error);
  EXPECT_EQ(tree.versionCount(), versions);
  EXPECT_EQ(tree.nodeCount(), records);
  EXPECT_EQ(tree.rangeAggregate(1, 0, 0), maximum - 9);
  EXPECT_EQ(tree.rangeAggregate(1, 1, 2), 5);

  tree.initialize({7});
  EXPECT_EQ(tree.versionCount(), 1u);
  EXPECT_EQ(tree.rangeAggregate(0, 0, 0), 7);
}

TEST(PolicyTrees, FailedUpdatesPublishNothing) {
  checkOverflowIsolation<RetainedTagPersistentTree<SumAddPolicy>>();
  checkOverflowIsolation<CopyOnPushPersistentTree<SumAddPolicy>>();
  checkOverflowIsolation<PointMaterializedPersistentTree<SumAddPolicy>>();
}

} // namespace
