# Version-Aware Lazy Segment Tree

[![CI](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml/badge.svg)](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An Advanced Algorithms project exploring how range-add lazy propagation can be combined with partial persistence while preserving correct historical range-sum queries.

> **Current status**
> Route B Phase 1's implementation and evidence package is complete and was re-audited on
> 1 September 2026. Gate G1 is reconfirmed for the scoped Route B continuation; the durable
> approval is preserved in the [Phase 1 human review record](docs/research/phase-1-human-review.md).
> The authoritative [Phase 1 charter](docs/research/phase-1-charter.md), corrected
> [exit review](docs/research/phase-1-exit-review.md) and
> [claim-evidence matrix](docs/research/claim-evidence-matrix.md) record the technical closure
> and the reconfirmed Gate G1 decision. Phase 2 (theorems, frontier identities and the cost model)
> passed Gate G2 under the public automated-review governance amendment: the
> observational commutativity boundary ([docs/proof.md](docs/proof.md) section 9) and the
> frontier identities (section 10) are merged and independently audited by an automated reviewer
> who authored neither change; the lower-bound
> attempt of section 10 found a counterexample, so no optimality claim is made and the adopted
> paper title is:
>
> **Partial Persistence Strategies for In-Memory Segment Trees under Additive Range Updates:
> Structural Space Characterizations and a Controlled Space–Time Study**
>
> The physical and predictive cost model is specified in
> [docs/research/cost-model.md](docs/research/cost-model.md), including stream-group holdout
> separation and a hashable three-stage prepare/fit/evaluate path. The
> Gate G1 evidence assessment is section 9 of the claim-evidence matrix. Phase charters and exit
> reviews are version-controlled records in `docs/research/`.
>
> Phase 3 (registered confirmatory evaluation) is being hardened: the prospective analysis
> protocol is written in
> [docs/research/registered-protocol.md](docs/research/registered-protocol.md) with its
> statistics, fail-closed H3/H4/H5 decisions, custody-separated blinding, sensitivity paths and
> an external implementation in place, and the statistical machinery approved by an independent
> review ([docs/research/statistical-review.md](docs/research/statistical-review.md)). The
> The fresh-process harness is hardened and its excluded-seed dry run passed on macOS
> ([bench/README.md](bench/README.md)); the Linux x86-64 dry run on dedicated hardware
> ([bench/env/README.md](bench/env/README.md)), the registration (a tagged, pushed commit with
> its manifest) and the one-shot campaign
> ([docs/research/confirmatory-campaign.md](docs/research/confirmatory-campaign.md)) remain
> open. No confirmatory measurement runs before that registration; the protocol and data are
> published with the paper's arXiv submission.

## Implemented components

| Component                        | Purpose                                                        |                        Update |             Query | Persistence                                          |
| -------------------------------- | -------------------------------------------------------------- | ----------------------------: | ----------------: | ---------------------------------------------------- |
| `BruteForceArray`                | Historical correctness oracle                                  |                        `O(n)` | `O(n)` worst case | Complete array copy                                  |
| `LazySegmentTree`                | Non-persistent performance baseline                            |                    `O(log n)` |        `O(log n)` | None                                                 |
| `PersistentLazySegmentTree`      | Partially persistent range-add/range-sum tree                  |                    `O(log n)` |        `O(log n)` | Path copying with structural sharing                 |
| `FullCopyPersistentSegmentTree`  | Benchmark baseline: full tree copy per update                  |                        `O(n)` |        `O(log n)` | Complete tree copy, no sharing                       |
| `PointOnlyPersistentSegmentTree` | Benchmark baseline: path copying without lazy tags             | `Θ(k + log n)` for `k` leaves |        `O(log n)` | Path copying to every updated leaf                   |
| `CheckpointingSegmentTree`       | Benchmark baseline: update log + checkpoint every `K` versions |  `O(log n + n / K)` amortized |    `O(log n + K)` | Full-tree checkpoints + log replay, no sharing       |
| `BufferedPathCopyingSegmentTree` | Benchmark baseline: path copying with 2-slot node buffer       |                    `O(log n)` |        `O(log n)` | Version-tagged in-node buffer, path copy on overflow |
| `FatNodePersistentSegmentTree`   | Benchmark baseline: fat nodes with node copying                |                    `O(log n)` |        `O(log n)` | In-place versioned states, copy on overflow          |
| `CopyOnPushSegmentTree`          | Measurement subject under `bench/`: path copying that pushes tags |                 `O(log n)` |        `O(log n)` | Path copying, tag pushed into copied children        |

[include/valseg/policy.hpp](include/valseg/policy.hpp) defines the aggregate/action policy model
these strategies are analysed under: `SumAddPolicy`, `MinAddPolicy` and `AffineSumModPolicy` with
their algebraic laws and compile-time capability facts; the strategy-by-strategy audit is in
[docs/research/capability-taxonomy.md](docs/research/capability-taxonomy.md).
[include/valseg/policy_trees.hpp](include/valseg/policy_trees.hpp) holds the policy-generic
research instruments the boundary theorem is stated for: `RetainedTagPersistentTree<Policy>` (the
subject; refuses at compile time a policy that does not declare `kInducedActionsCommute`),
`CopyOnPushPersistentTree<Policy>` (the ablation), `PointMaterializedPersistentTree<Policy>` and
`PushedLazyTree<Policy>`. Their SumAdd instantiations match `PersistentLazySegmentTree` and
`CopyOnPushSegmentTree` in arena size after every update and in every probed answer on the
tested histories. [include/valseg/frontier.hpp](include/valseg/frontier.hpp) holds the executable
frontier definitions (`F`, the closed form, the intersecting-node count, the push frontier `P`
and exact range-family sums) that [docs/proof.md](docs/proof.md) section 10 states record
identities with. The existing structures remain SumAdd-only; the
policies and templates feed the research programme, not a new public API for the trees.

All components support zero-based, inclusive range-add and range-sum operations using `long long`
values. Their numeric domain is exact signed-integer arithmetic in which every evaluated
intermediate, canonical segment sum and retained lazy tag is representable as `long long`.
Out-of-domain initialization, update, or query operations throw `std::overflow_error`; failed
writes leave the prior state and published history unchanged. Persistent updates apply to the
latest version only, while queries read any published version. Persistent update time is amortized
over arena growth; the bounds are proved in [docs/proof.md](docs/proof.md). The update bounds in
the table describe the structural algorithm and the constant-time numeric fast path. Lazy-tag
implementations use a conservative magnitude envelope to prove the full logical array remains in
domain; when that proof is inconclusive near `long long` boundaries, they run an `O(n)` read-only
exact preflight before the normal update. This fallback preserves exact acceptance and rejection
without enlarging the benchmarked node layouts.

## Build and verify

Requirements:

- CMake 3.25 or newer
- A C++17 compiler
- Git and network access during the first configure for the pinned GoogleTest dependency

Enable the repository pre-push hook once per clone; it runs the same ClangFormat 18 check as CI
before every push:

```bash
git config core.hooksPath .githooks
```

Configure, build, and run the verbose Debug suite:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset verify
```

The verification preset prints every test and writes:

```text
build/dev/test_output.log
build/dev/test_results.xml
```

Strict Debug and optimized Release verification:

```bash
cmake --preset dev-werror
cmake --build --preset dev-werror
ctest --test-dir build/dev-werror -C Debug --output-on-failure

cmake --preset release-verify
cmake --build --preset release-verify
ctest --preset release-verify
```

Optional Clang-Tidy analysis, with `clang++` and `clang-tidy` on `PATH`:

```bash
cmake --preset analyze
cmake --build --preset analyze
ctest --preset analyze
```

Install the library and use it from another project:

```bash
cmake --preset release -DVALSEG_BUILD_BENCH=OFF
cmake --build --preset release
cmake --install build/release --prefix /path/to/prefix
```

```cmake
find_package(valseg 0.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE valseg::valseg)
```

`add_subdirectory` exposes the same `valseg::valseg` target. CTest, the tests and the benchmark
runner exist only when this repository is the top-level project; a parent project can set
`VALSEG_BUILD_BENCH=ON` to opt in to the runner. Both consumer paths are built and run by the
`consumer_find_package` and `consumer_add_subdirectory` CTest cases.

## Verification matrix

| Platform | Compiler    | CI preset         |
| -------- | ----------- | ----------------- |
| Linux    | GCC         | `ci-linux-gcc`    |
| Linux    | Clang       | `ci-linux-clang`  |
| Windows  | MSVC        | `ci-windows-msvc` |
| macOS    | Apple Clang | `ci-macos-clang`  |

CI also enforces ClangFormat 18 on `include/`, `src/`, `tests/` and the benchmark sources under `bench/` (the vendored `bench/external/` is excluded), and runs Linux Clang with AddressSanitizer and UndefinedBehaviorSanitizer. Each compiled job uploads verbose logs and JUnit XML.

## Repository layout

```text
include/valseg/             Public headers
include/valseg/detail/      Shared checked arithmetic, numeric-domain and validation helpers
src/                        Library implementations
tests/                      Deterministic and randomized differential GoogleTest suites
tests/compile_fail/         Sources that must fail to compile, run as CTest cases
tests/consumer/             Downstream project built by CTest through find_package and add_subdirectory
bench/                      Benchmark harness and workloads
bench/analysis/             Locked pilot analysis and figure/table generation
.github/workflows/ci.yml    Cross-platform CI
CMakeLists.txt              Build and tooling configuration
CMakePresets.json           Developer, release, analysis, and CI presets
docs/proof.md               Correctness proof, complexity analysis and the action-order boundary theorem
docs/research/              Claim-evidence matrix, capability model and cost model
paper/                      Version-controlled manuscript source
```

## Benchmarks

`bench/` replays twelve workloads against every implemented persistence
strategy and one vendored external implementation
([bench/external/PROVENANCE.md](bench/external/PROVENANCE.md)); see
[bench/README.md](bench/README.md). Measured campaign data, the
generated tables, figures and PDFs are kept out of version control; the
analysis and report sources, raw checksum manifest and provenance record are
versioned. The recorded campaign is an **exploratory pilot** (one machine, no
registered protocol). With the preserved local data present,
`bench/verify_pilot.sh` verifies checksums and rebuilds the complete report.
The confirmatory campaign runs under the registered protocol instead:
batch-mode primary timing, fresh-process orchestration
(`bench/run_confirmatory.sh`), blinded confirmatory statistics
(`bench/analysis/confirm.py`), one locked decision pipeline
(`bench/run_registered_analysis.sh`) and second-compiler/second-allocator sensitivity paths.

## Documentation

Detailed design and research documentation is maintained in the
[project Wiki](https://github.com/m-nobin/version-aware-lazy-segtree/wiki), while actionable work is
organized through the [GitHub Project](https://github.com/users/m-nobin/projects/1) and repository
[issue tracker](https://github.com/m-nobin/version-aware-lazy-segtree/issues).

## Citation and license

Citation metadata is available in [CITATION.cff](CITATION.cff). The project is released under the [MIT License](LICENSE).
