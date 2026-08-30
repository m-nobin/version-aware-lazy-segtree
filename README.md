# Version-Aware Lazy Segment Tree

[![CI](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml/badge.svg)](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An Advanced Algorithms project exploring how range-add lazy propagation can be combined with partial persistence while preserving correct historical range-sum queries.

> **Current status**
> Route B Phase 1 (evidence and model lock) is complete: both pull requests are merged and the
> independent reader's sign-off is recorded in the claim-evidence matrix. Phase 2 (theorems and
> the predictive model) is under way: the observational commutativity boundary
> ([docs/proof.md](docs/proof.md) section 9) and the frontier identities (section 10) are
> independently reviewed; the lower-bound attempt of section 10 found a counterexample, so no
> optimality claim is made and the plan's fallback title is selected at Gate G2. The physical
> and predictive cost model is specified in
> [docs/research/cost-model.md](docs/research/cost-model.md). The
> Gate G1 record is section 9 of the
> [claim-evidence matrix](docs/research/claim-evidence-matrix.md); phase charters and exit
> reviews are working documents kept outside the repository.

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

All components support zero-based, inclusive range-add and range-sum operations using `long long` values. Persistent updates apply to the latest version only, while queries read any published version. Persistent update time is amortized over arena growth; the bounds are proved in [docs/proof.md](docs/proof.md).

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

## Verification matrix

| Platform | Compiler    | CI preset         |
| -------- | ----------- | ----------------- |
| Linux    | GCC         | `ci-linux-gcc`    |
| Linux    | Clang       | `ci-linux-clang`  |
| Windows  | MSVC        | `ci-windows-msvc` |
| macOS    | Apple Clang | `ci-macos-clang`  |

CI also enforces ClangFormat 18 and runs Linux Clang with AddressSanitizer and UndefinedBehaviorSanitizer. Each compiled job uploads verbose logs and JUnit XML.

## Repository layout

```text
include/valseg/             Public headers
src/                        Library implementations
tests/                      Deterministic and randomized differential GoogleTest suites
tests/compile_fail/         Sources that must fail to compile, run as CTest cases
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
strategy; see [bench/README.md](bench/README.md). Measured campaign data, the
generated tables, figures and PDFs are kept out of version control; the
analysis and report sources, raw checksum manifest and provenance record are
versioned. The recorded campaign is an **exploratory pilot** (one machine, no
registered protocol). With the preserved local data present,
`bench/verify_pilot.sh` verifies checksums and rebuilds the complete report.

## Documentation

Detailed design and research documentation is maintained in the
[project Wiki](https://github.com/m-nobin/version-aware-lazy-segtree/wiki), while actionable work is
organized through the [GitHub Project](https://github.com/users/m-nobin/projects/1) and repository
[issue tracker](https://github.com/m-nobin/version-aware-lazy-segtree/issues).

## Citation and license

Citation metadata is available in [CITATION.cff](CITATION.cff). The project is released under the [MIT License](LICENSE).
