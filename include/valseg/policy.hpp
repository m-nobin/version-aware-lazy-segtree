#ifndef VALSEG_POLICY_HPP
#define VALSEG_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace valseg {

/**
 * @brief Aggregate and action policies for segment trees with range actions.
 *
 * A policy names an aggregate type, an action type and five operations:
 *
 *  - combine(x, y): ordered aggregate composition; children are combined in
 *    index order and combine need not commute;
 *  - aggregateIdentity(): the aggregate of an empty segment;
 *  - compose(newer, older): action composition, applying older first;
 *  - actionIdentity(): the action that changes nothing;
 *  - apply(action, aggregate, length): the action's effect on the aggregate
 *    of a segment of the given nonnegative length.
 *
 * A valid policy satisfies, for all valid aggregates and lengths in its
 * mathematical domain:
 *
 *  - combine is associative with identity aggregateIdentity();
 *  - compose is associative with identity actionIdentity();
 *  - apply(actionIdentity(), x, len) == x;
 *  - apply(compose(g, f), x, len) == apply(g, apply(f, x, len), len);
 *  - apply(f, combine(x, y), lenX + lenY)
 *      == combine(apply(f, x, lenX), apply(f, y, lenY));
 *  - apply(f, aggregateIdentity(), 0) == aggregateIdentity().
 *
 * A valid aggregate for length len is one reachable as the aggregate of some
 * segment of len elements; in particular the only valid aggregate for length
 * zero is aggregateIdentity(). SumAddPolicy and MinAddPolicy model mathematical
 * integers but use long long as their machine representation. They throw
 * std::overflow_error before an exact mathematical result leaves that
 * representation. Consequently, their law checks use admissible evaluations in
 * which every intermediate result is representable. tests/policy_test.cpp checks
 * every law exhaustively over small domains for the three policies below and
 * separately checks the overflow boundary. Those checks validate these
 * implementations, not the laws in general.
 *
 * Each policy carries two constexpr capability facts. They record what the
 * accompanying analysis in docs/research/capability-taxonomy.md claims for
 * the policy; the compiler propagates them, it does not prove them.
 *
 *  - kInducedActionsCommute: for every length, the induced transformations
 *    x -> apply(f, x, len) commute pairwise over valid aggregates. This is
 *    the property the tag-retaining persistent tree needs. A tag retained on
 *    a node through a partial descent is older than any tag placed below it
 *    by that descent, yet a query applies the ancestor's tag outside the
 *    descendant's; the tree order does not record which tag is newer.
 *  - kCheckpointQueryProjectable: a logged update's contribution to a later
 *    range query can be computed from the action and the overlap length
 *    alone, without reconstructing intermediate state. This is the property
 *    the checkpoint-plus-log baseline needs; its replay is not generic.
 */

/**
 * @brief Exact signed addition with a defined representability failure.
 *
 * @param left  First addend.
 * @param right Second addend.
 *
 * @return The exact sum when it is representable as long long.
 *
 * @throws std::overflow_error if the exact sum is not representable.
 */
inline long long checkedAdd(long long left, long long right) {
  const long long maximum = std::numeric_limits<long long>::max();
  const long long minimum = std::numeric_limits<long long>::min();
  if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right)) {
    throw std::overflow_error("policy addition is not representable");
  }
  return left + right;
}

/**
 * @brief Exact signed multiplication with a defined representability failure.
 *
 * @param left  First factor.
 * @param right Second factor.
 *
 * @return The exact product when it is representable as long long.
 *
 * @throws std::overflow_error if the exact product is not representable.
 */
inline long long checkedMultiply(long long left, long long right) {
  const long long maximum = std::numeric_limits<long long>::max();
  const long long minimum = std::numeric_limits<long long>::min();

  if (left == 0 || right == 0) {
    return 0;
  }
  if (left == -1) {
    if (right == minimum) {
      throw std::overflow_error("policy multiplication is not representable");
    }
    return -right;
  }
  if (right == -1) {
    if (left == minimum) {
      throw std::overflow_error("policy multiplication is not representable");
    }
    return -left;
  }

  const bool overflows = (left > 0 && right > 0 && left > maximum / right) ||
                         (left > 0 && right < 0 && right < minimum / left) ||
                         (left < 0 && right > 0 && left < minimum / right) ||
                         (left < 0 && right < 0 && left < maximum / right);
  if (overflows) {
    throw std::overflow_error("policy multiplication is not representable");
  }
  return left * right;
}

/**
 * @brief Convert a segment length to the signed representation used by the
 *        integer policies.
 *
 * @param length Nonnegative segment length.
 *
 * @return The representable signed length.
 *
 * @throws std::overflow_error if length is larger than long long can represent.
 */
inline long long checkedLength(std::size_t length) {
  const auto maximum = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
  if (length > maximum) {
    throw std::overflow_error("policy segment length is not representable");
  }
  return static_cast<long long>(length);
}

/**
 * @brief Range-add actions over range-sum aggregates.
 *
 * The abstract policy uses mathematical integers. This C++ witness stores them
 * as long long and throws std::overflow_error if an exact result is not
 * representable. Existing production structures have the same representability
 * precondition on their public operations; this policy makes failure explicit
 * without changing their valid-input behavior.
 *
 * Induced action transformations x -> x + d * len commute, so the
 * tag-retaining persistent tree supports this policy. A logged range-add's
 * contribution to a later range-sum is delta times the overlap length, so
 * the checkpoint baseline's query projection is valid for it.
 */
struct SumAddPolicy {
  /** @brief Aggregate: the segment sum. */
  using Aggregate = long long;

  /** @brief Action: the per-element delta of a range addition. */
  using Action = long long;

  /** @brief Induced action transformations commute; see the file comment. */
  static constexpr bool kInducedActionsCommute = true;

  /** @brief Checkpoint query projection is valid; see the file comment. */
  static constexpr bool kCheckpointQueryProjectable = true;

  /**
   * @brief Ordered sum of two adjacent segment aggregates.
   *
   * @param left  Aggregate of the left segment.
   * @param right Aggregate of the right segment.
   *
   * @return Aggregate of the concatenated segment.
   */
  static Aggregate combine(Aggregate left, Aggregate right) {
    return checkedAdd(left, right);
  }

  /**
   * @brief Aggregate of an empty segment.
   *
   * @return Zero.
   */
  static Aggregate aggregateIdentity() {
    return 0;
  }

  /**
   * @brief Compose two actions, applying older first.
   *
   * @param newer Action applied second.
   * @param older Action applied first.
   *
   * @return The composed action.
   */
  static Action compose(Action newer, Action older) {
    return checkedAdd(newer, older);
  }

  /**
   * @brief The action that changes nothing.
   *
   * @return Zero.
   */
  static Action actionIdentity() {
    return 0;
  }

  /**
   * @brief Effect of a range addition on a segment sum.
   *
   * @param action    Per-element delta.
   * @param aggregate Segment sum before the action.
   * @param length    Number of elements in the segment.
   *
   * @return aggregate + action * length.
   */
  static Aggregate apply(Action action, Aggregate aggregate, std::size_t length) {
    return checkedAdd(aggregate, checkedMultiply(action, checkedLength(length)));
  }
};

/**
 * @brief Range-add actions over range-minimum aggregates.
 *
 * The structural and implementation-generality check: its aggregate ignores
 * the segment length, so code that accidentally depends on summation or on
 * length multiplication fails against it.
 *
 * The abstract policy uses mathematical integers. This C++ witness stores them
 * as long long and rejects an unrepresentable result with std::overflow_error.
 * It deliberately does not use modular wraparound: translation modulo 2^64 is
 * not monotone in signed order and therefore does not distribute over minimum.
 *
 * Induced action transformations x -> x + d commute, so the tag-retaining
 * persistent tree supports this policy. A logged range-add's contribution to
 * a later range-minimum cannot be computed from overlap length alone, so the
 * checkpoint query projection is invalid for it.
 */
struct MinAddPolicy {
  /** @brief Aggregate: the segment minimum. */
  using Aggregate = long long;

  /** @brief Action: the per-element delta of a range addition. */
  using Action = long long;

  /** @brief Induced action transformations commute; see the file comment. */
  static constexpr bool kInducedActionsCommute = true;

  /** @brief Checkpoint query projection is invalid for this policy. */
  static constexpr bool kCheckpointQueryProjectable = false;

  /**
   * @brief Minimum of two adjacent segment aggregates.
   *
   * @param left  Aggregate of the left segment.
   * @param right Aggregate of the right segment.
   *
   * @return The smaller of the two.
   */
  static Aggregate combine(Aggregate left, Aggregate right) {
    return left < right ? left : right;
  }

  /**
   * @brief Aggregate of an empty segment.
   *
   * @return The largest representable value, the identity of minimum.
   */
  static Aggregate aggregateIdentity() {
    return std::numeric_limits<long long>::max();
  }

  /**
   * @brief Compose two actions, applying older first.
   *
   * @param newer Action applied second.
   * @param older Action applied first.
   *
   * @return The composed action.
   */
  static Action compose(Action newer, Action older) {
    return checkedAdd(newer, older);
  }

  /**
   * @brief The action that changes nothing.
   *
   * @return Zero.
   */
  static Action actionIdentity() {
    return 0;
  }

  /**
   * @brief Effect of a range addition on a segment minimum.
   *
   * A length of zero returns the aggregate unchanged, so the empty-segment
   * law holds: the only valid aggregate at length zero is the identity, and
   * an action on an empty segment has nothing to act on.
   *
   * @param action    Per-element delta.
   * @param aggregate Segment minimum before the action.
   * @param length    Number of elements in the segment.
   *
   * @return aggregate + action for a nonempty segment; aggregate otherwise.
   */
  static Aggregate apply(Action action, Aggregate aggregate, std::size_t length) {
    return length == 0 ? aggregate : checkedAdd(aggregate, action);
  }
};

/**
 * @brief Affine actions over range-sum aggregates, modulo an integer.
 *
 * The minimal noncommutative witness: x -> a * x + b composed with
 * x -> c * x + d depends on the order, so its induced transformations do not
 * commute and the tag-retaining persistent tree does not support it. It is
 * the positive control for structures that preserve chronological action
 * order. All arithmetic is modulo the template parameter, so every operation
 * and law is exact for every input.
 *
 * @tparam Modulus The modulus; smaller than 2^32 so a product of two reduced
 *                 values fits in 64 bits.
 */
template <std::uint64_t Modulus> struct AffineSumModPolicy {
  static_assert(Modulus > 1, "AffineSumModPolicy needs a modulus of at least two.");
  static_assert(Modulus < (std::uint64_t{1} << 32),
                "AffineSumModPolicy needs the modulus below 2^32 so a product of two "
                "reduced values fits in 64 bits.");

  /** @brief Aggregate: the segment sum, reduced modulo the modulus. */
  using Aggregate = std::uint64_t;

  /**
   * @brief Action: the affine map x -> scale * x + shift, modulo the modulus.
   */
  struct Action {
    /** @brief Multiplicative coefficient, reduced modulo the modulus. */
    std::uint64_t scale;

    /** @brief Additive coefficient, reduced modulo the modulus. */
    std::uint64_t shift;

    /**
     * @brief Construct a canonical affine action.
     *
     * @param actionScale Multiplicative coefficient.
     * @param actionShift Additive coefficient.
     */
    Action(std::uint64_t actionScale, std::uint64_t actionShift)
        : scale(actionScale % Modulus), shift(actionShift % Modulus) {}

    /**
     * @brief Coefficient-wise equality.
     *
     * @param other Action to compare against.
     *
     * @return Whether both coefficients are equal.
     */
    bool operator==(const Action& other) const {
      return scale == other.scale && shift == other.shift;
    }
  };

  /** @brief Induced action transformations do not commute in general. */
  static constexpr bool kInducedActionsCommute = false;

  /** @brief Checkpoint query projection is invalid for this policy. */
  static constexpr bool kCheckpointQueryProjectable = false;

  /**
   * @brief Sum of two adjacent segment aggregates, modulo the modulus.
   *
   * @param left  Aggregate of the left segment.
   * @param right Aggregate of the right segment.
   *
   * @return Aggregate of the concatenated segment.
   */
  static Aggregate combine(Aggregate left, Aggregate right) {
    return addReduced(left % Modulus, right % Modulus);
  }

  /**
   * @brief Aggregate of an empty segment.
   *
   * @return Zero.
   */
  static Aggregate aggregateIdentity() {
    return 0;
  }

  /**
   * @brief Compose two affine maps, applying older first.
   *
   * @param newer Action applied second.
   * @param older Action applied first.
   *
   * @return The map x -> newer(older(x)).
   */
  static Action compose(Action newer, Action older) {
    return Action{multiplyReduced(newer.scale, older.scale),
                  addReduced(multiplyReduced(newer.scale, older.shift), newer.shift)};
  }

  /**
   * @brief The identity map.
   *
   * @return The action with scale one and shift zero.
   */
  static Action actionIdentity() {
    return Action{1, 0};
  }

  /**
   * @brief Effect of an affine map on a segment sum.
   *
   * Applying x -> scale * x + shift to every element of a segment of the
   * given length turns its sum s into scale * s + shift * length.
   *
   * @param action    The affine map.
   * @param aggregate Segment sum before the action, reduced.
   * @param length    Number of elements in the segment.
   *
   * @return The segment sum after the action, reduced.
   */
  static Aggregate apply(Action action, Aggregate aggregate, std::size_t length) {
    const std::uint64_t reducedLength = length % Modulus;
    return addReduced(multiplyReduced(action.scale, aggregate % Modulus),
                      multiplyReduced(action.shift, reducedLength));
  }

private:
  static Aggregate addReduced(Aggregate left, Aggregate right) {
    return (left + right) % Modulus;
  }

  static Aggregate multiplyReduced(Aggregate left, Aggregate right) {
    return (left * right) % Modulus;
  }
};

} // namespace valseg

#endif
