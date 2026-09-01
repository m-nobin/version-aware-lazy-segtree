#ifndef VALSEG_DETAIL_CHECKED_SIZE_HPP
#define VALSEG_DETAIL_CHECKED_SIZE_HPP

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace valseg::detail {

/**
 * @brief Add two sizes without unsigned wraparound.
 *
 * @param left  Left operand.
 * @param right Right operand.
 * @return The exact sum.
 * @throws std::overflow_error The exact sum is not representable as std::size_t.
 */
inline std::size_t checkedSizeAdd(std::size_t left, std::size_t right) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw std::overflow_error("size calculation is not representable");
  }
  return left + right;
}

/**
 * @brief Multiply two sizes without unsigned wraparound.
 *
 * @param left  Left operand.
 * @param right Right operand.
 * @return The exact product.
 * @throws std::overflow_error The exact product is not representable as std::size_t.
 */
inline std::size_t checkedSizeMultiply(std::size_t left, std::size_t right) {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    throw std::overflow_error("size calculation is not representable");
  }
  return left * right;
}

/**
 * @brief Number of nodes in the canonical binary tree over n leaves.
 *
 * @param n Leaf count; zero denotes the empty tree.
 * @return Zero for an empty tree, otherwise 2n - 1.
 * @throws std::overflow_error The exact count is not representable as std::size_t.
 */
inline std::size_t canonicalNodeCount(std::size_t n) {
  if (n == 0) {
    return 0;
  }
  return checkedSizeAdd(checkedSizeMultiply(2, n - 1), 1);
}

/**
 * @brief Slots used by a 4n heap-indexed lazy-tree representation.
 *
 * @param n Leaf count.
 * @return Exactly 4n slots.
 * @throws std::overflow_error The exact count is not representable as std::size_t.
 */
inline std::size_t lazyStorageCount(std::size_t n) {
  return checkedSizeMultiply(4, n);
}

/**
 * @brief Overflow-safe midpoint of an inclusive interval.
 *
 * @param left  Left endpoint.
 * @param right Right endpoint, not smaller than left.
 * @return left + floor((right - left) / 2).
 */
inline std::size_t midpoint(std::size_t left, std::size_t right) {
  return left + (right - left) / 2;
}

/**
 * @brief Length of an inclusive interval.
 *
 * @param left  Left endpoint.
 * @param right Right endpoint.
 * @return right - left + 1.
 * @throws std::invalid_argument left is greater than right.
 * @throws std::overflow_error The exact length is not representable as std::size_t.
 */
inline std::size_t inclusiveLength(std::size_t left, std::size_t right) {
  if (left > right) {
    throw std::invalid_argument("left endpoint exceeds right endpoint");
  }
  return checkedSizeAdd(right - left, 1);
}

/**
 * @brief Height of the canonical binary tree, ceil(log2(n)).
 *
 * @param n Positive leaf count.
 * @return Tree height; zero for one leaf.
 * @throws std::invalid_argument n is zero.
 */
inline std::size_t treeHeight(std::size_t n) {
  if (n == 0) {
    throw std::invalid_argument("tree size must be positive");
  }
  std::size_t height = 0;
  --n;
  while (n != 0) {
    n >>= 1U;
    ++height;
  }
  return height;
}

/**
 * @brief Compute the triangular number n(n + 1) / 2 without wraparound.
 *
 * @param n Nonnegative argument.
 * @return The exact triangular number.
 * @throws std::overflow_error The exact result is not representable as std::size_t.
 */
inline std::size_t triangularNumber(std::size_t n) {
  if ((n & 1U) == 0U) {
    return checkedSizeMultiply(n / 2, checkedSizeAdd(n, 1));
  }
  return checkedSizeMultiply(n, n / 2 + 1);
}

} // namespace valseg::detail

#endif
