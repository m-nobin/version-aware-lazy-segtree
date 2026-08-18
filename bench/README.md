# Benchmark harness

Replays one operation stream per workload against every structure and writes
raw CSV. The stream is generated from the workload alone, so every structure
sees exactly the same operations in the same order.

## Run it

```sh
cmake --preset release-verify
cmake --build build/release-verify --target valseg_bench valseg_bench_alloc
./build/release-verify/bench/valseg_bench --out-dir bench/results/raw
./build/release-verify/bench/valseg_bench_alloc --out-dir bench/results/raw --tag alloc
python3 bench/summarize.py
python3 bench/summarize.py --tag alloc --summary bench/results/summary/alloc
```

Measure on the release build. A Debug build measures the debug allocator.

| Flag | Default | Meaning |
|---|---|---|
| `--workload` | `all` | one of `W1` .. `W11` |
| `--structure` | `all` | `lazy`, `persistent`, `full-copy`, `point-only`, `checkpointing`, `buffered`, `fat-node` |
| `--out-dir` | `bench/results/raw` | must already exist |
| `--tag` | none | suffix for the output filenames, so runs do not overwrite each other |
| `--seed` | `20260818` | attempt *i* uses `seed + i` |
| `--trials` | `5` | recorded trials per cell |
| `--warmup` | `1` | trials run and discarded before recording |
| `--cap-mib` | `4096` | a trial stops when retained payload passes this |
| `--trace` | none | replay an operation trace as `W12`; see below |
| `--smoke` | off | n = 256, 400 operations, for CI |
| `--list` | off | print the workload table and exit |

A full campaign at the committed sizes takes hours, most of it in the
no-sharing baselines before they reach their ceiling. Run one workload at a
time with `--workload` while iterating.

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

- **Warm-up.** One trial per cell runs and is discarded. Recorded trials use
  distinct seeds, so a cell is not one run repeated.
- **Trials.** Five by default. Published numbers use `--trials 11`.
- **Statistic.** Median, with the min-max range printed beside it. With trial
  counts this small a mean and a confidence interval would claim more than the
  data supports; the range shows the spread without pretending to a
  distribution.
- **Outliers.** None are removed. The range makes them visible instead.
- **Pinning and frequency scaling.** The runner does not set them, because it
  cannot do so portably. On Linux, pin and fix the governor before a published
  run:

  ```sh
  sudo cpupower frequency-set --governor performance
  taskset -c 2 ./build/release-verify/bench/valseg_bench --out-dir bench/results/raw
  ```

  macOS offers no equivalent; record macOS numbers as a second
  microarchitecture, not as the primary result.
- **Two microarchitectures.** CI already builds and smoke-runs on x86-64 Linux
  and Apple Silicon macOS. Published runs come from one machine of each, and
  `environment.txt` records which.

## Workloads

W1-W7 are the set frozen on issue #10. W8-W11 add the axes the frozen set
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

W1-W5 run at n in {1e3, 1e4, 1e5, 1e6} over 2e5 operations. W6-W11 hold n at
1e4 and move one axis at a time.

W8 publishes all its updates first and then reads, so query cost is measured
against a known version count rather than averaged over a growing one.

W9's draw is exact Zipf over recency rank: rank 1 is the newest version, and
the generator inverts the prefix sums of `r^-theta` over the versions that
exist at that point in the stream. `theta = 0` is uniform, which makes W9 a
superset of W3's version policy rather than a different thing.

### W12: replayed traces

`--trace FILE` replays an operation stream this repository did not generate:

```
n,100000
u,0,15,7
q,120,0,15
```

`n` declares the array size; `u` is `left,right,delta` and `q` is
`left,right,version`. Everything else, including trials, warm-up, the memory
cap and the cross-structure checksum, works exactly as it does for a generated
workload. No trace is committed yet. The seam exists so a workload derived from
a published temporal-data study can be added without touching the runner,
which is the difference between a comparison built only on data we invented
and one that also replays traffic somebody else published.

### W13: an implementation we did not write

Every structure measured here was written for this repository, which invites
the reasonable objection that the baselines were built to lose. Adding an
external implementation is a new entry in `adapters.hpp` and one row in the
`structures()` table in `bench_main.cpp`, and no other change. The fairest
candidate is the competitive-programming reference the work is measured
against, since that folklore implementation is the thing a reader will
otherwise assume is faster.

## Output

`runs.csv` — one row per recorded trial:

```
workload,structure,n,axis,variant,seed,trial,updates,queries,
build_ns,update_ns,query_ns,nodes,bytes,
alloc_peak_bytes,alloc_live_bytes,alloc_count,checksum,status
```

`memory.csv` — one row per memory sample, twenty per trial plus the last, so
allocated-node growth is a curve rather than a final number.

`environment.txt` — build type, compiler and version, pointer width, seed,
trial and warm-up counts, cap, whether allocation counting was linked in, and
the exact command line.

Raw CSV stays under `results/raw/`; everything `summarize.py` generates goes
under `results/summary/`, including `feasibility.md`. Nothing generated is
edited by hand.

## What the numbers do and do not mean

- `checksum` is the running hash of every query answer. All six persistent
  structures must produce the same checksum for the same cell; the runner
  exits non-zero if they do not. The harness checks itself, which is why a
  timing run is also a correctness run.
- `lazy` is a control, not a competitor. It keeps no history, so it answers
  every query against the latest state whatever version the stream names. Its
  checksum is expected to differ and is excluded from the cross-check; read
  its update column, and its query column only as a floor.
- `bytes` is the payload each header documents: nodes times the documented node
  size, and for `checkpointing` the tree copies at 16 bytes plus the log at 24.
  `alloc_peak_bytes` is what was actually requested. Neither is RSS.
- `status = memory_cap` is a result. `summarize.py` collects those points into
  `feasibility.md`: the largest (n, versions) each structure completed and
  where it stopped. A baseline that cannot reach the sizes the others reach has
  demonstrated something about persistence strategies, and the ceiling belongs
  in the results rather than in a missing row.
- `FatNodePersistentSegmentTree`'s `HISTORY_CAPACITY` is fixed at 3 and is not
  a parameter, so it gets no sweep of its own. `K` is tunable on the
  checkpointing baseline only, which is why W7 sweeps it there and runs every
  other structure once at the default.
- Range draws use `mt19937_64` with modulo reduction rather than
  `std::uniform_int_distribution`, whose output is implementation-defined: a
  seed has to mean the same stream on GCC, libc++ and MSVC, or runs from
  different platforms cannot be compared.
