#ifndef VALSEG_BENCH_PROCESS_MEMORY_HPP
#define VALSEG_BENCH_PROCESS_MEMORY_HPP

#include <cstddef>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// psapi.h must follow windows.h.
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace valseg::bench {

/**
 * Peak resident set size of this process in bytes, or zero when the platform
 * does not report it.
 *
 * This is the operating system's high-water mark for the whole process, so it
 * only rises: inside one process it is the largest footprint of any cell run
 * so far, not the footprint of the last one. It is a per-trial measurement
 * only under the fresh-process orchestration of the registered protocol,
 * where one process runs one cell. It is recorded beside, never instead of,
 * the payload and allocator figures, which are per trial.
 */
inline std::size_t peakResidentBytes() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return 0;
  }
  return static_cast<std::size_t>(counters.PeakWorkingSetSize);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#if defined(__APPLE__)
  // ru_maxrss is in bytes on macOS and in kibibytes on Linux and the BSDs.
  return static_cast<std::size_t>(usage.ru_maxrss);
#else
  return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

} // namespace valseg::bench

#endif
