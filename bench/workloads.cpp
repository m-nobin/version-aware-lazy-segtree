#include "workloads.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace valseg::bench {

namespace {

constexpr std::size_t kSmall = 1000;
constexpr std::size_t kMedium = 10000;
constexpr std::size_t kLarge = 100000;
constexpr std::size_t kHuge = 1000000;

// Campaign length. Short campaigns measure the build and the cache, not the
// structures: at 2e5 operations even the cheapest structure spends most of the
// run in steady state, and the no-sharing baselines reach their feasibility
// ceiling rather than merely looking expensive.
constexpr std::size_t kOperations = 200000;

/**
 * Uniform draw in [0, bound). Modulo reduction, not
 * std::uniform_int_distribution: the standard library's distributions are
 * implementation-defined, so the same seed would produce different streams on
 * GCC, libc++ and MSVC and the runs would stop being comparable. The residual
 * modulo bias is below 2^-40 for every bound used here.
 */
std::size_t below(std::mt19937_64& rng, std::size_t bound) {
  return static_cast<std::size_t>(rng() % static_cast<std::uint64_t>(bound));
}

/**
 * Uniform double in [0, 1) from the top 53 bits.
 */
double unitDouble(std::mt19937_64& rng) {
  return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}

/**
 * The version a query reads, given how many versions exist.
 */
std::size_t pickVersion(std::mt19937_64& rng, const Workload& workload,
                        const std::vector<double>& harmonic, std::size_t published,
                        std::size_t queryIndex) {
  const std::size_t latest = published - 1;
  switch (workload.versions) {
  case VersionPick::Latest:
    return latest;
  case VersionPick::Uniform:
    return below(rng, published);
  case VersionPick::OldAndRecent: {
    if (queryIndex % 2 == 0) {
      return below(rng, published);
    }
    const std::size_t window = std::max<std::size_t>(1, published / 20);
    return latest - below(rng, window);
  }
  case VersionPick::Zipf: {
    // Rank 1 is the newest version. Inverse transform on the prefix sums of
    // r^-theta, which is exact Zipf(theta) over the versions that exist at
    // this point in the stream, and reduces to uniform at theta = 0.
    const double target = unitDouble(rng) * harmonic[published];
    const std::size_t rank = static_cast<std::size_t>(
        std::upper_bound(harmonic.begin() + 1,
                         harmonic.begin() + static_cast<std::ptrdiff_t>(published) + 1, target) -
        harmonic.begin());
    return latest - std::min(latest, rank - 1);
  }
  }
  return latest;
}

/**
 * Endpoints of one update or query range.
 */
std::pair<std::size_t, std::size_t> pickRange(std::mt19937_64& rng, const Workload& workload,
                                              std::size_t size, std::size_t width,
                                              std::size_t hotStart, std::size_t hotWidth) {
  switch (workload.shape) {
  case RangeShape::Point: {
    const std::size_t index = below(rng, size);
    return {index, index};
  }
  case RangeShape::Full:
    return {0, size - 1};
  case RangeShape::FixedWidth: {
    const std::size_t span = std::min(std::max<std::size_t>(1, width), size);
    const std::size_t start = below(rng, size - span + 1);
    return {start, start + span - 1};
  }
  case RangeShape::Hot: {
    const bool hot = hotWidth < size && unitDouble(rng) < 0.8;
    const std::size_t base = hot ? hotStart : 0;
    const std::size_t span = hot ? hotWidth : size;
    const std::size_t first = base + below(rng, span);
    const std::size_t second = base + below(rng, span);
    return {std::min(first, second), std::max(first, second)};
  }
  case RangeShape::Uniform:
    break;
  }
  const std::size_t first = below(rng, size);
  const std::size_t second = below(rng, size);
  return {std::min(first, second), std::max(first, second)};
}

} // namespace

const std::vector<Workload>& workloads() {
  // Field order, which C++17 cannot make explicit without designated
  // initializers: id, summary, sizes, operations, updateFraction,
  // zeroDeltaFraction, shape, versions, skew, axis, variants, queryTail.
  static const std::vector<Workload> table = {
      {"W1",
       "balanced 50/50, uniform ranges, uniform version reads",
       {kSmall, kMedium, kLarge, kHuge},
       kOperations,
       0.5,
       0.0,
       RangeShape::Uniform,
       VersionPick::Uniform,
       0.0,
       VariantAxis::None,
       {0.0},
       0},
      {"W2",
       "update-heavy 90/10: copy cost per published version",
       {kSmall, kMedium, kLarge, kHuge},
       kOperations,
       0.9,
       0.0,
       RangeShape::Uniform,
       VersionPick::Uniform,
       0.0,
       VariantAxis::None,
       {0.0},
       0},
      {"W3",
       "query-heavy 10/90, half old versions and half the newest 5%",
       {kSmall, kMedium, kLarge, kHuge},
       kOperations,
       0.1,
       0.0,
       RangeShape::Uniform,
       VersionPick::OldAndRecent,
       0.0,
       VariantAxis::None,
       {0.0},
       0},
      {"W4",
       "point updates: the exact per-update bound each header documents",
       {kSmall, kMedium, kLarge, kHuge},
       kOperations,
       0.5,
       0.0,
       RangeShape::Point,
       VersionPick::Uniform,
       0.0,
       VariantAxis::None,
       {0.0},
       0},
      {"W5",
       "full-range updates: full copy's home turf, path copying's worst case",
       {kSmall, kMedium, kLarge, kHuge},
       kOperations,
       0.5,
       0.0,
       RangeShape::Full,
       VersionPick::Uniform,
       0.0,
       VariantAxis::None,
       {0.0},
       0},
      {"W6",
       "zero-delta stress: the shared-root fast path, at 10% and 50%",
       {kMedium},
       kOperations,
       0.9,
       0.1,
       RangeShape::Uniform,
       VersionPick::Uniform,
       0.0,
       VariantAxis::ZeroDeltaShare,
       {0.1, 0.5},
       0},
      {"W7",
       "checkpoint-interval sweep, W1 traffic, K = 1..4096 and unbounded",
       {kMedium},
       kOperations,
       0.5,
       0.0,
       RangeShape::Uniform,
       VersionPick::OldAndRecent,
       0.0,
       VariantAxis::CheckpointInterval,
       {1.0, 16.0, 128.0, 500.0, 4096.0, 0.0},
       0},
      {"W8",
       "version-count sweep: 1e3 to 1e6 versions, then 10000 uniform reads",
       {kMedium},
       0,
       1.0,
       0.0,
       RangeShape::Uniform,
       VersionPick::Uniform,
       0.0,
       VariantAxis::UpdateBudget,
       {1000.0, 10000.0, 100000.0, 1000000.0},
       10000},
      {"W9",
       "Zipfian version reads by recency rank, theta = 0, 0.5 and 0.99",
       {kMedium},
       kOperations,
       0.1,
       0.0,
       RangeShape::Uniform,
       VersionPick::Zipf,
       0.0,
       VariantAxis::Zipf,
       {0.0, 0.5, 0.99},
       0},
      {"W10",
       "update-locality skew: 80% of updates inside a hot window of this width",
       {kMedium},
       kOperations,
       0.9,
       0.0,
       RangeShape::Hot,
       VersionPick::Uniform,
       0.0,
       VariantAxis::HotShare,
       {0.01, 0.05, 0.2, 1.0},
       0},
      {"W11",
       "range-width sweep from a point to the whole array; W4 and W5 are its ends",
       {kMedium},
       kOperations,
       0.5,
       0.0,
       RangeShape::FixedWidth,
       VersionPick::Uniform,
       0.0,
       VariantAxis::RangeWidth,
       {1.0, 8.0, 64.0, 512.0, 4096.0, 10000.0},
       0},
  };
  return table;
}

const Workload* findWorkload(const std::string& id) {
  for (const Workload& workload : workloads()) {
    if (workload.id == id) {
      return &workload;
    }
  }
  return nullptr;
}

const char* axisName(VariantAxis axis) {
  switch (axis) {
  case VariantAxis::None:
    return "none";
  case VariantAxis::CheckpointInterval:
    return "k";
  case VariantAxis::UpdateBudget:
    return "updates";
  case VariantAxis::RangeWidth:
    return "width";
  case VariantAxis::Zipf:
    return "theta";
  case VariantAxis::HotShare:
    return "hot_share";
  case VariantAxis::ZeroDeltaShare:
    return "zero_delta_share";
  }
  return "none";
}

std::vector<ValueType> initialArray(std::size_t size, std::uint64_t seed) {
  std::mt19937_64 rng(seed ^ 0x9e3779b97f4a7c15ULL);
  std::vector<ValueType> values(size);
  for (ValueType& value : values) {
    value = static_cast<ValueType>(below(rng, 2001)) - 1000;
  }
  return values;
}

std::vector<Operation> generate(const Workload& workload, std::size_t size, double variant,
                                std::uint64_t seed) {
  std::mt19937_64 rng(seed);

  const bool budgetAxis = workload.axis == VariantAxis::UpdateBudget;
  const std::size_t updateBudget = budgetAxis ? static_cast<std::size_t>(variant) : 0;
  const std::size_t total = budgetAxis ? updateBudget + workload.queryTail : workload.operations;

  const double zeroDelta =
      workload.axis == VariantAxis::ZeroDeltaShare ? variant : workload.zeroDeltaFraction;
  const double theta = workload.axis == VariantAxis::Zipf ? variant : workload.skew;
  const std::size_t width =
      workload.axis == VariantAxis::RangeWidth ? static_cast<std::size_t>(variant) : size;

  const double hotShare = workload.axis == VariantAxis::HotShare ? variant : 0.2;
  const std::size_t hotWidth = std::min(
      size,
      std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(size) * hotShare)));
  const std::size_t hotStart = below(rng, size - hotWidth + 1);

  // Prefix sums of r^-theta over every version the stream will publish, so a
  // Zipfian draw is one binary search and the distribution is exact for the
  // version count that exists when the draw happens. Only W9 needs it.
  std::vector<double> harmonic;
  if (workload.versions == VersionPick::Zipf) {
    harmonic.assign(total + 2, 0.0);
    for (std::size_t rank = 1; rank + 1 < harmonic.size(); ++rank) {
      harmonic[rank] = harmonic[rank - 1] + std::pow(static_cast<double>(rank), -theta);
    }
    harmonic.back() = harmonic[harmonic.size() - 2];
  }

  std::vector<Operation> stream;
  stream.reserve(total);

  std::size_t published = 1; // version 0 exists after the build
  std::size_t queryIndex = 0;

  for (std::size_t step = 0; step < total; ++step) {
    const bool isUpdate =
        budgetAxis ? step < updateBudget : unitDouble(rng) < workload.updateFraction;

    const std::pair<std::size_t, std::size_t> range =
        pickRange(rng, workload, size, width, hotStart, hotWidth);

    if (isUpdate) {
      ValueType delta = 0;
      if (unitDouble(rng) >= zeroDelta) {
        delta = static_cast<ValueType>(below(rng, 199)) - 99;
        if (delta == 0) {
          delta = 1;
        }
      }
      stream.push_back({true, range.first, range.second, 0, delta});
      ++published;
    } else {
      const std::size_t version = pickVersion(rng, workload, harmonic, published, queryIndex++);
      stream.push_back({false, range.first, range.second, version, 0});
    }
  }

  return stream;
}

} // namespace valseg::bench
