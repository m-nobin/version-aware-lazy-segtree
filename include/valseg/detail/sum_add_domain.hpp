#ifndef VALSEG_DETAIL_SUM_ADD_DOMAIN_HPP
#define VALSEG_DETAIL_SUM_ADD_DOMAIN_HPP

#include <valseg/detail/checked_size.hpp>
#include <valseg/policy.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace valseg::detail {

/**
 * @brief Validate and return the canonical sum over one recursive segment.
 *
 * @param values Logical leaf values.
 * @param left   Left index of the canonical segment, inclusive.
 * @param right  Right index of the canonical segment, inclusive.
 * @return Exact segment sum.
 * @throws std::overflow_error A canonical sum is not representable.
 */
inline long long validateCanonicalSums(const std::vector<long long>& values, std::size_t left,
                                       std::size_t right) {
  if (left == right) {
    return values[left];
  }
  const std::size_t middle = midpoint(left, right);
  return checkedAdd(validateCanonicalSums(values, left, middle),
                    validateCanonicalSums(values, middle + 1, right));
}

/**
 * @brief Validate every sum in the canonical tree over the supplied leaves.
 *
 * @param values Logical leaf values; an empty vector is valid.
 * @throws std::overflow_error A canonical sum is not representable.
 */
inline void validateCanonicalSums(const std::vector<long long>& values) {
  if (!values.empty()) {
    static_cast<void>(validateCanonicalSums(values, 0, values.size() - 1));
  }
}

/**
 * @brief Cheap common-domain guard for lazy SumAdd representations.
 *
 * The scalar bound proves the usual small-value path without changing node
 * layouts or benchmark work. When that conservative proof is unavailable, a
 * caller-provided traversal materializes the current leaves once and the guard
 * validates every proposed leaf and canonical sum exactly before publication.
 */
class SumAddDomainGuard {
public:
  /** @brief Unsigned type able to represent the magnitude of long long minimum. */
  using Bound = std::uint64_t;

  /** @brief Construct an empty-domain guard. */
  SumAddDomainGuard() = default;

  /**
   * @brief Construct a guard for an initialized logical array.
   *
   * @param values Initial logical leaf values.
   */
  explicit SumAddDomainGuard(const std::vector<long long>& values)
      : magnitudeBound(maxMagnitude(values)) {}

  /**
   * @brief Preflight a proposed range addition without publishing state.
   *
   * @tparam Materialize Callable that appends the current leaves in index
   *                     order to a supplied vector.
   * @param size        Number of logical leaves.
   * @param left        Updated left index, inclusive.
   * @param right       Updated right index, inclusive.
   * @param value       Delta added to every selected leaf.
   * @param materialize Exact read-only traversal used only when the scalar
   *                    proof is inconclusive.
   * @return Magnitude bound to commit after the caller publishes the update.
   * @throws std::overflow_error A proposed leaf or canonical sum is not
   *                             representable.
   * @throws std::bad_alloc      Exact fallback storage cannot be allocated.
   * @throws std::logic_error    materialize returns the wrong leaf count.
   */
  template <class Materialize>
  Bound validateRangeAdd(std::size_t size, std::size_t left, std::size_t right, long long value,
                         Materialize&& materialize) const {
    const Bound conservative = saturatedAdd(magnitudeBound, magnitude(value));
    if (provesCanonicalRepresentability(conservative, size)) {
      return conservative;
    }

    std::vector<long long> proposed;
    proposed.reserve(size);
    std::forward<Materialize>(materialize)(proposed);
    if (proposed.size() != size) {
      throw std::logic_error("numeric-domain traversal produced the wrong number of elements");
    }
    for (std::size_t index = left; index <= right; ++index) {
      proposed[index] = checkedAdd(proposed[index], value);
    }
    validateCanonicalSums(proposed);
    return maxMagnitude(proposed);
  }

  /**
   * @brief Publish the bound returned by a successful preflight.
   *
   * @param nextBound Bound returned by validateRangeAdd().
   */
  void commit(Bound nextBound) noexcept {
    magnitudeBound = nextBound;
  }

private:
  Bound magnitudeBound = 0;

  static Bound magnitude(long long value) noexcept {
    if (value >= 0) {
      return static_cast<Bound>(value);
    }
    return static_cast<Bound>(-(value + 1)) + 1;
  }

  static Bound maxMagnitude(const std::vector<long long>& values) noexcept {
    Bound maximum = 0;
    for (const long long value : values) {
      const Bound current = magnitude(value);
      if (current > maximum) {
        maximum = current;
      }
    }
    return maximum;
  }

  static Bound saturatedAdd(Bound left, Bound right) noexcept {
    const Bound maximum = std::numeric_limits<Bound>::max();
    return right > maximum - left ? maximum : left + right;
  }

  static bool provesCanonicalRepresentability(Bound bound, std::size_t size) noexcept {
    if (bound == 0 || size == 0) {
      return true;
    }
    const Bound maximum = static_cast<Bound>(std::numeric_limits<long long>::max());
    if (size > maximum) {
      return false;
    }
    return bound <= maximum / static_cast<Bound>(size);
  }
};

} // namespace valseg::detail

#endif
