#include <cstdlib>
#include <cstring>
#include <new>

#include "allocation_counter.hpp"

namespace valseg::bench {
namespace {

std::size_t liveBytes = 0;
std::size_t peakBytes = 0;
std::size_t allocations = 0;

/**
 * Every block carries its own size in a header wide enough to keep the
 * returned pointer suitably aligned for any type, which is what operator new
 * has to guarantee.
 */
constexpr std::size_t headerSize = alignof(std::max_align_t) > sizeof(std::size_t)
                                       ? alignof(std::max_align_t)
                                       : sizeof(std::size_t);

void* allocate(std::size_t size) {
  void* raw = std::malloc(size + headerSize);
  if (raw == nullptr) {
    return nullptr;
  }
  std::memcpy(raw, &size, sizeof(size));
  liveBytes += size;
  allocations += 1;
  if (liveBytes > peakBytes) {
    peakBytes = liveBytes;
  }
  return static_cast<char*>(raw) + headerSize;
}

void release(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  char* raw = static_cast<char*>(pointer) - headerSize;
  std::size_t size = 0;
  std::memcpy(&size, raw, sizeof(size));
  liveBytes -= size;
  std::free(raw);
}

} // namespace

bool allocationCountingEnabled() {
  return true;
}

void resetAllocationStats() {
  liveBytes = 0;
  peakBytes = 0;
  allocations = 0;
}

AllocationStats allocationStats() {
  return {liveBytes, peakBytes, allocations};
}

} // namespace valseg::bench

void* operator new(std::size_t size) {
  void* block = valseg::bench::allocate(size);
  if (block == nullptr) {
    throw std::bad_alloc();
  }
  return block;
}

void* operator new[](std::size_t size) {
  void* block = valseg::bench::allocate(size);
  if (block == nullptr) {
    throw std::bad_alloc();
  }
  return block;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return valseg::bench::allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return valseg::bench::allocate(size);
}

void operator delete(void* pointer) noexcept {
  valseg::bench::release(pointer);
}

void operator delete[](void* pointer) noexcept {
  valseg::bench::release(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  valseg::bench::release(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  valseg::bench::release(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  valseg::bench::release(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  valseg::bench::release(pointer);
}
