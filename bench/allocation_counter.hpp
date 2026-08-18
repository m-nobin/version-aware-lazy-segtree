#ifndef VALSEG_BENCH_ALLOCATION_COUNTER_HPP
#define VALSEG_BENCH_ALLOCATION_COUNTER_HPP

#include <cstddef>

namespace valseg::bench {

/**
 * Bytes actually requested from the allocator, as opposed to the payload each
 * header documents.
 *
 * The difference is not cosmetic: every structure here stores its nodes in a
 * std::vector, and a growing vector holds up to twice the bytes its elements
 * occupy, plus the old block until the copy finishes. A payload figure alone
 * understates the real footprint, so both are recorded.
 */
struct AllocationStats {
  std::size_t liveBytes;   ///< requested and not yet freed
  std::size_t peakBytes;   ///< high-water mark since the last reset
  std::size_t allocations; ///< calls to operator new since the last reset
};

/**
 * @brief Whether this binary was linked against the counting allocator.
 *
 * Counting is a link-time choice, not a runtime flag: the counting build adds
 * a header to every allocation, which would perturb the timings it sits next
 * to. valseg_bench measures time, valseg_bench_alloc measures memory, and no
 * run reports both from the same binary.
 *
 * @return True in valseg_bench_alloc, false in valseg_bench.
 */
bool allocationCountingEnabled();

/**
 * @brief Zero the live, peak and call counters.
 */
void resetAllocationStats();

/**
 * @brief Read the counters.
 *
 * @return Current statistics; all zero when counting is disabled.
 */
AllocationStats allocationStats();

} // namespace valseg::bench

#endif
