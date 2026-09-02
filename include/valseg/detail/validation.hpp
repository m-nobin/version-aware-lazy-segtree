#ifndef VALSEG_DETAIL_VALIDATION_HPP
#define VALSEG_DETAIL_VALIDATION_HPP

#include <cstddef>
#include <stdexcept>

namespace valseg::detail {

/**
 * @brief Require that a structure has published at least one version.
 *
 * @param versionCount Number of published versions.
 * @throws std::runtime_error No version has been published.
 */
inline void validateInitialized(std::size_t versionCount) {
  if (versionCount == 0) {
    throw std::runtime_error("Tree has no versions.");
  }
}

/**
 * @brief Require that a version number names a published version.
 *
 * @param version      Zero-based version number.
 * @param versionCount Number of published versions.
 * @throws std::out_of_range version is not smaller than versionCount.
 */
inline void validateVersion(std::size_t version, std::size_t versionCount) {
  if (version >= versionCount) {
    throw std::out_of_range("Invalid version number.");
  }
}

/**
 * @brief Require that a tree holds at least one logical element.
 *
 * @param arraySize Number of logical elements.
 * @throws std::runtime_error The tree is empty.
 */
inline void validateNonEmpty(std::size_t arraySize) {
  if (arraySize == 0) {
    throw std::runtime_error("Tree is empty.");
  }
}

/**
 * @brief Require that an inclusive index range lies inside a logical array.
 *
 * Endpoint order is checked before the upper bound, so a reversed range is
 * reported as such even when it also exceeds the array.
 *
 * @param arraySize Number of logical elements.
 * @param left      Left index, inclusive.
 * @param right     Right index, inclusive.
 * @throws std::invalid_argument  left is greater than right.
 * @throws std::out_of_range      right is not smaller than arraySize.
 */
inline void validateRange(std::size_t arraySize, std::size_t left, std::size_t right) {
  if (left > right) {
    throw std::invalid_argument("Left index is greater than right index.");
  }
  if (right >= arraySize) {
    throw std::out_of_range("Range exceeds array size.");
  }
}

} // namespace valseg::detail

#endif
