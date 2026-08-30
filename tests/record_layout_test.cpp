// The byte equations of docs/research/cost-model.md section 2, checked
// against sizeof and alignof. Every record is a struct of word-sized fields,
// so the equation is a sum of widths with no padding term; a target where a
// width differs fails the equation rather than the documented number.

#include <valseg/policy.hpp>
#include <valseg/policy_trees.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace {

using valseg::AffineSumModPolicy;
using valseg::CopyOnPushPersistentTree;
using valseg::MinAddPolicy;
using valseg::PointMaterializedPersistentTree;
using valseg::RetainedTagPersistentTree;
using valseg::SumAddPolicy;

using Affine = AffineSumModPolicy<998244353ULL>;

constexpr std::size_t I = sizeof(std::size_t);
constexpr std::size_t S = sizeof(long long);
constexpr std::size_t A = sizeof(long long);

// Tagged records: two child indices, an aggregate and an action.
static_assert(RetainedTagPersistentTree<SumAddPolicy>::kRecordBytes == 2 * I + S + A);
static_assert(CopyOnPushPersistentTree<SumAddPolicy>::kRecordBytes == 2 * I + S + A);
static_assert(RetainedTagPersistentTree<MinAddPolicy>::kRecordBytes == 2 * I + S + A);
// AffineSum's action is two words, and the tagged layouts grow with it.
static_assert(CopyOnPushPersistentTree<Affine>::kRecordBytes ==
              2 * I + sizeof(std::uint64_t) + sizeof(Affine::Action));
static_assert(sizeof(Affine::Action) == 2 * sizeof(std::uint64_t));
// Tagless records: two child indices and an aggregate.
static_assert(PointMaterializedPersistentTree<SumAddPolicy>::kRecordBytes == 2 * I + S);
static_assert(PointMaterializedPersistentTree<Affine>::kRecordBytes ==
              2 * I + sizeof(std::uint64_t));

TEST(RecordLayout, DocumentedTotalsHoldOnSixtyFourBitTargets) {
  if (sizeof(std::size_t) != 8) {
    GTEST_SKIP() << "documented totals assume 8-byte indices";
  }
  EXPECT_EQ(RetainedTagPersistentTree<SumAddPolicy>::kRecordBytes, 32u);
  EXPECT_EQ(CopyOnPushPersistentTree<SumAddPolicy>::kRecordBytes, 32u);
  EXPECT_EQ(PointMaterializedPersistentTree<SumAddPolicy>::kRecordBytes, 24u);
  EXPECT_EQ(CopyOnPushPersistentTree<Affine>::kRecordBytes, 40u);
  EXPECT_EQ(RetainedTagPersistentTree<SumAddPolicy>::kRecordAlignment, 8u);
  EXPECT_EQ(PointMaterializedPersistentTree<SumAddPolicy>::kRecordAlignment, 8u);

  // The composite layouts of the SumAdd baselines, as their headers assert.
  const std::size_t V = I;
  EXPECT_EQ(2 * I + S + A + I + 2 * (V + S + A), 88u);
  EXPECT_EQ(I + 3 * (V + 2 * I + S + A), 128u);
  EXPECT_EQ(S + A, 16u);
  EXPECT_EQ(2 * I + A, 24u);
}

} // namespace
