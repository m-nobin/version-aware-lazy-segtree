#include <valseg/brute_force_array.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {

std::atomic<bool> failNextAllocation{false};

void failIfRequested() {
  if (failNextAllocation.exchange(false)) {
    throw std::bad_alloc();
  }
}

void* allocate(std::size_t size) {
  failIfRequested();
  if (size == 0) {
    size = 1;
  }
  if (void* memory = std::malloc(size)) {
    return memory;
  }
  throw std::bad_alloc();
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
  failIfRequested();
  if (size == 0) {
    size = 1;
  }
#if defined(_MSC_VER)
  if (void* memory = _aligned_malloc(size, alignment)) {
    return memory;
  }
#else
  void* memory = nullptr;
  if (::posix_memalign(&memory, alignment, size) == 0) {
    return memory;
  }
#endif
  throw std::bad_alloc();
}

void freeAligned(void* memory) noexcept {
#if defined(_MSC_VER)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}

} // namespace

void* operator new(std::size_t size) {
  return allocate(size);
}

void* operator new[](std::size_t size) {
  return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
  } catch (...) {
    return nullptr;
  }
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

void operator delete(void* memory, const std::nothrow_t&) noexcept {
  std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
  std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
  freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
  freeAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
  freeAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
  freeAligned(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
  freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
  freeAligned(memory);
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
