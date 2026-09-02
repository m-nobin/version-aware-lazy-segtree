#include <algorithm>
#include <cerrno>
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
#include <dlfcn.h>
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <sched.h>
#endif

#include "adapters.hpp"
#include "allocation_counter.hpp"
#include "process_memory.hpp"
#include "structural_counts.hpp"
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
 * Raise this thread to the interactive quality-of-service class and read back
 * where the process may actually run.
 *
 * Apple silicon has performance and efficiency cores with different clocks and
 * different caches, and the scheduler is free to move a background thread onto
 * an efficiency core part-way through a trial. That turns a structure
 * comparison into a core-placement comparison. Asking for the interactive
 * class is the portable request that keeps the run on performance cores; it is
 * a request, not a guarantee, so the environment file records the class read
 * back after the request rather than the request itself. On Linux the
 * placement is the CPU affinity mask this process inherited from
 * bench/env/pin_linux.sh: a pin that did not take effect shows up as every
 * core, not as a bare "requested".
 */
std::string corePlacement() {
#if defined(__APPLE__)
  if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) != 0) {
    return "qos_request_failed";
  }
  qos_class_t actual = QOS_CLASS_UNSPECIFIED;
  if (pthread_get_qos_class_np(pthread_self(), &actual, nullptr) != 0) {
    return "qos_readback_failed";
  }
  return actual == QOS_CLASS_USER_INTERACTIVE ? "qos_user_interactive" : "qos_other";
#elif defined(__linux__)
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
    return "affinity_readback_failed";
  }
  std::string cpus = "affinity";
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &allowed)) {
      cpus += (cpus == "affinity" ? ":" : ",") + std::to_string(cpu);
    }
  }
  return cpus;
#else
  return "unavailable";
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
  std::size_t peakRss = 0;
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
                     std::size_t checkpointInterval, std::size_t capBytes, std::size_t timeEvery) {
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
    // timeEvery == 1 brackets every operation (the pilot's interleaved mode).
    // The registered sampled-latency mode times every timeEvery-th operation
    // and replays the rest unbracketed, so the clock-pair overhead scales with
    // the sampling rate instead of the stream length. update_ns/query_ns then
    // sum only the sampled operations; throughput never comes from this mode.
    const bool timed = timeEvery <= 1 || index % timeEvery == 0;
    if (op.isUpdate) {
      if (timed) {
        const Clock::time_point start = Clock::now();
        adapter.update(op.left, op.right, op.delta);
        const std::int64_t elapsed = nanosSince(start);
        result.updateNs += elapsed;
        updateSamples.push_back(static_cast<std::uint32_t>(
            std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max())));
      } else {
        adapter.update(op.left, op.right, op.delta);
      }
      ++result.updates;
    } else {
      ValueType answer = 0;
      if (timed) {
        const Clock::time_point start = Clock::now();
        answer = adapter.query(op.version, op.left, op.right);
        const std::int64_t elapsed = nanosSince(start);
        result.queryNs += elapsed;
        querySamples.push_back(static_cast<std::uint32_t>(
            std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max())));
      } else {
        answer = adapter.query(op.version, op.left, op.right);
      }
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
  result.peakRss = peakResidentBytes();
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
std::int64_t runBatched(const std::vector<ValueType>& initial, const std::vector<Operation>& stream,
                        std::size_t checkpointInterval, std::size_t capBytes) {
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

/**
 * The registered primary timing: one clock pair around the update batch and
 * one around the query batch.
 *
 * Queries mutate no version's answers, so replaying every update first leaves
 * each published version answering exactly what it answers in the interleaved
 * replay, and each query still targets the version recorded in the stream.
 * The cross-mode checksum is therefore identical, which is how a batch
 * campaign proves it replayed the same semantic work.
 *
 * Inside the timed regions the only instrumentation is the O(1) memory-cap
 * check per update (unavoidable: a run past the cap takes the machine down)
 * and the checksum accumulation per query (it is what keeps the query from
 * being dead code). Both are identical work for every structure. There are no
 * per-operation clock pairs, no latency samples and no growth samples; growth
 * curves come from the interleaved allocation runs.
 */
template <typename Adapter>
TrialResult runBatchTrial(const std::vector<ValueType>& initial,
                          const std::vector<Operation>& stream, std::size_t checkpointInterval,
                          std::size_t capBytes) {
  TrialResult result;
  Adapter adapter(checkpointInterval);
  resetAllocationStats();

  const Clock::time_point buildStart = Clock::now();
  adapter.build(initial);
  result.buildNs = nanosSince(buildStart);
  result.buildNodes = adapter.nodes();

  bool capped = false;
  const Clock::time_point updateStart = Clock::now();
  for (const Operation& op : stream) {
    if (!op.isUpdate) {
      continue;
    }
    adapter.update(op.left, op.right, op.delta);
    ++result.updates;
    if (adapter.bytes() > capBytes) {
      capped = true;
      break;
    }
  }
  result.updateNs = nanosSince(updateStart);

  if (capped) {
    // The cap truncated the version history, so the stream's later queries
    // would name versions that do not exist. The cell is a censored
    // feasibility outcome; its timing never enters a throughput ratio.
    result.status = "memory_cap";
  } else {
    const Clock::time_point queryStart = Clock::now();
    for (const Operation& op : stream) {
      if (op.isUpdate) {
        continue;
      }
      const ValueType answer = adapter.query(op.version, op.left, op.right);
      result.checksum = result.checksum * 1000003ULL + static_cast<std::uint64_t>(answer);
      ++result.queries;
      if (adapter.bytes() > capBytes) {
        // The external structure allocates during queries, so the cap can be
        // crossed here too.
        result.status = "memory_cap";
        break;
      }
    }
    result.queryNs = nanosSince(queryStart);
  }
  result.batchNs = result.updateNs + result.queryNs;

  result.nodes = adapter.nodes();
  result.bytes = adapter.bytes();
  const AllocationStats allocation = allocationStats();
  result.allocPeak = allocation.peakBytes;
  result.allocLive = allocation.liveBytes;
  result.allocCount = allocation.allocations;
  result.peakRss = peakResidentBytes();
  return result;
}

using RunFunction = TrialResult (*)(const std::vector<ValueType>&, const std::vector<Operation>&,
                                    std::size_t, std::size_t, std::size_t);
using BatchFunction = std::int64_t (*)(const std::vector<ValueType>&, const std::vector<Operation>&,
                                       std::size_t, std::size_t);
using BatchTrialFunction = TrialResult (*)(const std::vector<ValueType>&,
                                           const std::vector<Operation>&, std::size_t, std::size_t);

/**
 * One structure under measurement.
 */
struct Structure {
  const char* name;
  RunFunction run;
  BatchFunction batch;
  BatchTrialFunction batchTrial;
  bool historical;
};

const std::vector<Structure>& structures() {
  static const std::vector<Structure> table = {
      {"lazy", &runTrial<LazyAdapter>, &runBatched<LazyAdapter>, &runBatchTrial<LazyAdapter>,
       false},
      {"persistent", &runTrial<PersistentAdapter<PersistentLazySegmentTree>>,
       &runBatched<PersistentAdapter<PersistentLazySegmentTree>>,
       &runBatchTrial<PersistentAdapter<PersistentLazySegmentTree>>, true},
      {"copy-on-push", &runTrial<PersistentAdapter<CopyOnPushSegmentTree>>,
       &runBatched<PersistentAdapter<CopyOnPushSegmentTree>>,
       &runBatchTrial<PersistentAdapter<CopyOnPushSegmentTree>>, true},
      {"full-copy", &runTrial<PersistentAdapter<FullCopyPersistentSegmentTree>>,
       &runBatched<PersistentAdapter<FullCopyPersistentSegmentTree>>,
       &runBatchTrial<PersistentAdapter<FullCopyPersistentSegmentTree>>, true},
      {"point-only", &runTrial<PersistentAdapter<PointOnlyPersistentSegmentTree>>,
       &runBatched<PersistentAdapter<PointOnlyPersistentSegmentTree>>,
       &runBatchTrial<PersistentAdapter<PointOnlyPersistentSegmentTree>>, true},
      {"checkpointing", &runTrial<PersistentAdapter<CheckpointingSegmentTree>>,
       &runBatched<PersistentAdapter<CheckpointingSegmentTree>>,
       &runBatchTrial<PersistentAdapter<CheckpointingSegmentTree>>, true},
      {"buffered", &runTrial<PersistentAdapter<BufferedPathCopyingSegmentTree>>,
       &runBatched<PersistentAdapter<BufferedPathCopyingSegmentTree>>,
       &runBatchTrial<PersistentAdapter<BufferedPathCopyingSegmentTree>>, true},
      {"fat-node", &runTrial<PersistentAdapter<FatNodePersistentSegmentTree>>,
       &runBatched<PersistentAdapter<FatNodePersistentSegmentTree>>,
       &runBatchTrial<PersistentAdapter<FatNodePersistentSegmentTree>>, true},
      {"external", &runTrial<ExternalAdapter>, &runBatched<ExternalAdapter>,
       &runBatchTrial<ExternalAdapter>, true},
  };
  return table;
}

/**
 * How a trial is timed.
 */
enum class TimingMode : std::uint8_t {
  Interleaved, ///< the pilot protocol: a clock pair around every operation
  Batch,       ///< the registered primary: one clock pair per operation batch
  Latency      ///< the registered secondary: sampled per-operation clock pairs
};

const char* timingModeName(TimingMode mode) {
  switch (mode) {
  case TimingMode::Interleaved:
    return "interleaved";
  case TimingMode::Batch:
    return "batch";
  case TimingMode::Latency:
    return "latency";
  }
  return "interleaved";
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
  TimingMode mode = TimingMode::Interleaved;
  std::size_t sampleEvery = 64; ///< latency mode: time every Nth operation
  std::size_t sizeFilter = 0;   ///< 0 = every size the workload defines
  double variantFilter = 0.0;   ///< meaningful only when variantFilterSet
  long trialIndex = -1;         ///< -1 = every recorded trial
  bool variantFilterSet = false;
  bool smoke = false;
  bool structural = false;
  bool list = false;
  bool listCells = false;
  bool outDirGiven = false;
  bool shuffle = true;
  std::string traceId;
  long execOrder = -1;  ///< -1 = derive from in-process shuffle position
  long processSeq = -1; ///< -1 = not supplied by the schedule
};

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "valseg_bench: " << message << "\n";
  std::exit(2);
}

/**
 * Checked replacements for strtoull/strtol/strtod: reject an empty value,
 * a negative value where none is meaningful, trailing garbage after the
 * number, and anything strtoul or strtod itself flags as out of range. The
 * unchecked originals silently wrapped ("--n -5") or silently truncated
 * ("--trials 1abc") instead of failing the campaign before it started.
 */
std::uint64_t parseUnsigned(const std::string& text, const std::string& flag) {
  if (text.empty() || text[0] == '-') {
    fail(flag + " must be a non-negative integer: " + text);
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    fail(flag + " must be a non-negative integer: " + text);
  }
  if (errno == ERANGE) {
    fail(flag + " is out of range: " + text);
  }
  return static_cast<std::uint64_t>(parsed);
}

long parseLong(const std::string& text, const std::string& flag) {
  if (text.empty()) {
    fail(flag + " must be an integer: " + text);
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    fail(flag + " must be an integer: " + text);
  }
  if (errno == ERANGE) {
    fail(flag + " is out of range: " + text);
  }
  return parsed;
}

long long parseLongLong(const std::string& text, const std::string& flag) {
  if (text.empty()) {
    fail(flag + " must be an integer: " + text);
  }
  errno = 0;
  char* end = nullptr;
  const long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    fail(flag + " must be an integer: " + text);
  }
  if (errno == ERANGE) {
    fail(flag + " is out of range: " + text);
  }
  return parsed;
}

double parseDouble(const std::string& text, const std::string& flag) {
  if (text.empty()) {
    fail(flag + " must be a number: " + text);
  }
  errno = 0;
  char* end = nullptr;
  const double parsed = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') {
    fail(flag + " must be a number: " + text);
  }
  if (errno == ERANGE) {
    fail(flag + " is out of range: " + text);
  }
  return parsed;
}

Options parse(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const bool hasValue = i + 1 < argc;
    const std::string value = hasValue ? argv[i + 1] : std::string();
    if (flag == "--list") {
      options.list = true;
    } else if (flag == "--list-cells") {
      options.listCells = true;
    } else if (flag == "--smoke") {
      options.smoke = true;
    } else if (flag == "--structural") {
      options.structural = true;
    } else if (flag == "--warmup-seconds" && hasValue) {
      options.warmupSeconds = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--batch-trials" && hasValue) {
      options.batchTrials = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--mode" && hasValue) {
      if (value == "interleaved") {
        options.mode = TimingMode::Interleaved;
      } else if (value == "batch") {
        options.mode = TimingMode::Batch;
      } else if (value == "latency") {
        options.mode = TimingMode::Latency;
      } else {
        fail("--mode must be interleaved, batch or latency");
      }
      ++i;
    } else if (flag == "--sample-every" && hasValue) {
      options.sampleEvery = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--n" && hasValue) {
      options.sizeFilter = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--variant" && hasValue) {
      options.variantFilter = parseDouble(value, flag);
      options.variantFilterSet = true;
      ++i;
    } else if (flag == "--trial-index" && hasValue) {
      options.trialIndex = parseLong(value, flag);
      ++i;
    } else if (flag == "--exec-order" && hasValue) {
      options.execOrder = parseLong(value, flag);
      ++i;
    } else if (flag == "--process-seq" && hasValue) {
      options.processSeq = parseLong(value, flag);
      ++i;
    } else if (flag == "--trace-id" && hasValue) {
      options.traceId = value;
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
      options.seed = parseUnsigned(value, flag);
      ++i;
    } else if (flag == "--trials" && hasValue) {
      options.trials = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--warmup" && hasValue) {
      options.warmup = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--capped-trials" && hasValue) {
      options.cappedTrials = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--trace" && hasValue) {
      options.trace = value;
      ++i;
    } else if (flag == "--cap-mib" && hasValue) {
      options.capMiB = static_cast<std::size_t>(parseUnsigned(value, flag));
      ++i;
    } else if (flag == "--help") {
      std::cout << "usage: valseg_bench [--workload W1|all] [--structure NAME|all]\n"
                   "                    [--mode interleaved|batch|latency] [--sample-every N]\n"
                   "                    [--n SIZE] [--variant VALUE] [--trial-index K]\n"
                   "                    [--out-dir DIR] [--tag SUFFIX] [--seed N]\n"
                   "                    [--trials N] [--warmup N] [--warmup-seconds N]\n"
                   "                    [--capped-trials N]\n"
                   "                    [--cap-mib N] [--trace FILE] [--trace-id ID]\n"
                   "                    [--batch-trials N] [--exec-order N] [--process-seq N]\n"
                   "                    [--no-shuffle] [--smoke] [--structural] [--list]\n";
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
  if (options.sampleEvery == 0) {
    fail("--sample-every must be at least 1");
  }
  if (options.trialIndex >= 0 && static_cast<std::size_t>(options.trialIndex) >= options.trials) {
    fail("--trial-index must be below --trials");
  }
  if (options.smoke && !options.outDirGiven) {
    options.outDir = ".";
  }
  const bool traceFilter = options.workloadFilter == "WT" && !options.trace.empty();
  if (options.workloadFilter != "all" && !traceFilter &&
      findWorkload(options.workloadFilter) == nullptr) {
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
 * The whole file is validated before anything is timed: every range must lie
 * inside `[0, n)`, and a query may only name a version that an earlier update
 * has already published (version 0 exists after the build; each update adds
 * one). This is the interleaved-order rule, which batch replay also
 * satisfies because it runs every update before any query.
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
  std::size_t published = 1;
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

    const std::string where = path + ":" + std::to_string(lineNumber);
    if (field[0] == "n" && field.size() == 2) {
      if (size != 0) {
        fail(where + ": n is declared more than once");
      }
      size = static_cast<std::size_t>(parseUnsigned(field[1], where + " n"));
      continue;
    }
    if (field.size() != 4 || (field[0] != "u" && field[0] != "q")) {
      fail(where + ": expected n,<size> or u|q,l,r,arg");
    }
    if (size == 0) {
      fail(where + ": n must be declared before the first operation");
    }
    const std::size_t left = static_cast<std::size_t>(parseUnsigned(field[1], where + " left"));
    const std::size_t right = static_cast<std::size_t>(parseUnsigned(field[2], where + " right"));
    if (left > right || right >= size) {
      fail(where + ": range [" + std::to_string(left) + ", " + std::to_string(right) +
           "] is not inside [0, " + std::to_string(size) + ")");
    }
    if (field[0] == "u") {
      const long long delta = parseLongLong(field[3], where + " delta");
      stream.push_back({true, left, right, 0, static_cast<ValueType>(delta)});
      ++published;
    } else {
      const std::size_t version =
          static_cast<std::size_t>(parseUnsigned(field[3], where + " version"));
      if (version >= published) {
        fail(where + ": version " + std::to_string(version) + " is not published yet (" +
             std::to_string(published) + " versions exist at this point)");
      }
      stream.push_back({false, left, right, version, 0});
    }
  }

  if (size == 0 || stream.empty()) {
    fail(path + ": trace must declare a non-zero n and contain at least one operation");
  }
  return stream;
}

/**
 * The workload record a trace is reported under. Its id is the --trace-id
 * label (WT01...) the CSV groups by, so the id cannot identify a trace; the
 * summary can, and isReplayedTrace is the only test the replay loop uses.
 */
constexpr const char* kReplayedTraceSummary = "replayed trace";

bool isReplayedTrace(const Workload& workload) {
  return workload.summary == kReplayedTraceSummary;
}

Workload traceWorkload(std::size_t size, const std::string& traceId) {
  Workload workload;
  workload.id = traceId.empty() ? "WT" : traceId;
  workload.summary = kReplayedTraceSummary;
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
  Workload shrunk = workload;
  shrunk.sizes = {256};
  shrunk.operations = workload.operations == 0 ? 0 : 400;
  shrunk.queryTail = workload.queryTail == 0 ? 0 : 100;
  for (double& variant : shrunk.variants) {
    if (shrunk.axis == VariantAxis::UpdateBudget) {
      variant = std::min(variant, 300.0);
    } else if (shrunk.axis == VariantAxis::RangeWidth) {
      variant = std::min(variant, 256.0);
    }
  }
  shrunk.variants.erase(std::unique(shrunk.variants.begin(), shrunk.variants.end()),
                        shrunk.variants.end());
  return shrunk;
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
  return std::max<std::size_t>(
      1, static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(size)))));
}

/**
 * The library that actually provides `malloc` in this process.
 *
 * A preloaded allocator is a registered sensitivity dimension, so recording
 * the request (`LD_PRELOAD` / `DYLD_INSERT_LIBRARIES`) is not evidence: an
 * ignored preload would measure the default allocator twice and read as
 * insensitivity to the allocator. The registered analysis compares this value
 * across the two campaigns, so it must describe the resolved provider rather
 * than the intent. Returns `unknown` where the platform cannot report it.
 */
std::string mallocProvider() {
#if defined(__APPLE__) || defined(__linux__)
  void* symbol = dlsym(RTLD_DEFAULT, "malloc");
  Dl_info info{};
  if (symbol != nullptr && dladdr(symbol, &info) != 0 && info.dli_fname != nullptr) {
    return info.dli_fname;
  }
#endif
  return "unknown";
}

void writeEnvironment(const std::string& path, const Options& options,
                      const ClockCalibration& calibration, const std::string& placement, int argc,
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
  out << "malloc_provider=" << mallocProvider() << "\n";
  out << "base_seed=" << options.seed << "\n";
  out << "trials=" << options.trials << "\n";
  out << "warmup_trials=" << options.warmup << "\n";
  out << "warmup_seconds=" << options.warmupSeconds << "\n";
  out << "capped_trials=" << options.cappedTrials << "\n";
  out << "allocation_counting=" << (allocationCountingEnabled() ? "on" : "off") << "\n";
  out << "memory_cap_mib=" << options.capMiB << "\n";
  out << "clock_overhead_ns=" << calibration.overheadNs << "\n";
  out << "clock_resolution_ns=" << calibration.resolutionNs << "\n";
  out << "core_placement=" << placement << "\n";
  out << "execution_order=" << (options.shuffle ? "shuffled_per_trial" : "fixed") << "\n";
  out << "batch_trials=" << options.batchTrials << "\n";
  out << "timing_mode=" << timingModeName(options.mode) << "\n";
  out << "sample_every=" << (options.mode == TimingMode::Latency ? options.sampleEvery : 0) << "\n";
  out << "n_filter=" << options.sizeFilter << "\n";
  out << "variant_filter=";
  if (options.variantFilterSet) {
    out << options.variantFilter;
  } else {
    out << "all";
  }
  out << "\n";
  out << "trial_index=" << options.trialIndex << "\n";
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
void warmUp(std::size_t seconds, std::size_t size, std::uint64_t seed, std::size_t capBytes,
            const std::string& structureFilter) {
  // full-copy is never warmed (see the loop below); waiting out the deadline
  // with nothing to run would just idle the process.
  if (seconds == 0 || structureFilter == "full-copy") {
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
      // Under the fresh-process protocol only one structure is ever measured
      // in this process; warming any other one still inflates the whole
      // process's high-water-mark RSS (process_memory.hpp), contaminating
      // the very peak-RSS reading that process exists to isolate.
      if (structureFilter != "all" && std::string(structure.name) != structureFilter) {
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

/**
 * Write structural_<tag>-W*.csv: one row per (workload, n, variant, seed)
 * for the seeds the timing campaign records under the same options, so the
 * rows join the runs CSV on those columns. No structure is built and nothing
 * is timed; --structure is ignored in this mode. With --trace the plan is
 * that one replayed trace: its counts do not depend on the seed, but one row
 * per recorded seed is still written so the rows join the trace phase's runs
 * CSV on the same key.
 */
int runStructural(const Options& options) {
  const std::string suffix = options.tag.empty() ? std::string() : "_" + options.tag;
  std::vector<Workload> plan = workloads();
  std::vector<Operation> traceStream;
  if (!options.trace.empty()) {
    std::size_t traceSize = 0;
    traceStream = loadTrace(options.trace, traceSize);
    plan = {traceWorkload(traceSize, options.traceId)};
  }
  for (const Workload& original : plan) {
    if (!isReplayedTrace(original) && options.workloadFilter != "all" &&
        options.workloadFilter != original.id) {
      continue;
    }
    const Workload workload =
        options.smoke && !isReplayedTrace(original) ? shrink(original) : original;
    const std::string path = options.outDir + "/structural" + suffix + "-" + workload.id + ".csv";
    if (!options.smoke || options.outDirGiven) {
      refuseExistingCampaignOutput({path});
    }
    std::ofstream out(path);
    if (!out) {
      fail("cannot write CSV into " + options.outDir + " (does it exist?)");
    }
    out << "workload,n,axis,variant,seed,k,stream_fingerprint,updates,nonzero_updates,queries,"
           "sum_update_visits,sum_checkpoint_update_visits,sum_pushes,sum_intersecting,"
           "sum_query_visits,sum_replay_entries,sum_query_version_distance,"
           "full_coverage_updates,full_coverage_queries,latest_version_queries\n";
    for (const std::size_t size : workload.sizes) {
      for (std::size_t attempt = options.warmup; attempt < options.warmup + options.trials;
           ++attempt) {
        const std::uint64_t seed = options.seed + attempt;
        for (const double variant : workload.variants) {
          const std::vector<Operation> stream =
              isReplayedTrace(workload) ? traceStream : generate(workload, size, variant, seed);
          std::size_t interval = balancedInterval(size);
          if (workload.axis == VariantAxis::CheckpointInterval) {
            interval = variant == 0.0 ? 0 : static_cast<std::size_t>(variant);
          }
          const StructuralCounts counts = structuralCounts(stream, size, interval);
          out << workload.id << ',' << size << ',' << axisName(workload.axis) << ','
              << std::to_string(variant) << ',' << seed << ',' << interval << ','
              << fingerprintText(streamFingerprint(stream, size, seed)) << ',' << counts.updates
              << ',' << counts.nonZeroUpdates << ',' << counts.queries << ',' << counts.updateVisits
              << ',' << counts.checkpointUpdateVisits << ',' << counts.pushes << ','
              << counts.intersecting << ',' << counts.queryVisits << ',' << counts.replayEntries
              << ',' << counts.queryVersionDistance << ',' << counts.fullCoverageUpdates << ','
              << counts.fullCoverageQueries << ',' << counts.latestVersionQueries << '\n';
        }
      }
    }
    std::cout << "structural " << workload.id << '\n';
  }
  return 0;
}

int run(int argc, char** argv) {
  const Options options = parse(argc, argv);
  // The process's own high-water mark before any workload data, trace or
  // structure exists: the baseline peak_rss_bytes grows from, per trial.
  const std::size_t initialRss = peakResidentBytes();

  if (options.list) {
    printList();
    return 0;
  }
  if (options.listCells) {
    // Machine-readable cell inventory for the fresh-process orchestrator, so
    // the binary stays the single source of truth for the run matrix. The
    // variant text is what --variant parses back to the identical double.
    std::cout << "workload,n,axis,variant\n";
    for (const Workload& workload : workloads()) {
      for (const std::size_t size : workload.sizes) {
        for (const double variant : workload.variants) {
          std::cout << workload.id << ',' << size << ',' << axisName(workload.axis) << ','
                    << std::to_string(variant) << '\n';
        }
      }
    }
    return 0;
  }
  if (options.structural) {
    return runStructural(options);
  }

  // A trace is read and fully validated before anything is created on disk,
  // so a malformed trace fails before the campaign begins rather than after
  // leaving a header-only runs/memory/environment file behind.
  std::vector<Workload> plan = workloads();
  std::vector<Operation> traceStream;
  if (!options.trace.empty()) {
    std::size_t traceSize = 0;
    traceStream = loadTrace(options.trace, traceSize);
    plan.push_back(traceWorkload(traceSize, options.traceId));
  }

  const std::string placement = corePlacement();
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
  writeEnvironment(environmentPath, options, calibration, placement, argc, argv);

  runs << "workload,structure,n,axis,variant,k,seed,trial,exec_order,process_seq,initial_rss_bytes,"
          "updates,queries,"
          "build_ns,build_nodes,update_ns,query_ns,batch_ns,"
          "update_p50,update_p90,update_p99,update_p999,update_max,"
          "query_p50,query_p90,query_p99,query_p999,query_max,"
          "nodes,bytes,alloc_peak_bytes,alloc_live_bytes,alloc_count,peak_rss_bytes,"
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

  for (const Workload& original : plan) {
    // A trace workload's id is relabeled to --trace-id for CSV grouping
    // (each registered external draw needs its own label), but --workload WT
    // remains the CLI activation keyword regardless of that relabeling.
    const bool isSelectedTrace =
        !options.trace.empty() && options.workloadFilter == "WT" && isReplayedTrace(original);
    if (options.workloadFilter != "all" && options.workloadFilter != original.id &&
        !isSelectedTrace) {
      continue;
    }
    const Workload workload = options.smoke ? shrink(original) : original;

    for (const std::size_t size : workload.sizes) {
      if (options.sizeFilter != 0 && size != options.sizeFilter) {
        continue;
      }
      const std::size_t defaultInterval = balancedInterval(size);
      warmUp(options.warmupSeconds, size, options.seed, capBytes, options.structureFilter);

      // Trials are the outer loop and structures the inner one, so every
      // structure is measured under whatever the machine was doing during that
      // trial rather than one structure owning the first minute and another
      // the last. Within a trial the order is shuffled, and the order a
      // measurement ran in is recorded, so an order effect can be tested for
      // instead of assumed away.
      for (std::size_t attempt = 0; attempt < options.warmup + options.trials; ++attempt) {
        const bool recorded = attempt >= options.warmup;
        const std::size_t trial = recorded ? attempt - options.warmup : attempt;
        // Fresh-process orchestration runs exactly one recorded trial per
        // process. Warm-up attempts still run, and the seed of recorded trial
        // k is options.seed + warmup + k in every mode, so a sharded campaign
        // replays the same streams a monolithic one would.
        if (options.trialIndex >= 0 && recorded &&
            trial != static_cast<std::size_t>(options.trialIndex)) {
          continue;
        }
        const std::uint64_t seed = options.seed + attempt;
        const std::vector<ValueType> initial = initialArray(size, seed);

        for (const double variant : workload.variants) {
          // Exact comparison on purpose: the filter value is parsed from the
          // same decimal text the variant table was compiled from, so equal
          // text means an identical double.
          if (options.variantFilterSet && variant != options.variantFilter) {
            continue;
          }
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
              isReplayedTrace(workload) ? traceStream : generate(workload, size, variant, seed);

          // An unbounded interval means "never checkpoint": the log is
          // replayed from version zero. It is the K -> infinity end of the
          // sweep and has to be expressed as a number the structure will
          // never reach.
          const std::size_t unbounded = stream.size() + 2;

          for (std::size_t position = 0; position < jobs.size(); ++position) {
            const Job& job = jobs[position];
            // A fresh confirmatory process measures exactly one structure, so
            // the in-process shuffle position is always 0; the schedule's
            // within-block order and global sequence, passed in from
            // confirm_schedule.py, are what the order-effect regression
            // actually needs. Unset (-1) falls back to the legacy in-process
            // position for monolithic/manual invocations.
            const long execOrder =
                options.execOrder >= 0 ? options.execOrder : static_cast<long>(position);
            const long processSeq = options.processSeq;
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
            // instead of being handed to one pass every time. Only the
            // interleaved mode carries instrumentation worth pricing; batch
            // mode is its own two-clock-read replay and latency mode is
            // priced by its sampling rate.
            const bool priceInstrumentation =
                options.mode == TimingMode::Interleaved && recorded && trial < options.batchTrials;
            const bool batchFirst = priceInstrumentation && trial % 2 == 1;

            std::int64_t batchNs = -1;
            if (batchFirst) {
              batchNs = job.structure->batch(initial, stream, interval, capBytes);
            }
            const std::size_t timeEvery =
                options.mode == TimingMode::Latency ? options.sampleEvery : 1;
            const TrialResult result =
                options.mode == TimingMode::Batch
                    ? job.structure->batchTrial(initial, stream, interval, capBytes)
                    : job.structure->run(initial, stream, interval, capBytes, timeEvery);
            if (!recorded) {
              continue;
            }
            if (std::string(result.status) == "memory_cap") {
              ++cappedSeen[cellKey];
            }
            if (priceInstrumentation && !batchFirst) {
              batchNs = job.structure->batch(initial, stream, interval, capBytes);
            }
            if (options.mode == TimingMode::Batch) {
              batchNs = result.batchNs;
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

            runs << key << ',' << job.interval << ',' << seed << ',' << trial << ',' << execOrder
                 << ',' << processSeq << ',' << initialRss << ',' << result.updates << ','
                 << result.queries << ',' << result.buildNs << ',' << result.buildNodes << ','
                 << result.updateNs << ',' << result.queryNs << ',' << batchNs << ','
                 << result.updateLatency.p50 << ',' << result.updateLatency.p90 << ','
                 << result.updateLatency.p99 << ',' << result.updateLatency.p999 << ','
                 << result.updateLatency.max << ',' << result.queryLatency.p50 << ','
                 << result.queryLatency.p90 << ',' << result.queryLatency.p99 << ','
                 << result.queryLatency.p999 << ',' << result.queryLatency.max << ','
                 << result.nodes << ',' << result.bytes << ',' << result.allocPeak << ','
                 << result.allocLive << ',' << result.allocCount << ',' << result.peakRss << ','
                 << calibration.overheadNs << ',' << calibration.resolutionNs << ','
                 << result.checksum << ',' << result.status << '\n';

            for (const Sample& sample : result.samples) {
              memory << memoryKey << ',' << sample.opIndex << ',' << sample.versions << ','
                     << sample.nodes << ',' << sample.bytes << '\n';
            }
          }
          runs.flush();
        }
      }
    }
    std::cout << "done " << workload.id << '\n';
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
