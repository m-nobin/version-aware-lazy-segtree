#include <valseg/brute_force_array.hpp>
#include <valseg/detail/sum_add_domain.hpp>
#include <valseg/policy.hpp>

#include <stdexcept>
#include <utility>

namespace valseg {

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
  if (version >= versions.size()) {
    throw std::out_of_range("Invalid version number.");
  }
}

void BruteForceArray::validateRange(std::size_t version, std::size_t left,
                                    std::size_t right) const {
  // Precondition:
  // validateVersion(version) has already been called,
  // so versions[version] is guaranteed to exist.

  const std::size_t n = versions[version].size();

  if (left > right) {
    throw std::invalid_argument("Left index is greater than right index.");
  }

  if (right >= n) {
    throw std::out_of_range("Range exceeds array size.");
  }
}

} // namespace valseg
