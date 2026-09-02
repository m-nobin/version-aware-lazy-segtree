#include <valseg/persistent_lazy_segment_tree.hpp>

int main() {
  valseg::PersistentLazySegmentTree tree({1, 2, 3, 4});
  tree.rangeAdd(1, 2, 10);
  const bool ok = tree.rangeSum(0, 0, 3) == 10 && tree.rangeSum(1, 0, 3) == 30;
  return ok ? 0 : 1;
}
