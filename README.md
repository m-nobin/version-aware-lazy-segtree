# Version-Aware Lazy Segment Tree

[![CI](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml/badge.svg)](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An Advanced Algorithms project exploring how range-add lazy propagation can be combined with partial persistence while preserving correct historical range-sum queries.

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

Three headers support the analysis rather than the public API. The trees above stay
SumAdd-only.

- [policy.hpp](include/valseg/policy.hpp) — the aggregate/action model the strategies are
  analysed under (`SumAddPolicy`, `MinAddPolicy`, `AffineSumModPolicy`), with their algebraic
  laws and compile-time capability facts. Strategy-by-strategy audit:
  [capability-taxonomy.md](docs/research/capability-taxonomy.md).
- [policy_trees.hpp](include/valseg/policy_trees.hpp) — policy-generic instruments the boundary
  theorem is stated for. `RetainedTagPersistentTree<Policy>` is the subject and rejects at compile
  time any policy that does not declare `kInducedActionsCommute`;
  `CopyOnPushPersistentTree<Policy>` is the ablation. Their SumAdd instantiations match the
  measured structures in arena size and in every probed answer.
- [frontier.hpp](include/valseg/frontier.hpp) — executable frontier definitions (`F`, its closed
  form, the intersecting-node count and the push frontier `P`) that
  [docs/proof.md](docs/proof.md) section 10 states the record identities with.

**Contract.** Ranges are zero-based and inclusive over `long long` values. Persistent updates
apply to the latest version only; queries read any published version. A failed write throws and
leaves the prior state and the published history unchanged.

**Numeric domain.** Exact signed-integer arithmetic in which every intermediate, segment sum and
retained lazy tag is representable as `long long`. Anything outside it throws
`std::overflow_error`. Lazy-tag implementations decide this with a conservative magnitude
envelope; near the `long long` boundary, where the envelope is inconclusive, they fall back to an
`O(n)` read-only exact preflight before the update. The fallback changes neither what is accepted
nor the benchmarked node layouts.

The table's update bounds describe the structural algorithm and the constant-time numeric fast
path; persistent update time is amortized over arena growth. Both are proved in
[docs/proof.md](docs/proof.md).

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
docs/research/              Prior-art audit, capability model, cost model and registered protocol
docs/benchmarking/          Exploratory-pilot report source
```

## Benchmarks

`bench/` replays twelve workloads against every persistence strategy plus one
vendored external implementation; see [bench/README.md](bench/README.md).

The recorded campaign is an **exploratory pilot**: one machine, one compiler, no
registered protocol. `bench/verify_pilot.sh` checks the data manifest and rebuilds
the report. The confirmatory campaign runs instead under the protocol registered in
[docs/research/](docs/research/README.md), with fresh-process orchestration, blinded
statistics and a single locked decision pipeline.

Sources, analysis code and the raw checksum manifest are versioned; measured data,
tables, figures and PDFs are not.

## Documentation

Design, algorithms and public API are documented in the
[project Wiki](https://github.com/m-nobin/version-aware-lazy-segtree/wiki). The correctness proof,
complexity analysis and the action-order boundary theorem are in [docs/proof.md](docs/proof.md).
The prior-art audit, capability model, cost model and registered analysis protocol are in
[docs/research/](docs/research/README.md).

## Citation and license

Citation metadata is available in [CITATION.cff](CITATION.cff). The project is released under the [MIT License](LICENSE).
