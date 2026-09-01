#include <valseg/brute_force_array.hpp>
#include <valseg/buffered_path_copying_segment_tree.hpp>
#include <valseg/checkpointing_segment_tree.hpp>
#include <valseg/fat_node_persistent_segment_tree.hpp>
#include <valseg/full_copy_persistent_segment_tree.hpp>
#include <valseg/lazy_segment_tree.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/point_only_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../bench/copy_on_push_segment_tree.hpp"

namespace {

template <class Tree> class PersistentHarness {
public:
  explicit PersistentHarness(const std::vector<long long>& values) {
    tree.initialize(values);
  }

  void initialize(const std::vector<long long>& values) {
    tree.initialize(values);
  }

  void add(std::size_t left, std::size_t right, long long value) {
    static_cast<void>(tree.rangeAdd(left, right, value));
  }

  long long sum(std::size_t left, std::size_t right) const {
    return tree.rangeSum(tree.versionCount() - 1, left, right);
  }

  std::pair<std::size_t, std::size_t> state() const {
    return {tree.versionCount(), tree.nodeCount()};
  }

private:
  Tree tree;
};

class BruteForceHarness {
public:
  explicit BruteForceHarness(const std::vector<long long>& values) : array(values) {}

  void initialize(const std::vector<long long>& values) {
    array.initialize(values);
  }

  void add(std::size_t left, std::size_t right, long long value) {
    static_cast<void>(array.rangeAdd(array.versionCount() - 1, left, right, value));
  }

  long long sum(std::size_t left, std::size_t right) const {
    return array.rangeSum(array.versionCount() - 1, left, right);
  }

  std::pair<std::size_t, std::size_t> state() const {
    return {array.versionCount(), 0};
  }

private:
  valseg::BruteForceArray array;
};

class LazyHarness {
public:
  explicit LazyHarness(const std::vector<long long>& values) : tree(values) {}

  void initialize(const std::vector<long long>& values) {
    tree.initialize(values);
  }

  void add(std::size_t left, std::size_t right, long long value) {
    tree.rangeAdd(left, right, value);
  }

  long long sum(std::size_t left, std::size_t right) const {
    return tree.rangeSum(left, right);
  }

  std::pair<std::size_t, std::size_t> state() const {
    return {1, tree.size()};
  }

private:
  valseg::LazySegmentTree tree;
};

using NumericImplementations =
    ::testing::Types<BruteForceHarness, LazyHarness,
                     PersistentHarness<valseg::PersistentLazySegmentTree>,
                     PersistentHarness<valseg::FullCopyPersistentSegmentTree>,
                     PersistentHarness<valseg::PointOnlyPersistentSegmentTree>,
                     PersistentHarness<valseg::CheckpointingSegmentTree>,
                     PersistentHarness<valseg::BufferedPathCopyingSegmentTree>,
                     PersistentHarness<valseg::FatNodePersistentSegmentTree>,
                     PersistentHarness<valseg::bench::CopyOnPushSegmentTree>>;

template <class Harness> class NumericContractTest : public ::testing::Test {};

struct NumericImplementationNames {
  template <class> static std::string GetName(int index) {
    return "Implementation" + std::to_string(index);
  }
};

TYPED_TEST_SUITE(NumericContractTest, NumericImplementations, NumericImplementationNames);

TYPED_TEST(NumericContractTest, InitializationRejectsUnrepresentableCanonicalSumWithoutMutation) {
  const long long maximum = std::numeric_limits<long long>::max();
  TypeParam structure({4, -1});
  const auto before = structure.state();

  EXPECT_THROW(structure.initialize({maximum, 1}), std::overflow_error);
  EXPECT_EQ(structure.state(), before);
  EXPECT_EQ(structure.sum(0, 1), 3);
}

TYPED_TEST(NumericContractTest, DeltaTimesSegmentLengthOverflowPublishesNothing) {
  const long long maximum = std::numeric_limits<long long>::max();
  TypeParam structure({0, 0});
  const auto before = structure.state();

  EXPECT_THROW(structure.add(0, 1, maximum), std::overflow_error);
  EXPECT_EQ(structure.state(), before);
  EXPECT_EQ(structure.sum(0, 1), 0);
}

TYPED_TEST(NumericContractTest, AccumulatedValueOrLazyTagOverflowPublishesNothing) {
  const long long maximum = std::numeric_limits<long long>::max();
  TypeParam structure({0});
  structure.add(0, 0, maximum);
  const auto before = structure.state();

  EXPECT_THROW(structure.add(0, 0, 1), std::overflow_error);
  EXPECT_EQ(structure.state(), before);
  EXPECT_EQ(structure.sum(0, 0), maximum);
}

TYPED_TEST(NumericContractTest, QueryRejectsAnUnrepresentableNoncanonicalRangeSum) {
  const long long maximum = std::numeric_limits<long long>::max();
  TypeParam structure({-maximum, maximum, maximum, -maximum});

  EXPECT_THROW(structure.sum(1, 2), std::overflow_error);
  EXPECT_EQ(structure.sum(0, 3), 0);
}

TYPED_TEST(NumericContractTest, SignedBoundariesRemainValidWhenEveryIntermediateFits) {
  const long long maximum = std::numeric_limits<long long>::max();
  const long long minimum = std::numeric_limits<long long>::min();
  TypeParam upper({maximum});
  TypeParam lower({minimum});

  EXPECT_EQ(upper.sum(0, 0), maximum);
  EXPECT_EQ(lower.sum(0, 0), minimum);
}

} // namespace
