#ifndef VALSEG_TESTS_POLICY_ORACLE_HPP
#define VALSEG_TESTS_POLICY_ORACLE_HPP

#include <cstddef>
#include <utility>
#include <vector>

namespace valseg::testing {

/**
 * Generic element-wise oracle for any aggregate/action policy.
 *
 * Keeps every version as a plain array of per-element aggregates, applies
 * each action element by element in chronological order, and answers a range
 * aggregate by folding combine left to right. It is correct for any valid
 * policy by construction, because it never reorders, defers or merges
 * actions, so it is the reference every policy-generic structure must agree
 * with. It is test-only: updates cost O(n) time and O(n) space per version.
 */
template <class Policy> class ElementWiseOracle {
public:
  using Aggregate = typename Policy::Aggregate;
  using Action = typename Policy::Action;

  explicit ElementWiseOracle(std::vector<Aggregate> initial) {
    versions.push_back(std::move(initial));
  }

  /// Apply action to [left, right] of the latest version; returns the new
  /// version number.
  std::size_t rangeApply(std::size_t left, std::size_t right, Action action) {
    std::vector<Aggregate> next = versions.back();
    for (std::size_t index = left; index <= right; ++index) {
      next[index] = Policy::apply(action, next[index], 1);
    }
    versions.push_back(std::move(next));
    return versions.size() - 1;
  }

  /// Aggregate over [left, right] in the given version, folded in index
  /// order.
  Aggregate rangeAggregate(std::size_t version, std::size_t left, std::size_t right) const {
    Aggregate result = Policy::aggregateIdentity();
    for (std::size_t index = left; index <= right; ++index) {
      result = Policy::combine(result, versions[version][index]);
    }
    return result;
  }

  std::size_t versionCount() const {
    return versions.size();
  }

private:
  std::vector<std::vector<Aggregate>> versions;
};

} // namespace valseg::testing

#endif
