#include <valseg/brute_force_array.hpp>
#include <valseg/persistent_lazy_segment_tree.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using valseg::BruteForceArray;
using valseg::PersistentLazySegmentTree;

// Randomized differential validation, covering issue #8.
//
// Every persistent-tree result is compared against the brute-force versioned
// array, which serves as the historical correctness oracle. Oracle updates are
// restricted to the oracle's latest version so that both structures execute
// the same partially persistent operation model.
//
// Campaign layout (all parameters fixed, every run reproducible from its seed):
//   sizes            n in {1, 2, 5, 10, 20, 50}   (one TEST per size)
//   operation counts {100, 1000, 10000}
//   seeds            10 per (size, operation-count) combination
//   update fraction  cycles through {0.8, 0.5, 0.2} across seeds,
//                    mirroring workload profiles W2 / W1 / W3
//
// On a mismatch the failure report contains the seed, the initial values, the
// operation index, the version, the range, the expected and actual results,
// and the replayable operation prefix up to the failing operation.
//
// Every update is additionally checked against the frontier bound of
// docs/proof.md, Proposition 2: a non-zero update may append at most
// 4 * (height + 1) nodes, and a zero-delta update must append none.

namespace {

// Value and delta magnitudes are bounded so that no intermediate value or
// range sum can overflow signed 64-bit arithmetic: with |initial| <= 1e12,
// |delta| <= 1e12, at most 1e4 updates per run, and n <= 50, every element
// stays below 1e12 * (1 + 1e4) and every range sum below 5.1e17, which is
// well within the long long limit of about 9.2e18.
constexpr long long kValueBound = 1'000'000'000'000LL;

constexpr std::size_t kOperationCounts[] = {100, 1000, 10000};
constexpr std::size_t kSeedsPerCombination = 10;
constexpr double kUpdateFractions[] = {0.8, 0.5, 0.2};

// Forcing a zero delta once every kZeroDeltaPeriod updates exercises the
// zero-delta fast path, which shares the latest root instead of copying.
constexpr std::size_t kZeroDeltaPeriod = 97;

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

struct CampaignStats {
  std::size_t runs = 0;
  std::size_t updates = 0;
  std::size_t queries = 0;
  std::size_t sweepChecks = 0;
};

// Smallest height h with 2^h >= n; the tree over n leaves has this height.
std::size_t treeHeight(std::size_t n) {
  std::size_t height = 0;
  while ((static_cast<std::size_t>(1) << height) < n) {
    ++height;
  }
  return height;
}

void runCampaign(std::size_t n, CampaignStats& stats) {
  const std::size_t nodeBound = 4 * (treeHeight(n) + 1);
  for (std::size_t operationCount : kOperationCounts) {
    for (std::size_t seedIndex = 0; seedIndex < kSeedsPerCombination; ++seedIndex) {
      // Transparent seed derivation; the failure report prints the value.
      const std::uint64_t seed = static_cast<std::uint64_t>(n) * 1000003ULL +
                                 static_cast<std::uint64_t>(operationCount) * 101ULL + seedIndex;
      std::mt19937_64 rng(seed);

      std::uniform_int_distribution<long long> valueDist(-kValueBound, kValueBound);
      std::uniform_int_distribution<std::size_t> indexDist(0, n - 1);
      std::uniform_real_distribution<double> mixDist(0.0, 1.0);
      const double updateFraction = kUpdateFractions[seedIndex % 3];

      std::vector<long long> initial(n);
      for (long long& value : initial) {
        value = valueDist(rng);
      }

      BruteForceArray oracle(initial);
      PersistentLazySegmentTree tree(initial);

      std::vector<Operation> prefix;
      prefix.reserve(operationCount);
      std::size_t updatesInRun = 0;

      for (std::size_t opIndex = 0; opIndex < operationCount; ++opIndex) {
        Operation op;
        std::size_t a = indexDist(rng);
        std::size_t b = indexDist(rng);
        op.left = (a < b) ? a : b;
        op.right = (a < b) ? b : a;

        if (mixDist(rng) < updateFraction) {
          op.isUpdate = true;
          op.delta = valueDist(rng);
          if (updatesInRun % kZeroDeltaPeriod == 0) {
            op.delta = 0; // exercise the zero-delta fast path
          }
          ++updatesInRun;
          prefix.push_back(op);

          const std::size_t nodesBefore = tree.nodeCount();
          const std::size_t treeVersion = tree.rangeAdd(op.left, op.right, op.delta);
          const std::size_t oracleVersion =
              oracle.rangeAdd(oracle.versionCount() - 1, op.left, op.right, op.delta);
          ASSERT_EQ(treeVersion, oracleVersion) << "published version identifiers diverged\n"
                                                << describeRun(seed, initial, prefix, opIndex);

          const std::size_t allocated = tree.nodeCount() - nodesBefore;
          if (op.delta == 0) {
            ASSERT_EQ(allocated, 0u) << "zero-delta update allocated nodes\n"
                                     << describeRun(seed, initial, prefix, opIndex);
          } else {
            ASSERT_GE(allocated, 1u) << "non-zero update allocated no nodes\n"
                                     << describeRun(seed, initial, prefix, opIndex);
            ASSERT_LE(allocated, nodeBound)
                << "update exceeded the 4(h+1) = " << nodeBound << " node bound\n"
                << describeRun(seed, initial, prefix, opIndex);
          }
          ++stats.updates;
        } else {
          std::uniform_int_distribution<std::size_t> versionDist(0, tree.versionCount() - 1);
          op.version = versionDist(rng);
          prefix.push_back(op);

          const long long expected = oracle.rangeSum(op.version, op.left, op.right);
          const long long actual = tree.rangeSum(op.version, op.left, op.right);
          ASSERT_EQ(actual, expected) << "historical range sum mismatch at version " << op.version
                                      << ", range [" << op.left << ", " << op.right << "]\n"
                                      << describeRun(seed, initial, prefix, opIndex);
          ++stats.queries;
        }
      }

      // Final sweep: the full-range sum of every published version must agree.
      ASSERT_EQ(tree.versionCount(), oracle.versionCount())
          << "version counts diverged\n"
          << describeRun(seed, initial, prefix, prefix.size() - 1);
      for (std::size_t version = 0; version < tree.versionCount(); ++version) {
        ASSERT_EQ(tree.rangeSum(version, 0, n - 1), oracle.rangeSum(version, 0, n - 1))
            << "full-range sum mismatch at version " << version << "\n"
            << describeRun(seed, initial, prefix, prefix.size() - 1);
        ++stats.sweepChecks;
      }
      ++stats.runs;
    }
  }
}

void runAndReport(std::size_t n) {
  CampaignStats stats;
  runCampaign(n, stats);
  std::cout << "[ STATS  ] n=" << n << " runs=" << stats.runs << " updates=" << stats.updates
            << " queries=" << stats.queries << " sweep_checks=" << stats.sweepChecks << '\n';
}

} // namespace

TEST(DifferentialValidationTest, Size1) {
  runAndReport(1);
}

TEST(DifferentialValidationTest, Size2) {
  runAndReport(2);
}

TEST(DifferentialValidationTest, Size5) {
  runAndReport(5);
}

TEST(DifferentialValidationTest, Size10) {
  runAndReport(10);
}

TEST(DifferentialValidationTest, Size20) {
  runAndReport(20);
}

TEST(DifferentialValidationTest, Size50) {
  runAndReport(50);
}
