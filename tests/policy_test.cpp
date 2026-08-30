// Deterministic checks for the aggregate/action policies.
//
// The law checks are exhaustive over small domains, so they validate these
// three implementations, not the laws in general. The commutation checks are
// what the capability facts in include/valseg/policy.hpp claim: induced
// transformations commute for SumAdd and MinAdd, and a concrete AffineSum
// pair witnesses that they do not commute in general. The oracle checks tie
// the generic element-wise oracle to the production SumAdd tree.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "policy_oracle.hpp"
#include "valseg/persistent_lazy_segment_tree.hpp"
#include "valseg/policy.hpp"

namespace {

using valseg::AffineSumModPolicy;
using valseg::MinAddPolicy;
using valseg::PersistentLazySegmentTree;
using valseg::SumAddPolicy;
using valseg::testing::ElementWiseOracle;

using Affine = AffineSumModPolicy<13>;

// Every law from the policy contract, checked over the given domains. An
// aggregate is presented together with the lengths it is valid for; length
// zero admits only the identity, which the helper adds itself.
template <class Policy>
void checkLaws(const std::vector<typename Policy::Action>& actions,
               const std::vector<typename Policy::Aggregate>& aggregates,
               const std::vector<long long>& positiveLengths) {
  using Aggregate = typename Policy::Aggregate;

  for (const Aggregate x : aggregates) {
    ASSERT_EQ(Policy::combine(Policy::aggregateIdentity(), x), x);
    ASSERT_EQ(Policy::combine(x, Policy::aggregateIdentity()), x);
    for (const Aggregate y : aggregates) {
      for (const Aggregate z : aggregates) {
        ASSERT_EQ(Policy::combine(Policy::combine(x, y), z),
                  Policy::combine(x, Policy::combine(y, z)));
      }
    }
  }

  for (const auto& f : actions) {
    ASSERT_TRUE(Policy::compose(Policy::actionIdentity(), f) == f);
    ASSERT_TRUE(Policy::compose(f, Policy::actionIdentity()) == f);
    for (const auto& g : actions) {
      for (const auto& h : actions) {
        ASSERT_TRUE(Policy::compose(h, Policy::compose(g, f)) ==
                    Policy::compose(Policy::compose(h, g), f));
      }
    }
  }

  for (const auto& f : actions) {
    ASSERT_EQ(Policy::apply(f, Policy::aggregateIdentity(), 0), Policy::aggregateIdentity());
    for (const Aggregate x : aggregates) {
      for (const long long length : positiveLengths) {
        ASSERT_EQ(Policy::apply(Policy::actionIdentity(), x, length), x);
        for (const auto& g : actions) {
          ASSERT_EQ(Policy::apply(Policy::compose(g, f), x, length),
                    Policy::apply(g, Policy::apply(f, x, length), length));
        }
      }
    }
  }

  // Distribution over combine, including an empty side, whose only valid
  // aggregate is the identity.
  std::vector<std::pair<Aggregate, long long>> sides;
  sides.emplace_back(Policy::aggregateIdentity(), 0);
  for (const Aggregate x : aggregates) {
    for (const long long length : positiveLengths) {
      sides.emplace_back(x, length);
    }
  }
  for (const auto& f : actions) {
    for (const auto& [x, lenX] : sides) {
      for (const auto& [y, lenY] : sides) {
        ASSERT_EQ(Policy::apply(f, Policy::combine(x, y), lenX + lenY),
                  Policy::combine(Policy::apply(f, x, lenX), Policy::apply(f, y, lenY)));
      }
    }
  }
}

template <class Policy>
bool inducedTransformationsCommute(const std::vector<typename Policy::Action>& actions,
                                   const std::vector<typename Policy::Aggregate>& aggregates,
                                   const std::vector<long long>& positiveLengths) {
  for (const auto& f : actions) {
    for (const auto& g : actions) {
      for (const auto x : aggregates) {
        for (const long long length : positiveLengths) {
          if (Policy::apply(f, Policy::apply(g, x, length), length) !=
              Policy::apply(g, Policy::apply(f, x, length), length)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

const std::vector<long long> smallValues = {-3, -2, -1, 0, 1, 2, 3};
const std::vector<long long> smallLengths = {1, 2, 3, 7};

std::vector<Affine::Action> allAffineActions() {
  std::vector<Affine::Action> actions;
  for (std::uint64_t scale = 0; scale < 13; ++scale) {
    for (std::uint64_t shift = 0; shift < 13; ++shift) {
      actions.push_back(Affine::Action{scale, shift});
    }
  }
  return actions;
}

std::vector<Affine::Aggregate> allResidues() {
  std::vector<Affine::Aggregate> residues;
  for (std::uint64_t value = 0; value < 13; ++value) {
    residues.push_back(value);
  }
  return residues;
}

TEST(PolicyLaws, SumAddSmallDomain) {
  checkLaws<SumAddPolicy>(smallValues, smallValues, smallLengths);
}

TEST(PolicyLaws, SumAddWraparoundExtremes) {
  const long long big = std::numeric_limits<long long>::max();
  const long long small = std::numeric_limits<long long>::min();
  // Lengths stay below 2^62 so the test's own lenX + lenY cannot overflow.
  checkLaws<SumAddPolicy>({-1, 0, 1, big, small}, {-1, 0, 1, big, small}, {1, 2, 1000000007});
}

TEST(PolicyLaws, MinAddSmallDomain) {
  checkLaws<MinAddPolicy>(smallValues, smallValues, smallLengths);
}

TEST(PolicyLaws, AffineSumExhaustive) {
  checkLaws<Affine>(allAffineActions(), allResidues(), {1, 2, 3, 12, 13});
}

TEST(PolicyCommutation, SumAddAndMinAddCommute) {
  EXPECT_TRUE(inducedTransformationsCommute<SumAddPolicy>(smallValues, smallValues, smallLengths));
  EXPECT_TRUE(inducedTransformationsCommute<MinAddPolicy>(smallValues, smallValues, smallLengths));
}

TEST(PolicyCommutation, AffineSumDoesNotCommute) {
  // The minimal witness: doubling and incrementing depend on their order.
  const Affine::Action doubleIt{2, 0};
  const Affine::Action increment{1, 1};
  const Affine::Aggregate start = 0;
  EXPECT_EQ(Affine::apply(doubleIt, Affine::apply(increment, start, 1), 1), 2u);
  EXPECT_EQ(Affine::apply(increment, Affine::apply(doubleIt, start, 1), 1), 1u);
  EXPECT_FALSE(Affine::compose(doubleIt, increment) == Affine::compose(increment, doubleIt));
  EXPECT_FALSE(inducedTransformationsCommute<Affine>(allAffineActions(), allResidues(), {1}));
}

TEST(PolicyCapabilities, TraitsMatchTheTaxonomy) {
  static_assert(SumAddPolicy::kInducedActionsCommute);
  static_assert(SumAddPolicy::kCheckpointQueryProjectable);
  static_assert(MinAddPolicy::kInducedActionsCommute);
  static_assert(!MinAddPolicy::kCheckpointQueryProjectable);
  static_assert(!Affine::kInducedActionsCommute);
  static_assert(!Affine::kCheckpointQueryProjectable);
  SUCCEED();
}

// The oracle applies actions element by element in chronological order, so
// for SumAdd it must agree with the production persistent tree on every
// version of a seeded random stream.
TEST(PolicyOracle, MatchesPersistentLazySegmentTreeOnSumAdd) {
  const std::size_t n = 64;
  std::mt19937_64 rng(20260830);
  std::vector<long long> initial(n);
  for (auto& value : initial) {
    value = static_cast<long long>(rng() % 2001) - 1000;
  }

  PersistentLazySegmentTree tree(initial);
  ElementWiseOracle<SumAddPolicy> oracle(initial);

  for (int step = 0; step < 200; ++step) {
    std::size_t left = rng() % n;
    std::size_t right = rng() % n;
    if (left > right) {
      std::swap(left, right);
    }
    const long long delta = static_cast<long long>(rng() % 201) - 100;
    ASSERT_EQ(tree.rangeAdd(left, right, delta), oracle.rangeApply(left, right, delta));
  }

  ASSERT_EQ(tree.versionCount(), oracle.versionCount());
  for (std::size_t version = 0; version < oracle.versionCount(); ++version) {
    for (int probe = 0; probe < 8; ++probe) {
      std::size_t left = rng() % n;
      std::size_t right = rng() % n;
      if (left > right) {
        std::swap(left, right);
      }
      ASSERT_EQ(tree.rangeSum(version, left, right), oracle.rangeAggregate(version, left, right));
    }
  }
}

// Hand-checked chronological semantics for the noncommutative policy: the
// oracle must apply overlapping affine updates oldest first.
TEST(PolicyOracle, AffineSumAppliesActionsChronologically) {
  ElementWiseOracle<Affine> oracle({1, 1, 1});
  oracle.rangeApply(0, 1, Affine::Action{2, 0}); // [2, 2, 1]
  oracle.rangeApply(1, 2, Affine::Action{1, 3}); // [2, 5, 4]
  EXPECT_EQ(oracle.rangeAggregate(2, 0, 2), 11u % 13u);
  EXPECT_EQ(oracle.rangeAggregate(1, 0, 2), 5u);
  EXPECT_EQ(oracle.rangeAggregate(0, 0, 2), 3u);
}

} // namespace
