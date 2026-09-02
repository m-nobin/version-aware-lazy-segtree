#include <valseg/brute_force_array.hpp>
#include <valseg/detail/sum_add_domain.hpp>
#include <valseg/detail/validation.hpp>
#include <valseg/policy.hpp>

#include <stdexcept>
#include <utility>

#if defined(VALSEG_ENABLE_BRUTE_FORCE_ALLOCATION_FAILURE_TEST_HOOK)
#include <new>
#endif

namespace valseg {

#if defined(VALSEG_ENABLE_BRUTE_FORCE_ALLOCATION_FAILURE_TEST_HOOK)
namespace detail {
namespace {

thread_local bool failNextReplacementAllocation = false;

void throwIfReplacementAllocationShouldFail() {
  if (std::exchange(failNextReplacementAllocation, false)) {
    throw std::bad_alloc();
  }
}

} // namespace

void failNextBruteForceReplacementAllocationForTesting() noexcept {
  failNextReplacementAllocation = true;
}

} // namespace detail
#endif

/*
=========================================================
Constructors
=========================================================
*/

BruteForceArray::BruteForceArray() {}

BruteForceArray::BruteForceArray(const std::vector<ValueType>& initial) {
  initialize(initial);
}

/*
=========================================================
Initialization
=========================================================
*/

void BruteForceArray::initialize(const std::vector<ValueType>& initial) {
  std::vector<std::vector<ValueType>> replacement;
#if defined(VALSEG_ENABLE_BRUTE_FORCE_ALLOCATION_FAILURE_TEST_HOOK)
  detail::throwIfReplacementAllocationShouldFail();
#endif
  replacement.push_back(initial);
  detail::validateCanonicalSums(replacement.front());
  versions.swap(replacement);
}

/*
=========================================================
Range Update
=========================================================
*/

std::size_t BruteForceArray::rangeAdd(std::size_t baseVersion, std::size_t left, std::size_t right,
                                      ValueType value) {
  validateVersion(baseVersion);
  validateRange(baseVersion, left, right);

  std::vector<ValueType> next = versions[baseVersion];
  for (std::size_t i = left; i <= right; ++i) {
    next[i] = checkedAdd(next[i], value);
  }
  detail::validateCanonicalSums(next);
  versions.push_back(std::move(next));

  return versions.size() - 1;
}

/*
=========================================================
Range Query
=========================================================
*/

BruteForceArray::ValueType BruteForceArray::rangeSum(std::size_t version, std::size_t left,
                                                     std::size_t right) const {
  validateVersion(version);
  validateRange(version, left, right);

  ValueType sum = 0;

  for (std::size_t i = left; i <= right; ++i) {
    sum = checkedAdd(sum, versions[version][i]);
  }

  return sum;
}

/*
=========================================================
Accessors
=========================================================
*/

const std::vector<BruteForceArray::ValueType>&
BruteForceArray::getVersion(std::size_t version) const {
  validateVersion(version);

  return versions[version];
}

std::size_t BruteForceArray::versionCount() const {
  return versions.size();
}

std::size_t BruteForceArray::size() const {
  if (versions.empty()) {
    return 0;
  }

  return versions.front().size();
}

/*
=========================================================
Validation
=========================================================
*/

void BruteForceArray::validateVersion(std::size_t version) const {
  detail::validateVersion(version, versions.size());
}

void BruteForceArray::validateRange(std::size_t version, std::size_t left,
                                    std::size_t right) const {
  // validateVersion(version) has already run, so versions[version] exists.
  detail::validateRange(versions[version].size(), left, right);
}

} // namespace valseg
