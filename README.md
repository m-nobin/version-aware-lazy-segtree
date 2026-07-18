# Version-Aware Lazy Segment Tree

[![CI](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml/badge.svg)](https://github.com/m-nobin/version-aware-lazy-segtree/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An Advanced Algorithms project exploring how range-add lazy propagation can be combined with partial persistence while preserving correct historical range-sum queries.

> **Current status:** the correctness and performance baselines are implemented and verified. The partially persistent lazy segment tree is the next development phase and is not yet part of the public API.

## Implemented components

| Component | Purpose | Update | Query | Persistence |
|---|---|---:|---:|---|
| `BruteForceArray` | Historical correctness oracle | `O(n)` | `O(n)` worst case | Complete array copy |
| `LazySegmentTree` | Non-persistent performance baseline | `O(log n)` | `O(log n)` | None |

Both components support zero-based, inclusive range-add and range-sum operations using `long long` values.

## Build and verify

Requirements:

- CMake 3.25 or newer
- A C++17 compiler
- Git and network access during the first configure for the pinned GoogleTest dependency

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

| Platform | Compiler | CI preset |
|---|---|---|
| Linux | GCC | `ci-linux-gcc` |
| Linux | Clang | `ci-linux-clang` |
| Windows | MSVC | `ci-windows-msvc` |
| macOS | Apple Clang | `ci-macos-clang` |

CI also enforces ClangFormat 18 and runs Linux Clang with AddressSanitizer and UndefinedBehaviorSanitizer. Each compiled job uploads verbose logs and JUnit XML.

## Repository layout

```text
include/valseg/             Public headers
src/                        Library implementations
tests/                      Deterministic GoogleTest suites
.github/workflows/ci.yml    Cross-platform CI
CMakeLists.txt              Build and tooling configuration
CMakePresets.json           Developer, release, analysis, and CI presets
```

## Documentation

Detailed design and research material lives in the [project Wiki](https://github.com/m-nobin/version-aware-lazy-segtree/wiki):

Actionable development work is tracked in the [GitHub Project](https://github.com/users/m-nobin/projects/1)
and the repository [issue tracker](https://github.com/m-nobin/version-aware-lazy-segtree/issues).

- [Getting Started](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Getting-Started)
- [Problem Definition](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Problem-Definition)
- [Architecture](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Architecture)
- [Public API](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Public-API)
- [Baseline Algorithms](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Baseline-Algorithms)
- [Planned Versioned Tree](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Planned-Versioned-Tree)
- [Proposal and progress documentation](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Proposal)
- [Literature Review](https://github.com/m-nobin/version-aware-lazy-segtree/wiki/Literature-Review)

Approved PDF deliverables will be added and linked in a later documentation commit.

## Roadmap

1. Finalize persistent-node and lazy-tag invariants.
2. Implement the partially persistent lazy segment tree.
3. Add randomized differential tests against `BruteForceArray`.
4. Benchmark update time, query time, and memory growth.
5. Publish the approved report, proposal, and presentation PDFs.

## Citation and license

Citation metadata is available in [CITATION.cff](CITATION.cff). The project is released under the [MIT License](LICENSE).
