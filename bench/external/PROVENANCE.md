# External implementation provenance

The registered protocol (`docs/research/registered-protocol.md`, section 8)
requires one range-add/range-sum persistent implementation this repository's
authors did not write. This directory vendors it.

## Source

| Field | Value |
| --- | --- |
| Repository | `TheAlgorithms/C-Plus-Plus` |
| File | `range_queries/persistent_seg_tree_lazy_prop.cpp` |
| Author | Magdy Sedra (`MSedra`) |
| Upstream commit last touching the file | `79aeaa9b5278542d49c83368003887e46a9cf7d5` (2025-08-15) |
| Fetched | 31 August 2026, from `raw.githubusercontent.com`, `master` |
| License | MIT (`THEALGORITHMS_LICENSE`, copied from the repository root) |

`thealgorithms_persistent_seg_tree_lazy_prop.cpp` is the pristine vendored
copy and is never compiled or edited. `thealgorithms_persistent_lazy.hpp` is
the compiled form.

## Exact modifications in the compiled header

1. The class and its namespace were extracted into an include-guarded header;
   the `test()` and `main()` functions and the iostream include were removed.
2. A `uint64_t created` counter and a `nodesCreated()` accessor were added,
   with `++created` at the three `std::make_shared<Node>` sites. The adapter
   needs a record count for the memory cap and the CSV; nothing upstream
   exposes one.
3. Nothing else. Algorithm, node layout, recursion structure, `shared_ptr`
   representation, and 0-indexed version convention are unmodified.

Both files diff cleanly: every difference is one of the three items above.

## Semantic audit

- Range add over `[l, r]`, range sum, `int64_t` values, one new root per
  update, version `v` = state after `v` updates. This matches the harness
  operation model exactly, so the adapter passes the same cross-structure
  checksum as the in-house structures.
- Strategy: copy-on-push. A fully covered node is copied and its tag is
  immediately materialized (`lazy()` after `newKid()`), copying its children.
- Queries are not read-only: `lazy()` inside `query()` materializes pending
  tags into copied children of shared published nodes. The rewrite preserves
  every version's answers (a shared subtree represents the same values for
  every root that shares it), and the cross-structure checksum checks that,
  but query cost includes allocation and query replays are not idempotent in
  allocation count. The analysis must not treat external query trials as
  allocation-free.
- Representation: `std::shared_ptr` nodes allocated per node with
  `make_shared` (no arena). `sizeof(Node)` is 48; `make_shared` co-allocates
  a control block, so the adapter's payload figure uses 48 bytes per created
  node and is an undercount of allocator bytes on purpose. Allocator and RSS
  figures come from `valseg_bench_alloc` and the OS, as for every structure.
- Version handles are never dropped, so `shared_ptr` reference cycles are not
  a concern (the graph is a DAG and roots are retained for the whole trial).

## Fairness limits

The adapter gives the external structure the same stream, the same seeds,
the same cap and the same build flags as every in-house structure. No tuning
was applied to either side. The representation differences (per-node
heap allocation, reference counting, allocating queries) are properties of
the external implementation and are reported as such, not corrected away.
