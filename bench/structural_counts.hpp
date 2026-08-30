#ifndef VALSEG_BENCH_STRUCTURAL_COUNTS_HPP
#define VALSEG_BENCH_STRUCTURAL_COUNTS_HPP

#include <valseg/frontier.hpp>
#include <valseg/policy.hpp>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "workloads.hpp"

namespace valseg::bench {

/**
 * Machine-independent structural counts of one operation stream.
 *
 * Counts are properties of the generated stream and the checkpoint interval,
 * not measurements of one benchmark implementation. They are the candidate
 * predictors defined in docs/research/cost-model.md.
 */
struct StructuralCounts {
  std::size_t updates = 0;
  std::size_t nonZeroUpdates = 0;
  std::size_t queries = 0;
  std::size_t updateVisits = 0;
  std::size_t checkpointUpdateVisits = 0;
  std::size_t pushes = 0;
  std::size_t intersecting = 0;
  std::size_t queryVisits = 0;
  std::size_t replayEntries = 0;
  std::uint64_t queryVersionDistance = 0;
  std::size_t fullCoverageUpdates = 0;
  std::size_t fullCoverageQueries = 0;
  std::size_t latestVersionQueries = 0;
};

/**
 * Count the structural work represented by one stream.
 *
 * interval == 0 denotes the unbounded checkpoint interval: historical reads
 * replay from version zero. Latest-version reads always use the live tree and
 * replay no entries, matching CheckpointingSegmentTree::rangeSum.
 */
inline StructuralCounts structuralCounts(const std::vector<Operation>& stream, std::size_t size,
                                         std::size_t interval) {
  StructuralCounts counts;
  PushCountingModel<SumAddPolicy> pushes(size);
  std::size_t latestVersion = 0;
  for (const Operation& op : stream) {
    if (op.isUpdate) {
      ++counts.updates;
      ++latestVersion;
      if (op.delta == 0) {
        continue;
      }
      ++counts.nonZeroUpdates;
      const FrontierCounts frontier = frontierCounts(size, op.left, op.right);
      counts.updateVisits += frontier.visited();
      if (interval != 0 && latestVersion % interval == 0) {
        // CheckpointingSegmentTree copies the pre-update live tree and then
        // applies the update to both the live tree and the checkpoint copy.
        counts.checkpointUpdateVisits += frontier.visited();
      }
      counts.pushes += pushes.apply(op.left, op.right, op.delta);
      counts.intersecting +=
          frontier.partial + 2 * (op.right - op.left + 1) - frontier.decomposition;
      if (op.left == 0 && op.right == size - 1) {
        ++counts.fullCoverageUpdates;
      }
    } else {
      ++counts.queries;
      counts.queryVisits += frontierCounts(size, op.left, op.right).visited();
      if (op.left == 0 && op.right == size - 1) {
        ++counts.fullCoverageQueries;
      }
      counts.queryVersionDistance += static_cast<std::uint64_t>(latestVersion - op.version);
      if (op.version == latestVersion) {
        ++counts.latestVersionQueries;
        continue;
      }
      counts.replayEntries += interval == 0 ? op.version : op.version % interval;
    }
  }
  return counts;
}

namespace detail {

inline void hashWord(std::uint64_t& hash, std::uint64_t word) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (unsigned int byte = 0; byte < 8; ++byte) {
    hash ^= (word >> (byte * 8U)) & 0xffU;
    hash *= kPrime;
  }
}

} // namespace detail

/**
 * Stable fingerprint of the complete generated input for one trial.
 *
 * The initial array is deterministic in (size, seed), so those two fields plus
 * every operation identify the replayed input without constructing and hashing
 * an additional million-element array in structural mode.
 */
inline std::uint64_t streamFingerprint(const std::vector<Operation>& stream, std::size_t size,
                                       std::uint64_t seed) {
  std::uint64_t hash = 14695981039346656037ULL;
  detail::hashWord(hash, static_cast<std::uint64_t>(size));
  detail::hashWord(hash, seed);
  detail::hashWord(hash, static_cast<std::uint64_t>(stream.size()));
  for (const Operation& op : stream) {
    detail::hashWord(hash, op.isUpdate ? 1U : 0U);
    detail::hashWord(hash, static_cast<std::uint64_t>(op.left));
    detail::hashWord(hash, static_cast<std::uint64_t>(op.right));
    detail::hashWord(hash, static_cast<std::uint64_t>(op.version));
    detail::hashWord(hash, static_cast<std::uint64_t>(op.delta));
  }
  return hash;
}

inline std::string fingerprintText(std::uint64_t fingerprint) {
  std::ostringstream out;
  out << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << fingerprint;
  return out.str();
}

} // namespace valseg::bench

#endif
