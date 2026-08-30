#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "structural_counts.hpp"

namespace valseg::bench {
namespace {

Operation update(std::size_t left, std::size_t right, ValueType delta) {
  return {true, left, right, 0, delta};
}

Operation query(std::size_t version, std::size_t left = 0, std::size_t right = 3) {
  return {false, left, right, version, 0};
}

TEST(StructuralCountsTest, LatestQueriesReplayNothingAndCheckpointUpdatesVisitBothTrees) {
  const std::vector<Operation> stream = {
      update(0, 3, 5), query(1),        update(0, 0, 2), query(1),
      query(2),        update(0, 3, 0), query(0),        query(2),
  };

  const StructuralCounts counts = structuralCounts(stream, 4, 2);
  const std::size_t fullVisits = frontierCounts(4, 0, 3).visited();
  const std::size_t pointVisits = frontierCounts(4, 0, 0).visited();

  EXPECT_EQ(counts.updates, 3U);
  EXPECT_EQ(counts.nonZeroUpdates, 2U);
  EXPECT_EQ(counts.updateVisits, fullVisits + pointVisits);
  EXPECT_EQ(counts.checkpointUpdateVisits, pointVisits);
  EXPECT_EQ(counts.queries, 5U);
  EXPECT_EQ(counts.replayEntries, 1U); // historical v1; v2 is a checkpoint
  EXPECT_EQ(counts.latestVersionQueries, 2U);
  EXPECT_EQ(counts.queryVersionDistance, 5U);
  EXPECT_EQ(counts.fullCoverageUpdates, 1U); // zero deltas do no update work
  EXPECT_EQ(counts.fullCoverageQueries, 5U);
}

TEST(StructuralCountsTest, UnboundedCheckpointLogStillSkipsLatestQueries) {
  const std::vector<Operation> stream = {
      update(0, 3, 1), update(1, 2, 2), query(2), query(1), query(0),
  };

  const StructuralCounts counts = structuralCounts(stream, 4, 0);

  EXPECT_EQ(counts.checkpointUpdateVisits, 0U);
  EXPECT_EQ(counts.latestVersionQueries, 1U);
  EXPECT_EQ(counts.replayEntries, 1U);
}

TEST(StructuralCountsTest, FingerprintCoversSeedSizeAndEveryOperationField) {
  const std::vector<Operation> original = {update(0, 3, -4), query(0, 1, 2)};
  std::vector<Operation> changed = original;
  changed.back().version = 1;

  const std::uint64_t fingerprint = streamFingerprint(original, 4, 17);
  EXPECT_EQ(streamFingerprint(original, 4, 17), fingerprint);
  EXPECT_NE(streamFingerprint(original, 5, 17), fingerprint);
  EXPECT_NE(streamFingerprint(original, 4, 18), fingerprint);
  EXPECT_NE(streamFingerprint(changed, 4, 17), fingerprint);
  EXPECT_EQ(fingerprintText(fingerprint).size(), 24U);
}

} // namespace
} // namespace valseg::bench
