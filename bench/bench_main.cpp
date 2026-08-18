#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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
 * Everything one replay of one operation stream produced.
 */
struct TrialResult {
  std::size_t updates = 0;
  std::size_t queries = 0;
  std::int64_t buildNs = 0;
  std::int64_t updateNs = 0;
  std::int64_t queryNs = 0;
  std::size_t nodes = 0;
  std::size_t bytes = 0;
  std::size_t allocPeak = 0;
  std::size_t allocLive = 0;
  std::size_t allocCount = 0;
  std::uint64_t checksum = 0;
  const char* status = "ok";
  std::vector<Sample> samples;
};

using Clock = std::chrono::steady_clock;

std::int64_t nanosSince(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

/**
 * Replay one operation stream against one structure.
 *
 * Update and query time are accumulated separately; the clock is read once per
 * operation, which costs tens of nanoseconds against operations that cost
 * hundreds to thousands, and costs every structure the same.
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

  const std::size_t sampleEvery = std::max<std::size_t>(1, stream.size() / 20);

  for (std::size_t index = 0; index < stream.size(); ++index) {
    const Operation& op = stream[index];
    if (op.isUpdate) {
      const Clock::time_point start = Clock::now();
      adapter.update(op.left, op.right, op.delta);
      result.updateNs += nanosSince(start);
      ++result.updates;
    } else {
      const Clock::time_point start = Clock::now();
      const ValueType answer = adapter.query(op.version, op.left, op.right);
      result.queryNs += nanosSince(start);
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

  result.nodes = adapter.nodes();
  result.bytes = adapter.bytes();
  const AllocationStats allocation = allocationStats();
  result.allocPeak = allocation.peakBytes;
  result.allocLive = allocation.liveBytes;
  result.allocCount = allocation.allocations;
  return result;
}

using RunFunction = TrialResult (*)(const std::vector<ValueType>&, const std::vector<Operation>&,
                                    std::size_t, std::size_t);

/**
 * One structure under measurement.
 */
struct Structure {
  const char* name;
  RunFunction run;
  bool historical;
};

const std::vector<Structure>& structures() {
  static const std::vector<Structure> table = {
      {"lazy", &runTrial<LazyAdapter>, false},
      {"persistent", &runTrial<PersistentAdapter<PersistentLazySegmentTree>>, true},
      {"full-copy", &runTrial<PersistentAdapter<FullCopyPersistentSegmentTree>>, true},
      {"point-only", &runTrial<PersistentAdapter<PointOnlyPersistentSegmentTree>>, true},
      {"checkpointing", &runTrial<PersistentAdapter<CheckpointingSegmentTree>>, true},
      {"buffered", &runTrial<PersistentAdapter<BufferedPathCopyingSegmentTree>>, true},
      {"fat-node", &runTrial<PersistentAdapter<FatNodePersistentSegmentTree>>, true},
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
  std::string trace;
  bool smoke = false;
  bool list = false;
  bool outDirGiven = false;
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
    } else if (flag == "--trace" && hasValue) {
      options.trace = value;
      ++i;
    } else if (flag == "--cap-mib" && hasValue) {
      options.capMiB = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
      ++i;
    } else if (flag == "--help") {
      std::cout << "usage: valseg_bench [--workload W1|all] [--structure NAME|all]\n"
                   "                    [--out-dir DIR] [--tag SUFFIX] [--seed N]\n"
                   "                    [--trials N] [--warmup N] [--cap-mib N]\n"
                   "                    [--trace FILE] [--smoke] [--list]\n";
      std::exit(0);
    } else {
      fail("unknown or incomplete option: " + flag);
    }
  }
  if (options.trials == 0) {
    fail("--trials must be at least 1");
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
  workload.id = "W12";
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

void writeEnvironment(const std::string& path, const Options& options, int argc, char** argv) {
  std::ofstream out(path);
  if (!out) {
    fail("cannot write " + path);
  }
  const std::time_t now = std::time(nullptr);
  out << "generated_utc=" << now << "\n";
  out << "build_type=" << VALSEG_BENCH_BUILD_TYPE << "\n";
  out << "compiler=" << VALSEG_BENCH_COMPILER << "\n";
  out << "pointer_bits=" << sizeof(void*) * 8 << "\n";
  out << "base_seed=" << options.seed << "\n";
  out << "trials=" << options.trials << "\n";
  out << "warmup_trials=" << options.warmup << "\n";
  out << "allocation_counting=" << (allocationCountingEnabled() ? "on" : "off") << "\n";
  out << "memory_cap_mib=" << options.capMiB << "\n";
  out << "command=";
  for (int i = 0; i < argc; ++i) {
    out << (i == 0 ? "" : " ") << argv[i];
  }
  out << "\n";
}

int run(int argc, char** argv) {
  const Options options = parse(argc, argv);

  if (options.list) {
    printList();
    return 0;
  }

  const std::size_t capBytes = options.capMiB * 1024 * 1024;
  const std::string suffix = options.tag.empty() ? std::string() : "_" + options.tag;

  std::ofstream runs(options.outDir + "/runs" + suffix + ".csv");
  std::ofstream memory(options.outDir + "/memory" + suffix + ".csv");
  if (!runs || !memory) {
    fail("cannot write CSV into " + options.outDir + " (does it exist?)");
  }
  writeEnvironment(options.outDir + "/environment" + suffix + ".txt", options, argc, argv);

  runs << "workload,structure,n,axis,variant,seed,trial,updates,queries,"
          "build_ns,update_ns,query_ns,nodes,bytes,alloc_peak_bytes,alloc_live_bytes,"
          "alloc_count,checksum,status\n";
  memory << "workload,structure,n,axis,variant,seed,trial,op_index,versions,nodes,bytes\n";

  // (workload, n, variant, seed, trial) -> checksum of the first persistent
  // structure to run it. Every persistent structure must agree; the control
  // does not, since it answers historical queries against the latest state.
  std::map<std::string, std::uint64_t> expected;
  std::size_t mismatches = 0;

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
      for (const Structure& structure : structures()) {
        if (options.structureFilter != "all" && options.structureFilter != structure.name) {
          continue;
        }

        // K is a parameter of exactly one structure. Every other structure
        // runs the sweep's traffic once, at the default interval.
        std::vector<double> variants = workload.variants;
        if (workload.axis == VariantAxis::CheckpointInterval &&
            std::string(structure.name) != "checkpointing") {
          variants = {500.0};
        }

        for (const double variant : variants) {
          // Warm-up trials run and are thrown away: the first replay of a
          // cell pays for cold caches and for the allocator growing its arena,
          // and that cost belongs to no structure in particular.
          for (std::size_t attempt = 0; attempt < options.warmup + options.trials; ++attempt) {
            const bool recorded = attempt >= options.warmup;
            const std::size_t trial = recorded ? attempt - options.warmup : attempt;
            const std::uint64_t seed = options.seed + attempt;
            const std::vector<ValueType> initial = initialArray(size, seed);
            const std::vector<Operation> stream =
                workload.id == "W12" ? traceStream : generate(workload, size, variant, seed);

            std::size_t interval = 500;
            if (workload.axis == VariantAxis::CheckpointInterval) {
              interval = variant == 0.0 ? stream.size() + 1 : static_cast<std::size_t>(variant);
            }

            const TrialResult result = structure.run(initial, stream, interval, capBytes);
            if (!recorded) {
              continue;
            }

            const std::string cell = workload.id + "|" + std::to_string(size) + "|" +
                                     std::to_string(variant) + "|" + std::to_string(seed);
            if (structure.historical && std::string(result.status) == "ok") {
              const auto inserted = expected.emplace(cell, result.checksum);
              if (!inserted.second && inserted.first->second != result.checksum) {
                std::cerr << "checksum mismatch: " << cell << " on " << structure.name << "\n";
                ++mismatches;
              }
            }

            const std::string key = workload.id + "," + structure.name + "," +
                                    std::to_string(size) + "," + axisName(workload.axis) + "," +
                                    std::to_string(variant) + "," + std::to_string(seed) + "," +
                                    std::to_string(trial);

            runs << key << ',' << result.updates << ',' << result.queries << ',' << result.buildNs
                 << ',' << result.updateNs << ',' << result.queryNs << ',' << result.nodes << ','
                 << result.bytes << ',' << result.allocPeak << ',' << result.allocLive << ','
                 << result.allocCount << ',' << result.checksum << ',' << result.status << '\n';

            for (const Sample& sample : result.samples) {
              memory << key << ',' << sample.opIndex << ',' << sample.versions << ','
                     << sample.nodes << ',' << sample.bytes << '\n';
            }
          }
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
