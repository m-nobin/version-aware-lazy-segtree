#ifndef VALSEG_BRUTE_FORCE_ARRAY_HPP
#define VALSEG_BRUTE_FORCE_ARRAY_HPP

#include <cstddef>
#include <stdexcept>
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
 */
class BruteForceArray {
public:
  using ValueType = long long;

  /**
   * @brief Construct an empty versioned array.
   */
  BruteForceArray();

  /**
   * @brief Construct Version 0 from an initial array.
   *
   * @param initial Initial array.
   */
  explicit BruteForceArray(const std::vector<ValueType> &initial);

  /**
   * @brief Initialize Version 0.
   *
   * Clears all previous versions.
   *
   * @param initial Initial array.
   */
  void initialize(const std::vector<ValueType> &initial);

  /**
   * @brief Perform a range addition on a copied version.
   *
   * A new version is first created from baseVersion,
   * then value is added to every element in [left, right].
   *
   * @param baseVersion Version to copy.
   * @param left Left index (inclusive).
   * @param right Right index (inclusive).
   * @param value Value to add.
   *
   * @return Newly created version number.
   */
  std::size_t rangeAdd(std::size_t baseVersion, std::size_t left,
                       std::size_t right, ValueType value);

  /**
   * @brief Compute the range sum.
   *
   * @param version Version to query.
   * @param left Left index (inclusive).
   * @param right Right index (inclusive).
   *
   * @return Sum over [left, right].
   */
  ValueType rangeSum(std::size_t version, std::size_t left,
                     std::size_t right) const;

  /**
   * @brief Get a read-only reference to a version.
   */
  const std::vector<ValueType> &getVersion(std::size_t version) const;

  /**
   * @brief Number of versions currently stored.
   */
  std::size_t versionCount() const;

  /**
   * @brief Size of each version.
   */
  std::size_t size() const;

private:
  /**
   * @brief Create a new version by copying an existing version.
   *
   * Used internally by rangeAdd(). This function is kept private
   * to prevent callers from creating duplicate versions without
   * applying any update.
   *
   * @param baseVersion Version to copy.
   * @return Index of the newly created version.
   */
  std::size_t createVersion(std::size_t baseVersion);

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
  void validateRange(std::size_t version, std::size_t left,
                     std::size_t right) const;
};

} // namespace valseg

#endif