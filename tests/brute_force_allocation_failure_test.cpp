#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

namespace {

std::atomic<bool> failNextAllocation{false};

void* allocate(std::size_t size) {
  if (failNextAllocation.exchange(false)) {
    throw std::bad_alloc();
  }
  if (void* memory = std::malloc(size)) {
    return memory;
  }
  throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) {
  return allocate(size);
}

void* operator new[](std::size_t size) {
  return allocate(size);
}

void operator delete(void* memory) noexcept {
  std::free(memory);
}

void operator delete[](void* memory) noexcept {
  std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
  std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
  std::free(memory);
}

TEST(BruteForceAllocationFailureTest, InitializePreservesHistoryWhenReplacementCopyCannotAllocate) {
  valseg::BruteForceArray array({1, 2, 3});
  static_cast<void>(array.rangeAdd(0, 0, 2, 4));
  const std::vector<long long> replacement = {9, 8, 7, 6};

  failNextAllocation = true;
  EXPECT_THROW(array.initialize(replacement), std::bad_alloc);

  EXPECT_EQ(array.versionCount(), 2U);
  EXPECT_EQ(array.getVersion(0), (std::vector<long long>{1, 2, 3}));
  EXPECT_EQ(array.getVersion(1), (std::vector<long long>{5, 6, 7}));
}
