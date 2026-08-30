#ifndef VALSEG_BENCH_WORKLOADS_HPP
#define VALSEG_BENCH_WORKLOADS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace valseg::bench {

/**
 * Element and sum type; identical to every structure's ValueType.
 */
using ValueType = long long;

/**
 * How an update or query range is drawn.
 */
enum class RangeShape {
  Uniform,    ///< two independent uniform endpoints, sorted
  Point,      ///< a single index
  Full,       ///< the whole array
  FixedWidth, ///< a window of the width carried by the variant axis
  Hot         ///< 80% of ranges inside a 20% window, 20% anywhere
};

/**
 * Which published version a query reads.
 */
enum class VersionPick {
  Latest,       ///< the newest version
  Uniform,      ///< uniform over every published version
  OldAndRecent, ///< alternating: uniform over all, then the newest 5%
  Zipf,         ///< Zipfian over recency rank; see Workload::skew
  Ancient       ///< uniform over the oldest share of versions; see AgeShare
};

/**
 * The axis a workload sweeps. Every workload sweeps at most one, so a cell of
 * the run matrix is (workload, n, variant value, seed, trial).
 */
enum class VariantAxis {
  None,               ///< one variant value, reported as 0
  CheckpointInterval, ///< K for CheckpointingSegmentTree
  UpdateBudget,       ///< number of updates, i.e. published versions
  RangeWidth,         ///< update range width in elements
  Zipf,               ///< Zipf exponent theta for VersionPick::Zipf
  HotShare,           ///< width of the hot window as a fraction of n
  ZeroDeltaShare,     ///< share of updates whose delta is 0
  AgeShare            ///< share of the version history a query may read from
};

/**
 * One frozen workload.
 *
 * The twelve instances live in workloads() and mirror the specification
 * frozen on issue #10. Nothing here is tuned per structure: a workload
 * produces one operation stream that every structure replays verbatim.
 */
struct Workload {
  std::string id;      ///< "W1" ... "W12"
  std::string summary; ///< one line, printed by --list
  std::vector<std::size_t> sizes;
  std::size_t operations;   ///< updates plus queries; 0 when axis is UpdateBudget
  double updateFraction;    ///< share of operations that are updates
  double zeroDeltaFraction; ///< share of updates whose delta is 0
  RangeShape shape;
  VersionPick versions;
  double skew; ///< exponent for VersionPick::Skewed; see generate()
  VariantAxis axis;
  std::vector<double> variants;
  std::size_t queryTail; ///< used only when axis is UpdateBudget: the stream
                         ///< is `variant` updates followed by this many
                         ///< queries, so query cost is read against a known
                         ///< version count
};

/**
 * One generated operation. Version is meaningful for queries only and always
 * names a version that exists by the time the operation runs.
 */
struct Operation {
  bool isUpdate;
  std::size_t left;
  std::size_t right;
  std::size_t version;
  ValueType delta;
};

/**
 * @brief The twelve frozen workloads, in order.
 *
 * @return W1 through W12.
 */
const std::vector<Workload>& workloads();

/**
 * @brief Look up a workload by id.
 *
 * @param id Workload id, e.g. "W7".
 * @return Pointer to the workload, or nullptr if no workload has that id.
 */
const Workload* findWorkload(const std::string& id);

/**
 * @brief Name of a variant axis as it appears in the CSV.
 *
 * @param axis Axis to name.
 * @return "none", "k", "updates", "width", "theta", "hot_share",
 *         "zero_delta_share" or "age_share".
 */
const char* axisName(VariantAxis axis);

/**
 * @brief Build the initial array for a trial.
 *
 * @param size Number of elements.
 * @param seed Trial seed.
 * @return Array of `size` pseudo-random values.
 */
std::vector<ValueType> initialArray(std::size_t size, std::uint64_t seed);

/**
 * @brief Generate the operation stream for one cell of the run matrix.
 *
 * Deterministic in (workload, size, variant, seed) and independent of the
 * structure that will replay it: every structure publishes exactly one
 * version per update, zero-delta updates included, so a query's target
 * version is known at generation time.
 *
 * Drawing uses std::mt19937_64 and modulo reduction rather than
 * std::uniform_int_distribution, whose output is implementation-defined and
 * would make a seed mean different things on different platforms.
 *
 * @param workload Workload to generate.
 * @param size     Array size n.
 * @param variant  Value from the workload's variant axis.
 * @param seed     Trial seed.
 * @return Operation stream of workload.operations entries.
 */
std::vector<Operation> generate(const Workload& workload, std::size_t size, double variant,
                                std::uint64_t seed);

} // namespace valseg::bench

#endif
