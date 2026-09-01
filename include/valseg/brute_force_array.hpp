#ifndef VALSEG_BRUTE_FORCE_ARRAY_HPP
#define VALSEG_BRUTE_FORCE_ARRAY_HPP

#include <cstddef>
#include <vector>

namespace valseg {

/**
 * @brief Brute-force implementation of a versioned array.
 *
 * Every update creates a completely new copy of the array.
 * Older versions remain unchanged.
 *
 * This implementation prioritizes correctness over performance
 * and serves as the correctness oracle for validating the
 * partially persistent segment tree.
 *
 * Numeric domain: values model mathematical signed integers in a long long
 * representation. Initialization and updates are accepted only when every
 * element, canonical segment sum and evaluated arithmetic intermediate is
 * representable. Queries apply the same rule to the requested sum. An
 * unrepresentable operation throws std::overflow_error before changing the
 * stored history.
 */
class BruteForceArray {
public:
  /**
   * @brief Element and sum type for every operation.
   */
  using ValueType = long long;

  /**
   * @brief Construct an empty versioned array.
   */
  BruteForceArray();

  /**
   * @brief Construct Version 0 from an initial array.
   *
   * @param initial Initial array.
   *
   * @throws std::bad_alloc Allocation of the initial version failed.
   * @throws std::overflow_error A canonical segment sum is not representable.
   */
  explicit BruteForceArray(const std::vector<ValueType>& initial);

  /**
   * @brief Initialize Version 0.
   *
   * Clears all previous versions.
   *
   * @param initial Initial array.
   *
   * @throws std::bad_alloc Allocation of the replacement version failed; the
   *                        previous history is left unchanged.
   * @throws std::overflow_error A canonical segment sum is not representable;
   *                             the previous history is left unchanged.
   */
  void initialize(const std::vector<ValueType>& initial);

  /**
   * @brief Perform a range addition on a copied version.
   *
   * A replacement version is copied from baseVersion, updated and validated
   * before it is published.
   *
   * @param baseVersion Version to copy.
   * @param left Left index (inclusive).
   * @param right Right index (inclusive).
   * @param value Value to add.
   *
   * @return Newly created version number.
   *
   * @throws std::out_of_range      baseVersion is not a stored version, or
   *                                right is not smaller than size().
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::overflow_error    An updated value or canonical segment sum
   *                                is not representable; no version is added.
   */
  std::size_t rangeAdd(std::size_t baseVersion, std::size_t left, std::size_t right,
                       ValueType value);

  /**
   * @brief Compute the range sum.
   *
   * @param version Version to query.
   * @param left Left index (inclusive).
   * @param right Right index (inclusive).
   *
   * @return Sum over [left, right].
   *
   * @throws std::out_of_range      version is not a stored version, or right
   *                                is not smaller than size().
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::overflow_error    The exact requested sum is not representable.
   */
  ValueType rangeSum(std::size_t version, std::size_t left, std::size_t right) const;

  /**
   * @brief Get a read-only reference to a version.
   *
   * @param version Version to read.
   *
   * @return Reference to the stored array of that version.
   *
   * @throws std::out_of_range version is not a stored version.
   */
  const std::vector<ValueType>& getVersion(std::size_t version) const;

  /**
   * @brief Number of versions currently stored.
   *
   * @return Number of stored versions.
   */
  std::size_t versionCount() const;

  /**
   * @brief Size of each version.
   *
   * @return Number of elements in every version, or zero before initialization.
   */
  std::size_t size() const;

private:
  /**
   * Each entry stores one complete array version.
   */
  std::vector<std::vector<ValueType>> versions;

  /**
   * Validate version number.
   */
  void validateVersion(std::size_t version) const;

  /**
   * Validate query/update range.
   */
  void validateRange(std::size_t version, std::size_t left, std::size_t right) const;
};

} // namespace valseg

#endif
