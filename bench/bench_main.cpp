#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

#include "adapters.hpp"
#include "allocation_counter.hpp"
#include "workloads.hpp"

namespace valseg::bench {
namespace {

#ifndef VALSEG_BENCH_BUILD_TYPE
#define VALSEG_BENCH_BUILD_TYPE "unknown"
#endif
#ifndef VALSEG_BENCH_COMPILER
#define VALSEG_BENCH_COMPILER "unknown"
#endif
#ifndef VALSEG_BENCH_FLAGS
#define VALSEG_BENCH_FLAGS "unknown"
#endif

using Clock = std::chrono::steady_clock;

std::int64_t nanosSince(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

/**
 * What one clock pair costs and what it can resolve.
 *
 * Every timed operation here is bracketed by two clock reads, so whatever a
 * back-to-back pair reports is added to every measurement. On a machine whose
 * timer ticks at tens of nanoseconds that is a large fraction of the cheapest
 * operation in the campaign, and a benchmark that does not report it is
 * quietly publishing the timer.
 */
struct ClockCalibration {
  std::int64_t overheadNs = 0;   ///< median interval a pair reports over no work
  std::int64_t resolutionNs = 0; ///< smallest non-zero interval seen
};

ClockCalibration calibrateClock() {
  constexpr std::size_t kSamples = 20000;
  std::vector<std::int64_t> pairs;
  pairs.reserve(kSamples);
  for (std::size_t i = 0; i < kSamples; ++i) {
    const Clock::time_point start = Clock::now();
    pairs.push_back(nanosSince(start));
  }
  std::vector<std::int64_t> sorted = pairs;
  std::sort(sorted.begin(), sorted.end());

  ClockCalibration calibration;
  calibration.overheadNs = sorted[sorted.size() / 2];
  for (const std::int64_t value : sorted) {
    if (value > 0) {
      calibration.resolutionNs = value;
      break;
    }
  }
  return calibration;
}

/**
 * Raise this thread to the interactive quality-of-service class.
 *
 * Apple silicon has performance and efficiency cores with different clocks and
 * different caches, and the scheduler is free to move a background thread onto
 * an efficiency core part-way through a trial. That turns a structure
 * comparison into a core-placement comparison. Asking for the interactive
 * class is the portable request that keeps the run on performance cores; it is
 * a request, not a guarantee, so the environment file records that it was made.
 */
bool requestPerformanceCores() {
#if defined(__APPLE__)
  return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0;
#else
  return false;
#endif
}

/**
 * One memory reading taken part-way through a trial, so allocated-node growth
 * is a curve rather than a final number.
 */
struct Sample {
  std::size_t opIndex;
  std::size_t versions;
  std::size_t nodes;
  std::size_t bytes;
};

/**
 * Quantiles of a per-operation latency sample, in nanoseconds.
 */
struct Latency {
  std::int64_t p50 = 0;
  std::int64_t p90 = 0;
  std::int64_t p99 = 0;
  std::int64_t p999 = 0;
  std::int64_t max = 0;
};

Latency quantiles(std::vector<std::uint32_t>& values) {
  Latency latency;
  if (values.empty()) {
    return latency;
  }
  std::sort(values.begin(), values.end());
  const auto at = [&values](double fraction) {
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t index = static_cast<std::size_t>(position);
    return static_cast<std::int64_t>(values[index]);
  };
  latency.p50 = at(0.50);
  latency.p90 = at(0.90);
  latency.p99 = at(0.99);
  latency.p999 = at(0.999);
  latency.max = static_cast<std::int64_t>(values.back());
  return latency;
}

/**
 * Everything one replay of one operation stream produced.
 */
struct TrialResult {
  std::size_t updates = 0;
  std::size_t queries = 0;
  std::int64_t buildNs = 0;
  std::size_t buildNodes = 0;
  std::int64_t updateNs = 0;
  std::int64_t queryNs = 0;
  std::int64_t batchNs = -1;
  Latency updateLatency;
  Latency queryLatency;
  std::size_t nodes = 0;
  std::size_t bytes = 0;
  std::size_t allocPeak = 0;
  std::size_t allocLive = 0;
  std::size_t allocCount = 0;
  std::uint64_t checksum = 0;
  const char* status = "ok";
  std::vector<Sample> samples;
};

/**
 * Replay one operation stream against one structure, timing every operation.
 *
 * Update and query time are accumulated separately and every operation's
 * latency is kept, so the campaign reports a distribution rather than a single
 * average. The per-operation clock pair is the price of that; its cost is
 * calibrated once per process and reported next to every number it touched.
 *
 * A trial stops early and reports status "memory_cap" when retained bytes pass
 * the cap. That is a result, not a failure: it is where a structure becomes
 * unrunnable at this size, and silently OOM-killing the machine would hide it.
 */
template <typename Adapter>
TrialResult runTrial(const std::vector<ValueType>& initial, const std::vector<Operation>& stream,
                     std::size_t checkpointInterval, std::size_t capBytes) {
  TrialResult result;
  Adapter adapter(checkpointInterval);
  resetAllocationStats();

  const Clock::time_point buildStart = Clock::now();
  adapter.build(initial);
  result.buildNs = nanosSince(buildStart);
  result.buildNodes = adapter.nodes();

  std::vector<std::uint32_t> updateSamples;
  std::vector<std::uint32_t> querySamples;
  updateSamples.reserve(stream.size());
  querySamples.reserve(stream.size());

  const std::size_t sampleEvery = std::max<std::size_t>(1, stream.size() / 20);

  for (std::size_t index = 0; index < stream.size(); ++index) {
    const Operation& op = stream[index];
    if (op.isUpdate) {
      const Clock::time_point start = Clock::now();
      adapter.update(op.left, op.right, op.delta);
      const std::int64_t elapsed = nanosSince(start);
      result.updateNs += elapsed;
      updateSamples.push_back(static_cast<std::uint32_t>(
          std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max())));
      ++result.updates;
    } else {
      const Clock::time_point start = Clock::now();
      const ValueType answer = adapter.query(op.version, op.left, op.right);
      const std::int64_t elapsed = nanosSince(start);
      result.queryNs += elapsed;
      querySamples.push_back(static_cast<std::uint32_t>(
          std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max())));
      ++result.queries;
      result.checksum = result.checksum * 1000003ULL + static_cast<std::uint64_t>(answer);
    }

    // The cap is checked every operation, not every sample: full copying at
    // n = 1e5 adds megabytes per update, and a coarse check would overshoot by
    // gigabytes before noticing. nodes() and bytes() are O(1) on every
    // structure, and both live outside the timed region.
    const std::size_t bytes = adapter.bytes();
    if ((index + 1) % sampleEvery == 0 || index + 1 == stream.size() || bytes > capBytes) {
      result.samples.push_back({index + 1, adapter.versions(), adapter.nodes(), bytes});
    }
    if (bytes > capBytes) {
      result.status = "memory_cap";
      break;
    }
  }

  result.updateLatency = quantiles(updateSamples);
  result.queryLatency = quantiles(querySamples);
  result.nodes = adapter.nodes();
  result.bytes = adapter.bytes();
  const AllocationStats allocation = allocationStats();
  result.allocPeak = allocation.peakBytes;
  result.allocLive = allocation.liveBytes;
  result.allocCount = allocation.allocations;
  return result;
}

/**
 * Replay the same stream with two clock reads in total.
 *
 * This exists to price the instrumentation the timed replay carries. The
 * difference between this total and the timed replay's update plus query total
 * is the cost of the per-operation clock pairs and of keeping the latency
 * samples, measured rather than assumed.
 *
 * It stops at the same cap, so a capped cell compares like with like.
 */
template <typename Adapter>
std::int64_t runBatched(const std::vector<ValueType>& initial,
                        const std::vector<Operation>& stream, std::size_t checkpointInterval,
                        std::size_t capBytes) {
  Adapter adapter(checkpointInterval);
  adapter.build(initial);

  volatile ValueType sink = 0;
  const Clock::time_point start = Clock::now();
  for (const Operation& op : stream) {
    if (op.isUpdate) {
      adapter.update(op.left, op.right, op.delta);
    } else {
      sink = adapter.query(op.version, op.left, op.right);
    }
    if (adapter.bytes() > capBytes) {
      break;
    }
  }
  const std::int64_t elapsed = nanosSince(start);
  (void)sink;
  return elapsed;
}

using RunFunction = TrialResult (*)(const std::vector<ValueType>&, const std::vector<Operation>&,
                                    std::size_t, std::size_t);
using BatchFunction = std::int64_t (*)(const std::vector<ValueType>&,
                                       const std::vector<Operation>&, std::size_t, std::size_t);

/**
 * One structure under measurement.
 */
struct Structure {
  const char* name;
  RunFunction run;
  BatchFunction batch;
  bool historical;
};

const std::vector<Structure>& structures() {
  static const std::vector<Structure> table = {
      {"lazy", &runTrial<LazyAdapter>, &runBatched<LazyAdapter>, false},
      {"persistent", &runTrial<PersistentAdapter<PersistentLazySegmentTree>>,
       &runBatched<PersistentAdapter<PersistentLazySegmentTree>>, true},
      {"copy-on-push", &runTrial<PersistentAdapter<CopyOnPushSegmentTree>>,
       &runBatched<PersistentAdapter<CopyOnPushSegmentTree>>, true},
      {"full-copy", &runTrial<PersistentAdapter<FullCopyPersistentSegmentTree>>,
       &runBatched<PersistentAdapter<FullCopyPersistentSegmentTree>>, true},
      {"point-only", &runTrial<PersistentAdapter<PointOnlyPersistentSegmentTree>>,
       &runBatched<PersistentAdapter<PointOnlyPersistentSegmentTree>>, true},
      {"checkpointing", &runTrial<PersistentAdapter<CheckpointingSegmentTree>>,
       &runBatched<PersistentAdapter<CheckpointingSegmentTree>>, true},
      {"buffered", &runTrial<PersistentAdapter<BufferedPathCopyingSegmentTree>>,
       &runBatched<PersistentAdapter<BufferedPathCopyingSegmentTree>>, true},
      {"fat-node", &runTrial<PersistentAdapter<FatNodePersistentSegmentTree>>,
       &runBatched<PersistentAdapter<FatNodePersistentSegmentTree>>, true},
  };
  return table;
}

/**
 * Command-line options.
 */
struct Options {
  std::string workloadFilter = "all";
  std::string structureFilter = "all";
  std::string outDir = "bench/results/raw";
  std::string tag;
  std::uint64_t seed = 20260818;
  std::size_t trials = 5;
  std::size_t capMiB = 4096;
  std::size_t warmup = 1;
  std::size_t cappedTrials = 2;
  std::size_t batchTrials = 2;
  std::size_t warmupSeconds = 20;
  std::string trace;
  bool smoke = false;
  bool list = false;
  bool outDirGiven = false;
  bool shuffle = true;
};

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "valseg_bench: " << message << "\n";
  std::exit(2);
}

Options parse(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const bool hasValue = i + 1 < argc;
    const std::string value = hasValue ? argv[i + 1] : std::string();
    if (flag == "--list") {
      options.list = true;
    } else if (flag == "--smoke") {
      options.smoke = true;
    } else if (flag == "--warmup-seconds" && hasValue) {
      options.warmupSeconds = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--batch-trials" && hasValue) {
      options.batchTrials = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--no-shuffle") {
      options.shuffle = false;
    } else if (flag == "--workload" && hasValue) {
      options.workloadFilter = value;
      ++i;
    } else if (flag == "--structure" && hasValue) {
      options.structureFilter = value;
      ++i;
    } else if (flag == "--out-dir" && hasValue) {
      options.outDir = value;
      options.outDirGiven = true;
      ++i;
    } else if (flag == "--tag" && hasValue) {
      options.tag = value;
      ++i;
    } else if (flag == "--seed" && hasValue) {
      options.seed = std::strtoull(value.c_str(), nullptr, 10);
      ++i;
    } else if (flag == "--trials" && hasValue) {
      options.trials = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--warmup" && hasValue) {
      options.warmup = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--capped-trials" && hasValue) {
      options.cappedTrials = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--trace" && hasValue) {
      options.trace = value;
      ++i;
    } else if (flag == "--cap-mib" && hasValue) {
      options.capMiB = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--help") {
      std::cout << "usage: valseg_bench [--workload W1|all] [--structure NAME|all]\n"
                   "                    [--out-dir DIR] [--tag SUFFIX] [--seed N]\n"
                   "                    [--trials N] [--warmup N] [--warmup-seconds N]\n"
                   "                    [--capped-trials N]\n"
                   "                    [--cap-mib N] [--trace FILE] [--batch-trials N]\n"
                   "                    [--no-shuffle] [--smoke] [--list]\n";
      std::exit(0);
    } else {
      fail("unknown or incomplete option: " + flag);
    }
  }
  if (options.trials == 0) {
    fail("--trials must be at least 1");
  }
  if (options.cappedTrials == 0) {
    fail("--capped-trials must be at least 1");
  }
  if (options.smoke && !options.outDirGiven) {
    options.outDir = ".";
  }
  if (options.workloadFilter != "all" && findWorkload(options.workloadFilter) == nullptr) {
    fail("no such workload: " + options.workloadFilter);
  }
  if (options.structureFilter != "all") {
    bool known = false;
    for (const Structure& structure : structures()) {
      known = known || options.structureFilter == structure.name;
    }
    if (!known) {
      fail("no such structure: " + options.structureFilter);
    }
  }
  return options;
}

/**
 * Load an operation trace: one line `n,<size>` followed by one line per
 * operation, `u,left,right,delta` or `q,left,right,version`.
 *
 * This is the seam for a workload taken from published data rather than
 * generated here. Synthetic streams are the only ones this repository can
 * produce on its own, and a comparison built exclusively on them is worth
 * less than one that also replays traffic somebody else published.
 */
std::vector<Operation> loadTrace(const std::string& path, std::size_t& size) {
  std::ifstream in(path);
  if (!in) {
    fail("cannot read trace " + path);
  }

  std::vector<Operation> stream;
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<std::string> field;
    std::size_t start = 0;
    while (start <= line.size()) {
      const std::size_t comma = line.find(',', start);
      field.push_back(line.substr(start, comma - start));
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }

    if (field[0] == "n" && field.size() == 2) {
      size = static_cast<std::size_t>(std::strtoull(field[1].c_str(), nullptr, 10));
      continue;
    }
    if (field.size() != 4 || (field[0] != "u" && field[0] != "q")) {
      fail(path + ":" + std::to_string(lineNumber) + ": expected n,<size> or u|q,l,r,arg");
    }
    const std::size_t left = static_cast<std::size_t>(std::strtoull(field[1].c_str(), nullptr, 10));
    const std::size_t right =
        static_cast<std::size_t>(std::strtoull(field[2].c_str(), nullptr, 10));
    const long long arg = std::strtoll(field[3].c_str(), nullptr, 10);
    if (field[0] == "u") {
      stream.push_back({true, left, right, 0, static_cast<ValueType>(arg)});
    } else {
      stream.push_back({false, left, right, static_cast<std::size_t>(arg), 0});
    }
  }

  if (size == 0 || stream.empty()) {
    fail(path + ": trace must declare a non-zero n and contain at least one operation");
  }
  return stream;
}

/**
 * The workload record a trace is reported under.
 */
Workload traceWorkload(std::size_t size) {
  Workload workload;
  workload.id = "WT";
  workload.summary = "replayed trace";
  workload.sizes = {size};
  workload.operations = 0;
  workload.updateFraction = 0.0;
  workload.zeroDeltaFraction = 0.0;
  workload.shape = RangeShape::Uniform;
  workload.versions = VersionPick::Uniform;
  workload.skew = 0.0;
  workload.axis = VariantAxis::None;
  workload.variants = {0.0};
  workload.queryTail = 0;
  return workload;
}

void printList() {
  std::cout << "id\taxis\tsizes\tops\tupdate%\tsummary\n";
  for (const Workload& workload : workloads()) {
    std::cout << workload.id << '\t' << axisName(workload.axis) << '\t';
    for (std::size_t i = 0; i < workload.sizes.size(); ++i) {
      std::cout << (i == 0 ? "" : ",") << workload.sizes[i];
    }
    std::cout << '\t'
              << (workload.axis == VariantAxis::UpdateBudget ? workload.queryTail
                                                             : workload.operations)
              << '\t' << static_cast<int>(workload.updateFraction * 100) << '\t' << workload.summary
              << '\n';
  }
}

/**
 * Shrink a workload so a smoke run finishes in well under a second while still
 * exercising every code path in it.
 */
Workload shrink(const Workload& workload) {
  Workload small = workload;
  small.sizes = {256};
  small.operations = workload.operations == 0 ? 0 : 400;
  small.queryTail = workload.queryTail == 0 ? 0 : 100;
  for (double& variant : small.variants) {
    if (small.axis == VariantAxis::UpdateBudget) {
      variant = std::min(variant, 300.0);
    } else if (small.axis == VariantAxis::RangeWidth) {
      variant = std::min(variant, 256.0);
    }
  }
  small.variants.erase(std::unique(small.variants.begin(), small.variants.end()),
                       small.variants.end());
  return small;
}

/**
 * The checkpoint interval a structure runs at when the workload is not
 * sweeping it.
 *
 * K balances an O(log n + n / K) update against an O(log n + K) query, so the
 * balancing choice is K = sqrt(n). Picking it from theory rather than by hand
 * matters: K is the one tuning knob any structure in this comparison has, and
 * a hand-chosen value invites the reading that it was chosen to lose. W7
 * sweeps K on the same traffic and reports where the measured optimum
 * actually falls.
 */
std::size_t balancedInterval(std::size_t size) {
  return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(std::sqrt(
                                      static_cast<double>(size)))));
}

void writeEnvironment(const std::string& path, const Options& options,
                      const ClockCalibration& calibration, bool performanceCores, int argc,
                      char** argv) {
  std::ofstream out(path);
  if (!out) {
    fail("cannot write " + path);
  }
  const std::time_t now = std::time(nullptr);
  out << "generated_utc=" << now << "\n";
  out << "build_type=" << VALSEG_BENCH_BUILD_TYPE << "\n";
  out << "compiler=" << VALSEG_BENCH_COMPILER << "\n";
  out << "compile_flags=" << VALSEG_BENCH_FLAGS << "\n";
  out << "cpp_standard=" << __cplusplus << "\n";
  out << "pointer_bits=" << sizeof(void*) * 8 << "\n";
  out << "base_seed=" << options.seed << "\n";
  out << "trials=" << options.trials << "\n";
  out << "warmup_trials=" << options.warmup << "\n";
  out << "warmup_seconds=" << options.warmupSeconds << "\n";
  out << "capped_trials=" << options.cappedTrials << "\n";
  out << "allocation_counting=" << (allocationCountingEnabled() ? "on" : "off") << "\n";
  out << "memory_cap_mib=" << options.capMiB << "\n";
  out << "clock_overhead_ns=" << calibration.overheadNs << "\n";
  out << "clock_resolution_ns=" << calibration.resolutionNs << "\n";
  out << "performance_core_qos=" << (performanceCores ? "requested" : "unavailable") << "\n";
  out << "execution_order=" << (options.shuffle ? "shuffled_per_trial" : "fixed") << "\n";
  out << "batch_trials=" << options.batchTrials << "\n";
  out << "command=";
  for (int i = 0; i < argc; ++i) {
    out << (i == 0 ? "" : " ") << argv[i];
  }
  out << "\n";
}

void refuseExistingCampaignOutput(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    std::ifstream existing(path);
    if (existing.good()) {
      fail("refusing to overwrite " + path + "; choose a new --tag or --out-dir");
    }
  }
}

/**
 * Put the machine into steady state before anything is recorded.
 *
 * A freshly started process does not run at the speed it will settle at. The
 * processor has not been asked for sustained work yet, and the allocator has
 * not grown a heap, so its first large requests fault in pages the later ones
 * reuse. Measured on this campaign, the first recorded trial of a cell came in
 * around 1.7 times its own steady-state cost and the effect took several
 * trials to decay -- and discarding more trials did not fix it, because the
 * ramp is a property of elapsed load rather than of how many replays have run.
 *
 * So the warm-up is a duration, not a count: replay a representative stream
 * until the clock says the machine has been busy long enough. It cycles
 * through the structures rather than warming one of them, because the effect
 * is proportional to how much a structure allocates and the allocator's caches
 * are per size class -- warming only the 32-byte-node structures left the one
 * that copies whole trees still paying it. Discarded per-cell trials still run
 * on top of this and handle the cache state of the particular cell.
 *
 * Full copying is left out: it reaches the memory limit within a few thousand
 * updates at these sizes, so it cannot be cycled, and it is excluded from the
 * throughput comparison for the same reason.
 */
void warmUp(std::size_t seconds, std::size_t size, std::uint64_t seed, std::size_t capBytes) {
  if (seconds == 0) {
    return;
  }
  Workload representative = *findWorkload("W1");
  representative.operations = 20000;
  const std::size_t warmSize = std::min<std::size_t>(size, 100000);
  const std::vector<ValueType> initial = initialArray(warmSize, seed);
  const std::vector<Operation> stream = generate(representative, warmSize, 0.0, seed);
  const std::size_t interval = balancedInterval(warmSize);
  const Clock::time_point deadline =
      Clock::now() + std::chrono::seconds(static_cast<std::int64_t>(seconds));

  while (Clock::now() < deadline) {
    for (const Structure& structure : structures()) {
      if (std::string(structure.name) == "full-copy") {
        continue;
      }
      structure.batch(initial, stream, interval, capBytes);
      if (Clock::now() >= deadline) {
        break;
      }
    }
  }
}

/**
 * One structure at one point of a workload's variant axis.
 */
struct Job {
  const Structure* structure;
  double variant;
  std::size_t interval;
};

int run(int argc, char** argv) {
  const Options options = parse(argc, argv);

  if (options.list) {
    printList();
    return 0;
  }

  const bool performanceCores = requestPerformanceCores();
  const ClockCalibration calibration = calibrateClock();

  const std::size_t capBytes = options.capMiB * 1024 * 1024;
  const std::string suffix = options.tag.empty() ? std::string() : "_" + options.tag;

  const std::string runsPath = options.outDir + "/runs" + suffix + ".csv";
  const std::string memoryPath = options.outDir + "/memory" + suffix + ".csv";
  const std::string environmentPath = options.outDir + "/environment" + suffix + ".txt";
  if (!options.smoke || options.outDirGiven) {
    refuseExistingCampaignOutput({runsPath, memoryPath, environmentPath});
  }

  std::ofstream runs(runsPath);
  std::ofstream memory(memoryPath);
  if (!runs || !memory) {
    fail("cannot write CSV into " + options.outDir + " (does it exist?)");
  }
  writeEnvironment(environmentPath, options, calibration, performanceCores, argc, argv);

  runs << "workload,structure,n,axis,variant,k,seed,trial,exec_order,updates,queries,"
          "build_ns,build_nodes,update_ns,query_ns,batch_ns,"
          "update_p50,update_p90,update_p99,update_p999,update_max,"
          "query_p50,query_p90,query_p99,query_p999,query_max,"
          "nodes,bytes,alloc_peak_bytes,alloc_live_bytes,alloc_count,"
          "clock_overhead_ns,clock_resolution_ns,checksum,status\n";
  memory << "workload,structure,n,axis,variant,seed,trial,op_index,versions,nodes,bytes\n";

  // (workload, n, variant, seed, trial) -> checksum of the first persistent
  // structure to run it. Every persistent structure must agree; the control
  // does not, since it answers historical queries against the latest state.
  std::map<std::string, std::uint64_t> expected;
  std::size_t mismatches = 0;

  // A cell that reached the memory cap has already reported its result. Its
  // remaining trials would re-measure a truncated replay, so they are skipped
  // once the cap has been seen this many times.
  std::map<std::string, std::size_t> cappedSeen;

  std::vector<Workload> plan = workloads();
  std::vector<Operation> traceStream;
  if (!options.trace.empty()) {
    std::size_t traceSize = 0;
    traceStream = loadTrace(options.trace, traceSize);
    plan.push_back(traceWorkload(traceSize));
  }

  for (const Workload& original : plan) {
    if (options.workloadFilter != "all" && options.workloadFilter != original.id) {
      continue;
    }
    const Workload workload = options.smoke ? shrink(original) : original;

    for (const std::size_t size : workload.sizes) {
      const std::size_t defaultInterval = balancedInterval(size);
      warmUp(options.warmupSeconds, size, options.seed, capBytes);

      // Trials are the outer loop and structures the inner one, so every
      // structure is measured under whatever the machine was doing during that
      // trial rather than one structure owning the first minute and another
      // the last. Within a trial the order is shuffled, and the order a
      // measurement ran in is recorded, so an order effect can be tested for
      // instead of assumed away.
      for (std::size_t attempt = 0; attempt < options.warmup + options.trials; ++attempt) {
        const bool recorded = attempt >= options.warmup;
        const std::size_t trial = recorded ? attempt - options.warmup : attempt;
        const std::uint64_t seed = options.seed + attempt;
        const std::vector<ValueType> initial = initialArray(size, seed);

        for (const double variant : workload.variants) {
          std::vector<Job> jobs;
          for (const Structure& structure : structures()) {
            if (options.structureFilter != "all" && options.structureFilter != structure.name) {
              continue;
            }
            std::size_t interval = defaultInterval;
            if (workload.axis == VariantAxis::CheckpointInterval) {
              // K is a parameter of exactly one structure. Every other
              // structure runs the sweep's traffic once, at the balanced
              // interval, and is reported at the first sweep point only.
              if (std::string(structure.name) != "checkpointing") {
                if (variant != workload.variants.front()) {
                  continue;
                }
              } else {
                interval = variant == 0.0 ? 0 : static_cast<std::size_t>(variant);
              }
            }
            jobs.push_back({&structure, variant, interval});
          }
          if (jobs.empty()) {
            continue;
          }

          if (options.shuffle) {
            std::mt19937_64 orderRng(seed ^ 0x5deece66dULL ^ static_cast<std::uint64_t>(size));
            std::shuffle(jobs.begin(), jobs.end(), orderRng);
          }

          const std::vector<Operation> stream =
              workload.id == "WT" ? traceStream : generate(workload, size, variant, seed);

          // An unbounded interval means "never checkpoint": the log is
          // replayed from version zero. It is the K -> infinity end of the
          // sweep and has to be expressed as a number the structure will
          // never reach.
          const std::size_t unbounded = stream.size() + 2;

          for (std::size_t position = 0; position < jobs.size(); ++position) {
            const Job& job = jobs[position];
            const std::size_t interval = job.interval == 0 ? unbounded : job.interval;

            const std::string cellKey = workload.id + "|" + job.structure->name + "|" +
                                        std::to_string(size) + "|" + std::to_string(job.variant);
            if (recorded && cappedSeen[cellKey] >= options.cappedTrials) {
              continue;
            }

            // The uninstrumented replay prices the instrumentation, so which
            // of the two passes runs first matters: whichever runs second
            // inherits a warm allocator and a warm cache. The order alternates
            // with the trial index so the advantage cancels across trials
            // instead of being handed to one pass every time.
            const bool priceInstrumentation = recorded && trial < options.batchTrials;
            const bool batchFirst = priceInstrumentation && trial % 2 == 1;

            std::int64_t batchNs = -1;
            if (batchFirst) {
              batchNs = job.structure->batch(initial, stream, interval, capBytes);
            }
            const TrialResult result = job.structure->run(initial, stream, interval, capBytes);
            if (!recorded) {
              continue;
            }
            if (std::string(result.status) == "memory_cap") {
              ++cappedSeen[cellKey];
            }
            if (priceInstrumentation && !batchFirst) {
              batchNs = job.structure->batch(initial, stream, interval, capBytes);
            }

            const std::string cell = workload.id + "|" + std::to_string(size) + "|" +
                                     std::to_string(job.variant) + "|" + std::to_string(seed);
            if (job.structure->historical && std::string(result.status) == "ok") {
              const auto inserted = expected.emplace(cell, result.checksum);
              if (!inserted.second && inserted.first->second != result.checksum) {
                std::cerr << "checksum mismatch: " << cell << " on " << job.structure->name << "\n";
                ++mismatches;
              }
            }

            const std::string key = workload.id + "," + job.structure->name + "," +
                                    std::to_string(size) + "," + axisName(workload.axis) + "," +
                                    std::to_string(job.variant);
            const std::string memoryKey =
                key + "," + std::to_string(seed) + "," + std::to_string(trial);

            runs << key << ',' << job.interval << ',' << seed << ',' << trial << ',' << position
                 << ',' << result.updates << ',' << result.queries << ',' << result.buildNs << ','
                 << result.buildNodes << ',' << result.updateNs << ',' << result.queryNs << ','
                 << batchNs << ',' << result.updateLatency.p50 << ',' << result.updateLatency.p90
                 << ',' << result.updateLatency.p99 << ',' << result.updateLatency.p999 << ','
                 << result.updateLatency.max << ',' << result.queryLatency.p50 << ','
                 << result.queryLatency.p90 << ',' << result.queryLatency.p99 << ','
                 << result.queryLatency.p999 << ',' << result.queryLatency.max << ','
                 << result.nodes << ',' << result.bytes << ',' << result.allocPeak << ','
                 << result.allocLive << ',' << result.allocCount << ',' << calibration.overheadNs
                 << ',' << calibration.resolutionNs << ',' << result.checksum << ','
                 << result.status << '\n';

            for (const Sample& sample : result.samples) {
              memory << memoryKey << ',' << sample.opIndex << ',' << sample.versions << ','
                     << sample.nodes << ',' << sample.bytes << '\n';
            }
          }
          runs.flush();
        }
      }
    }
    std::cout << "done " << workload.id << std::endl;
  }

  if (mismatches > 0) {
    std::cerr << mismatches << " checksum mismatch(es): the structures disagree on an answer\n";
    return 1;
  }
  return 0;
}

} // namespace
} // namespace valseg::bench

int main(int argc, char** argv) {
  try {
    return valseg::bench::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "valseg_bench: " << error.what() << "\n";
    return 2;
  }
}
