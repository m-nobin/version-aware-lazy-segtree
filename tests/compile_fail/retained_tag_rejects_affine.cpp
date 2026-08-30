// Must not compile. The tag-retaining subject rejects a policy whose induced
// action transformations do not commute; tests/CMakeLists.txt registers a
// CTest case that builds this file and passes only when the compiler reports
// the static_assert naming Policy::kInducedActionsCommute.

#include <valseg/policy.hpp>
#include <valseg/policy_trees.hpp>

int main() {
  valseg::RetainedTagPersistentTree<valseg::AffineSumModPolicy<13>> tree({1, 2, 3});
  return static_cast<int>(tree.versionCount());
}
