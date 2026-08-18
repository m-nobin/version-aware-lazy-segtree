#include <valseg/brute_force_array.hpp>
#include <valseg/buffered_path_copying_segment_tree.hpp>
#include <valseg/checkpointing_segment_tree.hpp>
#include <valseg/fat_node_persistent_segment_tree.hpp>
#include <valseg/full_copy_persistent_segment_tree.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>
#include <valseg/point_only_persistent_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using valseg::BruteForceArray;

// Randomized differential validation of the benchmark baselines.
//
// Every baseline shares the PersistentLazySegmentTree interface, so one typed
// test replays seeded campaigns of mixed latest-version updates and historical
// queries against BruteForceArray, the historical correctness oracle; the
// proposed structure itself runs as a control. A new baseline joins by adding
// its type to BaselineTypes and its name to BaselineNames below.
//
// Campaign layout (fixed, every run reproducible from its seed for a given
// standard library):
//   sizes            n in {1, 2, 5, 16, 64}
//   seeds            3 per size
//   operations       2000 per run, about half updates and half queries
//
// Structural bounds (copied-node counts) are asserted in each baseline's own
// deterministic suite; this test checks results only. On a mismatch the
// failure report contains the seed, the initial values, and the replayable
// operation prefix up to the failing operation.

namespace {

// |initial| and |delta| <= 1e12 with at most 2000 updates and n <= 64 keeps
// every element below 2.1e15 and every range sum below 1.3e17, well inside
// the signed 64-bit range.
constexpr long long kValueBound = 1'000'000'000'000LL;
constexpr std::size_t kSizes[] = {1, 2, 5, 16, 64};
constexpr std::size_t kSeedsPerSize = 3;
constexpr std::size_t kOperationsPerRun = 2000;
constexpr std::size_t kZeroDeltaPeriod = 53; // exercises the zero-delta fast path

struct Operation {
  bool isUpdate = false;
  std::size_t version = 0; // query only
  std::size_t left = 0;
  std::size_t right = 0;
  long long delta = 0; // update only
};

std::string describeRun(std::uint64_t seed, const std::vector<long long>& initial,
                        const std::vector<Operation>& prefix, std::size_t failingIndex) {
  std::ostringstream out;
  out << "seed=" << seed << "\ninitial=[";
  for (std::size_t i = 0; i < initial.size(); ++i) {
    out << (i == 0 ? "" : ", ") << initial[i];
  }
  out << "]\nfailing operation index=" << failingIndex << "\nreplayable prefix:\n";
  for (std::size_t i = 0; i <= failingIndex && i < prefix.size(); ++i) {
    const Operation& op = prefix[i];
    if (op.isUpdate) {
      out << "  " << i << ": rangeAdd(" << op.left << ", " << op.right << ", " << op.delta << ")\n";
    } else {
      out << "  " << i << ": rangeSum(v" << op.version << ", " << op.left << ", " << op.right
          << ")\n";
    }
  }
  return out.str();
}

template <typename Baseline> void runCampaign(std::size_t n, std::size_t seedIndex) {
  const std::uint64_t seed = static_cast<std::uint64_t>(n) * 1000003ULL + seedIndex;
  std::mt19937_64 rng(seed);

  std::uniform_int_distribution<long long> valueDist(-kValueBound, kValueBound);
  std::uniform_int_distribution<std::size_t> indexDist(0, n - 1);
  std::bernoulli_distribution isUpdateDist(0.5);

  std::vector<long long> initial(n);
  for (long long& value : initial) {
    value = valueDist(rng);
  }

  BruteForceArray oracle(initial);
  Baseline baseline(initial);

  std::vector<Operation> prefix;
  prefix.reserve(kOperationsPerRun);
  std::size_t updatesInRun = 0;

  for (std::size_t opIndex = 0; opIndex < kOperationsPerRun; ++opIndex) {
    Operation op;
    const std::size_t a = indexDist(rng);
    const std::size_t b = indexDist(rng);
    op.left = (a < b) ? a : b;
    op.right = (a < b) ? b : a;

    if (isUpdateDist(rng)) {
      op.isUpdate = true;
      op.delta = (updatesInRun % kZeroDeltaPeriod == 0) ? 0 : valueDist(rng);
      ++updatesInRun;
      prefix.push_back(op);

      const std::size_t baselineVersion = baseline.rangeAdd(op.left, op.right, op.delta);
      const std::size_t oracleVersion =
          oracle.rangeAdd(oracle.versionCount() - 1, op.left, op.right, op.delta);
      ASSERT_EQ(baselineVersion, oracleVersion) << "published version identifiers diverged\n"
                                                << describeRun(seed, initial, prefix, opIndex);
    } else {
      std::uniform_int_distribution<std::size_t> versionDist(0, baseline.versionCount() - 1);
      op.version = versionDist(rng);
      prefix.push_back(op);

      ASSERT_EQ(baseline.rangeSum(op.version, op.left, op.right),
                oracle.rangeSum(op.version, op.left, op.right))
          << "historical range sum mismatch at version " << op.version << ", range [" << op.left
          << ", " << op.right << "]\n"
          << describeRun(seed, initial, prefix, opIndex);
    }
  }

  // Final sweep: the full-range sum of every published version must agree.
  ASSERT_EQ(baseline.versionCount(), oracle.versionCount())
      << "version counts diverged\n"
      << describeRun(seed, initial, prefix, prefix.size() - 1);
  for (std::size_t version = 0; version < baseline.versionCount(); ++version) {
    ASSERT_EQ(baseline.rangeSum(version, 0, n - 1), oracle.rangeSum(version, 0, n - 1))
        << "full-range sum mismatch at version " << version << "\n"
        << describeRun(seed, initial, prefix, prefix.size() - 1);
  }
}

// The checkpointing baseline with a small interval, so each campaign replays
// across hundreds of checkpoint boundaries instead of the default 500's two.
struct CheckpointingIntervalThree : valseg::CheckpointingSegmentTree {
  explicit CheckpointingIntervalThree(const std::vector<long long>& values)
      : CheckpointingSegmentTree(values, 3) {}
};

// Names the typed-test instantiations after the baseline under test.
struct BaselineNames {
  template <typename T> static std::string GetName(int) {
    if constexpr (std::is_same_v<T, valseg::FullCopyPersistentSegmentTree>) {
      return "FullCopy";
    } else if constexpr (std::is_same_v<T, valseg::PointOnlyPersistentSegmentTree>) {
      return "PointOnly";
    } else if constexpr (std::is_same_v<T, valseg::CheckpointingSegmentTree>) {
      return "Checkpointing";
    } else if constexpr (std::is_same_v<T, CheckpointingIntervalThree>) {
      return "CheckpointingIntervalThree";
    } else if constexpr (std::is_same_v<T, valseg::BufferedPathCopyingSegmentTree>) {
      return "BufferedPathCopying";
    } else if constexpr (std::is_same_v<T, valseg::FatNodePersistentSegmentTree>) {
      return "FatNode";
    } else if constexpr (std::is_same_v<T, valseg::PersistentLazySegmentTree>) {
      return "PersistentLazy";
    } else {
      static_assert(sizeof(T) == 0, "name the new baseline in BaselineNames");
    }
  }
};

} // namespace

template <typename Baseline> class BaselineDifferentialTest : public ::testing::Test {};

using BaselineTypes =
    ::testing::Types<valseg::FullCopyPersistentSegmentTree, valseg::PointOnlyPersistentSegmentTree,
                     valseg::CheckpointingSegmentTree, CheckpointingIntervalThree,
                     valseg::BufferedPathCopyingSegmentTree, valseg::FatNodePersistentSegmentTree,
                     valseg::PersistentLazySegmentTree>;
TYPED_TEST_SUITE(BaselineDifferentialTest, BaselineTypes, BaselineNames);

TYPED_TEST(BaselineDifferentialTest, MatchesBruteForceOracle) {
  for (std::size_t n : kSizes) {
    for (std::size_t seedIndex = 0; seedIndex < kSeedsPerSize; ++seedIndex) {
      runCampaign<TypeParam>(n, seedIndex);
      if (this->HasFatalFailure()) {
        return;
      }
    }
  }
}
