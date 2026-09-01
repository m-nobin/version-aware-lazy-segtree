#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

#include <new>
#include <vector>

namespace valseg::detail {

void failNextBruteForceReplacementAllocationForTesting() noexcept;

} // namespace valseg::detail

TEST(BruteForceAllocationFailureTest, InitializePreservesHistoryWhenReplacementCopyCannotAllocate) {
  valseg::BruteForceArray array({1, 2, 3});
  static_cast<void>(array.rangeAdd(0, 0, 2, 4));
  const std::vector<long long> replacement = {9, 8, 7, 6};

  valseg::detail::failNextBruteForceReplacementAllocationForTesting();
  EXPECT_THROW(array.initialize(replacement), std::bad_alloc);

  EXPECT_EQ(array.versionCount(), 2U);
  EXPECT_EQ(array.getVersion(0), (std::vector<long long>{1, 2, 3}));
  EXPECT_EQ(array.getVersion(1), (std::vector<long long>{5, 6, 7}));

  EXPECT_NO_THROW(array.initialize(replacement));
  EXPECT_EQ(array.versionCount(), 1U);
  EXPECT_EQ(array.getVersion(0), replacement);
}
