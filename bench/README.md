# Benchmark harness

Replays one operation stream per workload against every structure and writes
raw CSV. The stream is generated from the workload alone, so every structure
sees exactly the same operations in the same order.

The campaign recorded under `bench/results/` is an **exploratory pilot**: one
machine, one primary compiler and allocator, run before any analysis protocol
was registered. It exists to find harness defects, choose estimands and size a
later confirmatory campaign. It is not confirmatory evidence and is never
pooled with confirmatory data. Raw campaign data and generated artifacts under
`bench/results/` are local and not version controlled. The analysis/report
sources, raw checksum manifest and `bench/results/README.md` provenance record
are version controlled.

## Reproduce the preserved pilot

With the locally preserved `bench/results/raw/` data present:

```sh
bench/verify_pilot.sh
```

This verifies all 96 raw-file checksums, rebuilds the locked analysis outputs,
checks the 357-cell inventory and compiles the exploratory report. It does not
rerun the measurements.

## Run a new campaign

```sh
cmake --preset release-verify
cmake --build build/release-verify --target valseg_bench valseg_bench_alloc
bench/run_campaign.sh timing 11 2026-08-30-machine-a
bench/run_campaign.sh alloc 3 2026-08-30-machine-a
```

Measure on the release build. A Debug build measures the debug allocator.
The required campaign ID creates
`bench/results/campaigns/<id>/raw/`. Existing output is never overwritten; use
a new ID for a rerun. A confirmatory campaign must start from a clean commit.

`run_campaign.sh` runs one process per workload rather than one for the whole
matrix. The run stays restartable, partial results are usable, and
`collect_environment.sh` records the machine again before each workload, so a
change part-way through a campaign — power source, load, thermal state — lands
in the record instead of silently in the numbers.

| Flag | Default | Meaning |
|---|---|---|
| `--workload` | `all` | one of `W1` .. `W12` |
| `--structure` | `all` | `lazy`, `persistent`, `copy-on-push`, `full-copy`, `point-only`, `checkpointing`, `buffered`, `fat-node`, `external` |
| `--mode` | `interleaved` | `interleaved` (a clock pair around every operation), `batch` (the registered primary: one clock pair per operation batch), `latency` (sampled clock pairs) |
| `--sample-every` | `64` | latency mode: time every Nth operation |
| `--n` | all | run only this array size |
| `--variant` | all | run only this variant-axis value |
| `--trial-index` | all | run only this recorded trial; warm-up still runs, seeds stay aligned with a monolithic run |
| `--list-cells` | off | print the (workload, n, axis, variant) inventory and exit |
| `--out-dir` | `bench/results/raw` | must already exist; non-smoke output files must not already exist |
| `--tag` | none | suffix for output filenames; the binary refuses a duplicate filename |
| `--seed` | `20260818` | attempt *i* uses `seed + i` |
| `--trials` | `5` | recorded trials per cell |
| `--warmup` | `1` | trials run and discarded before recording |
| `--warmup-seconds` | `20` | seconds of load before any trial runs; see below |
| `--capped-trials` | `2` | trials for a cell that reached the memory cap |
| `--batch-trials` | `2` | trials that also get an untimed replay, to price the timing |
| `--no-shuffle` | off | run structures in a fixed order instead of a shuffled one |
| `--cap-mib` | `4096` | a trial stops when retained payload passes this |
| `--trace` | none | replay an operation trace as `WT`; see below |
| `--smoke` | off | n = 256, 400 operations, for CI |
| `--list` | off | print the workload table and exit |

W1-W5 sweep five decades of array size, 1e3 to 1e7. The 1e7 cells dominate the
wall clock and the memory: the initial tree alone is 2n-1 nodes, so a structure
that stores a whole tree per version reaches the 4096 MiB cap within a few
versions there. That is the feasibility result, not a failure, but it costs a
full allocation of a 1e7 tree per trial to reobserve. Run the excluded dry-run
seeds (`VALSEG_DRY_RUN=1`) once per machine before a confirmatory campaign and
add the 1e7 cells that cap to `bench/capped_cells.csv`, so they run two trials
rather than twenty.

Most of what used to make a campaign take hours was the no-sharing baselines
replaying eleven times into the same memory ceiling; `--capped-trials` stops
that, since the ceiling is the result and re-measuring a truncated replay adds
nothing. Run one workload at a time with `--workload` while iterating, and
`--n` to skip the largest size while the harness itself is what you are
testing.

## The registered confirmatory pipeline

Everything above describes the pilot-era workflow. The confirmatory campaign
runs under the frozen protocol in
[docs/research/registered-protocol.md](../docs/research/registered-protocol.md)
instead, and no confirmatory seed runs before that protocol's immutable
deposit.

The differences that matter:

- **Batch timing is primary.** `--mode batch` replays every update with one
  clock pair around the whole batch, then every query with another. Queries
  mutate no version's answers, so the published versions and the answer
  checksum are identical to the interleaved replay's — each smoke run checks
  exactly that. `--mode latency` supplies the separate sampled-latency
  distribution; the interleaved mode survives for allocation runs and
  diagnostics.
- **One process per (cell, structure, trial).** `bench/confirm_schedule.py`
  turns the binary's own `--list-cells` inventory into a balanced,
  seed-shuffled schedule; `bench/run_confirmatory.sh <campaign-id> <phase>`
  executes it resumably, capturing the environment before every process.
  Phases: `structural`, `timing`, `alloc`, `latency`, `trace`. Fresh
  processes are what make `peak_rss_bytes` a per-cell number. Trial counts
  per cell class live in `bench/primary_cells.csv` (registered H2 cells, 40
  trials) and `bench/capped_cells.csv` (pilot-known cap cells, 2 trials);
  everything else runs 20. `VALSEG_DRY_RUN=1` switches to the excluded
  dry-run seeds.
- **Sensitivity paths.** The `release-verify-gcc` and `release-verify-clang`
  presets build the second-compiler binaries; `bench/run_sensitivity.sh`
  reruns the registered subset under a preloaded second allocator.
  `bench/env/pin_linux.sh` and `bench/env/pin_macos.sh` put each machine
  into, and record, the registered measurement state.
- **Confirmatory statistics live in `bench/analysis/confirm.py`** — paired
  log-ratio intervals, the four-state classification against `delta = 1.05`,
  the Holm-controlled primary family, the BH-flagged exploratory regime map,
  the mixed-effects pooled view, exact H1 identity checks, cross-process
  checksum verification and censored feasibility. `bench/analysis/blind.py`
  seals the structure labels per campaign before measurement and the primary
  analysis runs blinded.
- **Registration is a file list with hashes.** `bench/make_registration.sh`
  writes `docs/research/registration-manifest.txt` over every frozen file;
  the deposit of that manifest with the protocol is the registration
  timestamp.

## The external structure

`external` is the one implementation this repository's authors did not
write: TheAlgorithms' persistent lazy segment tree, vendored under
`bench/external/` with its MIT license, upstream commit, semantic audit and
the exact (instrumentation-only) modifications recorded in
[bench/external/PROVENANCE.md](external/PROVENANCE.md). It is copy-on-push
over per-node `shared_ptr` allocation, its queries materialize tags into
copied children (so its queries allocate — reported, not corrected), and it
passes the same cross-structure checksum as every in-house structure.

## Two binaries, on purpose

`valseg_bench` measures time against the untouched system allocator.
`valseg_bench_alloc` links a counting `operator new` and reports requested and
peak bytes. They are separate because the counting allocator adds a header to
every allocation, and a binary that perturbs the allocator has no business
reporting timings.

This matters more than it sounds. Every structure here stores nodes in a
`std::vector`, and a growing vector holds up to twice the bytes its elements
occupy, plus the old block until the copy completes. On the smoke run the
no-sharing baseline's documented payload is 2.26 MiB while its peak request is
4.71 MiB, a factor of 2.09. Reporting `nodeCount() * nodeBytes()` alone would
have understated it by that much, so both numbers are recorded and the summary
prints them side by side.

## Measurement protocol

- **Warm-up is a duration, not a count.** A freshly started process does not run
  at the speed it settles at. Measured here, the first recorded replays of a
  cell cost around 1.7x what the same replay costs a few trials later; the
  inflation lands on updates and on the build, never on queries, and scales with
  how much a structure allocates — the non-persistent control, which allocates
  nothing after its build, does not show it at all. It is the allocator and the
  kernel settling. It decays with elapsed load rather than with replay count, so
  discarding more trials does not reliably remove it. `--warmup-seconds` runs
  representative traffic through every structure until the clock says the
  machine has been busy long enough, and `--warmup` discards further per-cell
  trials on top of that.
- **Trials.** Five by default. Published numbers use `--trials 11 --warmup 3
  --warmup-seconds 15`.
- **Running order.** Trials are the outer loop and structures the inner one, and
  the structure order is reshuffled every trial. Whatever the machine was doing
  during a trial is spread across all structures instead of being paid by
  whichever ran first. `exec_order` records the position each measurement ran
  in, so an order effect can be tested for rather than assumed away.
- **Comparisons are paired.** Within one trial every structure replays the same
  stream from the same seed under the same machine state, so the trial index is
  a block. `bench/analysis` pairs on it, reports the median of the per-trial
  ratios rather than the ratio of medians, and tests with Wilcoxon signed-rank.
  One correction family is every cell-by-baseline comparison for one metric, so
  the campaign has two families, updates and queries. Within each family the
  reported significance flag is Benjamini-Hochberg at a 5% false-discovery
  rate; Holm-adjusted values are computed and kept alongside, and the docstring
  in `bench/analysis/data.py` says why Holm is not the deciding procedure at
  this trial count. The reported ratio bounds are observed 2.5/97.5 percentiles
  of the per-trial ratios, not confidence intervals.
- **Statistic.** Median over trials, with the observed range, the coefficient of
  variation and a 10,000-resample percentile bootstrap interval.
- **Outliers.** None are removed. The range and the interval make them visible.
- **Core placement.** The measuring thread requests the interactive
  quality-of-service class, which keeps it on performance cores. On an
  asymmetric processor an unpinned run compares core placements, not structures.
  On Linux, also fix the governor and pin explicitly:

  ```sh
  sudo cpupower frequency-set --governor performance
  mkdir -p bench/results/campaigns/2026-08-30-linux-a/raw
  taskset -c 2 ./build/release-verify/bench/valseg_bench --workload W1 \
    --out-dir bench/results/campaigns/2026-08-30-linux-a/raw --tag timing-W1
  ```
- **The timer is priced, not assumed.** `--batch-trials` replays a cell with two
  clock reads in total, so the cost of timing every operation individually is
  measured against an untimed replay of the same stream rather than argued
  about. On Apple silicon the timer resolves 41 ns, which is a large fraction of
  the cheapest operation in the campaign, so this is not a formality.
- **Two microarchitectures.** CI builds and smoke-runs on x86-64 Linux and Apple
  silicon macOS, but the published campaign comes from one machine. That is the
  first limitation to fix.

## Workloads

W1-W7 are the set frozen on issue #10. W8-W12 add the axes the frozen set
holds fixed, and which no amount of re-reading W1-W7 can recover: how cost
moves with the number of versions, and how it moves with skew.

| ID | Sweeps | What it separates |
|---|---|---|
| W1 | - | the general case, 50/50 |
| W2 | - | copy cost per published version, 90/10 |
| W3 | - | old versus recent reads, 10/90 |
| W4 | - | the exact per-update bound each header documents |
| W5 | - | full-range updates: full copy's home turf |
| W6 | zero-delta share | the O(1) shared-root fast path |
| W7 | checkpoint interval `K` | the update/query/space trade-off `K` controls |
| W8 | version count | 1e3 to 1e6 versions at fixed n: the axis partial persistence is about |
| W9 | Zipf theta | version reads by recency rank at theta = 0, 0.5, 0.99 |
| W10 | hot-window width | update locality: 80% of updates inside a window of this width |
| W11 | range width | the crossover between W4 and W5, which are its endpoints |
| W12 | share of history read | reads confined to the oldest 1%, 10%, 50% and 100% of versions |

W1-W5 run at n in {1e3, 1e4, 1e5, 1e6} over 2e5 operations. W6-W12 hold n at
1e4 and move one axis at a time.

W12 was added expecting it to be the case a checkpoint-and-replay structure has
to work hardest for. It is not, and the report says so: replay always starts at
the nearest checkpoint at or before the requested version, so its cost is
bounded by the checkpoint interval however old the version is. The workload
earns its place anyway — it is the audit access pattern, and it shows that
concentrating reads anywhere makes them cheaper for everyone.

W8 publishes all its updates first and then reads, so query cost is measured
against a known version count rather than averaged over a growing one.

W9's draw is exact Zipf over recency rank: rank 1 is the newest version, and
the generator inverts the prefix sums of `r^-theta` over the versions that
exist at that point in the stream. `theta = 0` is uniform, which makes W9 a
superset of W3's version policy rather than a different thing.

### WT: replayed traces

`--trace FILE` replays an operation stream this repository did not generate:

```
n,100000
u,0,15,7
q,120,0,15
```

`n` declares the array size; `u` is `left,right,delta` and `q` is
`left,right,version`. Everything else, including trials, warm-up, the memory
cap and the cross-structure checksum, works exactly as it does for a generated
workload. It is reported under the id `WT`. No public trace of this operation
model exists (the search outcome is recorded in
[bench/traces/README.md](traces/README.md)), so the registered external
workload is a distribution derived from published criteria:
`bench/traces/make_external_distribution.py` generates it deterministically
and `run_confirmatory.sh <id> trace` replays it.

## The copy-on-push baseline

`copy_on_push_segment_tree.hpp` lives here rather than in `include/valseg/`
because it is a measurement subject, not part of the library.

Six of the seven persistent structures differ from `PersistentLazySegmentTree`
in strategy, so a difference between them is a difference between strategies.
That is useful, and it says nothing about the specific decision this project
makes. Copy-on-push exists to say something about that decision. It is the same
structure in every respect that can be held fixed — the same 32-byte node, the
same convention that a node's stored sum already includes its own tag, the same
query, the same zero-delta short circuit — and differs in one place: when an
update descends past a node carrying a tag, it pushes the tag into the node's
children, which are published and immutable, so the push copies them. That is
what an implementer gets by taking a textbook lazy segment tree, whose update
pushes before descending, and adding path copying without revisiting the tag
invariant.

With one variable free, the difference between the two is attributable. A
dedicated deterministic/randomized unit suite checks historical answers and
the exact copy-on-push allocation event. Every campaign also replays it against
the same cross-structure checksum as the six library structures, and the CTest
smoke run does the same at n = 256 across all twelve workloads.

The implementation this repository did not write is the `external` row of
the `structures()` table; see "The external structure" below.

## Output

`runs_<tag>.csv` — one row per recorded trial:

```
workload,structure,n,axis,variant,k,seed,trial,exec_order,updates,queries,
build_ns,build_nodes,update_ns,query_ns,batch_ns,
update_p50,update_p90,update_p99,update_p999,update_max,
query_p50,query_p90,query_p99,query_p999,query_max,
nodes,bytes,alloc_peak_bytes,alloc_live_bytes,alloc_count,peak_rss_bytes,
clock_overhead_ns,clock_resolution_ns,checksum,status
```

The memory columns are four different units, kept apart on purpose
(`docs/research/cost-model.md`, section 1):

| Column | Unit | Meaning |
|---|---|---|
| `nodes`, `build_nodes` | logical records | what the structure retains, counted by its own `nodeCount()` |
| `bytes` | payload bytes | records times each header's documented record size |
| `alloc_live_bytes`, `alloc_peak_bytes`, `alloc_count` | allocator bytes | requested from `operator new` in the counting binary: includes `std::vector` capacity and the old block during a doubling |
| `peak_rss_bytes` | resident bytes | the operating system's high-water mark for the whole process; it only rises, so it is per trial only when one process runs one cell (the registered protocol) and otherwise reports the largest cell run so far. Zero where the platform does not report it. |

`structural_<tag>-W*.csv` — written by `--structural`, one row per
(workload, n, variant, seed) for the seeds a campaign records under the same
`--seed`, `--warmup` and `--trials`, so it joins the runs CSV on those columns:

```
workload,n,axis,variant,seed,k,stream_fingerprint,updates,nonzero_updates,queries,
sum_update_visits,sum_checkpoint_update_visits,sum_pushes,sum_intersecting,
sum_query_visits,sum_replay_entries,sum_query_version_distance,
full_coverage_updates,full_coverage_queries,latest_version_queries
```

These are machine-independent counts from the executable frontier definitions
in `include/valseg/frontier.hpp` (the records the tag-retaining subject appends,
the pushes copy-on-push pays, the nodes tagless path copying copies, the records
a query reads, checkpoint-boundary traversals, replay entries, version distance
and full-coverage events). Latest-version checkpoint queries replay zero log
entries. `stream_fingerprint` identifies the complete generated input for one
seed so equivalent cells cannot cross the model split. No structure is built
and nothing is timed, and `--structure` and `--trace` are ignored.

Per-operation latency is kept for every operation and reduced to quantiles after
the replay, so a cell reports a distribution rather than an average. `batch_ns`
is the same stream replayed with two clock reads in total, or `-1` on trials
that did not run one. `build_nodes` lets the analysis derive nodes stored per
update, which is a count rather than a timing and therefore does not depend on
the machine.

`memory.csv` — one row per memory sample, twenty per trial plus the last, so
allocated-node growth is a curve rather than a final number.

`system_<tag>.txt` — the machine: processor, core counts, cache sizes, memory,
operating system, allocator, power source and load average, recorded again
before every workload by `collect_environment.sh`.

`environment_<tag>.txt` — build type, compiler and version, optimiser flags,
measured timer resolution and overhead, core-placement request, pointer width,
seed,
trial and warm-up counts, cap, whether allocation counting was linked in, and
the exact command line.

Raw CSV stays under `results/raw/`. Everything `bench/analysis` generates goes
under `results/figures/`, `results/tables/` and `results/summary/`. Nothing
generated is edited by hand.

## Analysis

`bench/analysis` is a `uv` project: `data.py` loads and aggregates, `style.py`
holds the one definition of what each structure is called and what colour and
marker it wears, `tables.py` emits LaTeX, and `report.py` produces every figure
and table in one pass.

```sh
uv run --frozen --project bench/analysis bench/analysis/report.py
```

It writes vector PDFs and PNGs to `results/figures/`, booktabs fragments to
`results/tables/`, and `results/tables/facts.tex`, which defines a LaTeX macro
for every number the report quotes. `docs/benchmarking/benchmarking.tex` reads
those macros rather than containing literals, so no number in the document can
drift from the data it describes — re-running the campaign updates the prose.

The cost-model tools are specified in `docs/research/cost-model.md`.
`split.py` groups cells with identical generated streams before the salted
training/holdout assignment. `cost_model.py --stage fit` writes a canonical,
hashable model artifact from training rows; `--stage evaluate` scores that
fixed artifact without refitting. Synthetic fixtures exercise grouping,
rank-deficient fitting, non-positive-error treatment, missing inputs and
artifact stability.

One command regenerates the pilot structural counts into temporary storage,
without modifying the checksummed raw pilot, then runs both stages:

```sh
bench/rebuild_pilot_cost_model.sh
```

Its output is pilot-only model development, not the registered evaluation.

## What the numbers do and do not mean

- `checksum` is the running hash of every query answer. All seven persistent
  structures must produce the same checksum for the same cell; the runner
  exits non-zero if they do not. The harness checks itself, which is why a
  timing run is also a correctness run.
- `lazy` is a control, not a competitor. It keeps no history, so it answers
  every query against the latest state whatever version the stream names. Its
  checksum is expected to differ and is excluded from the cross-check; read
  its update column, and its query column only as a floor.
- `full-copy` and `point-only` reach the memory ceiling at sizes the others
  finish comfortably. Their per-operation times on a truncated run describe a
  shorter run, so the analysis keeps them in the memory, feasibility and
  range-width figures, where the ceiling is the result, and out of the
  throughput comparison, where a ratio against them would be a category error.
- `bytes` is the payload each header documents: nodes times the documented node
  size, and for `checkpointing` the tree copies at 16 bytes plus the log at 24.
  `alloc_peak_bytes` is what was actually requested. Neither is RSS;
  `peak_rss_bytes` is, with the process-level caveat above.
- `status = memory_cap` is a result. The analysis collects those points into
  `summary/feasibility.csv` and a figure: the largest (n, versions) each structure completed and
  where it stopped. A baseline that cannot reach the sizes the others reach has
  demonstrated something about persistence strategies, and the ceiling belongs
  in the results rather than in a missing row.
- `FatNodePersistentSegmentTree`'s `HISTORY_CAPACITY` is fixed at 3 and is not
  a parameter, so it gets no sweep of its own. `K` is tunable on the
  checkpointing baseline only, which is why W7 sweeps it there and runs every
  other structure once at the default.
- Outside W7 that default is `K = round(sqrt(n))`, the value that balances the
  `O(log n + n / K)` update against the `O(log n + K)` query. It comes from the
  bound rather than from a hand-picked constant, because `K` is the only tuning
  knob any structure in the comparison has and a hand-picked value invites the
  reading that it was chosen to lose. W7 sweeps it on the same traffic so the
  choice can be checked against where the measured optimum falls.
- Range draws use `mt19937_64` with modulo reduction rather than
  `std::uniform_int_distribution`, whose output is implementation-defined: a
  seed has to mean the same stream on GCC, libc++ and MSVC, or runs from
  different platforms cannot be compared.
