#include "allocation_counter.hpp"

namespace valseg::bench {

bool allocationCountingEnabled() {
  return false;
}

void resetAllocationStats() {}

AllocationStats allocationStats() {
  return {0, 0, 0};
}

} // namespace valseg::bench
