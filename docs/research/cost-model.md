# Physical and predictive cost model

This document fixes the units the benchmark reports, the byte equations of
every record layout, the candidate variables of the time model, the
training/hold-out split and the reclamation model. It is the specification
the registered protocol (PR6) freezes from; nothing here is a confirmatory
result. Where a number comes from the exploratory pilot it says so.

## 1. Five units, never added together

| Unit | Where it is measured | What it counts |
| --- | --- | --- |
| Logical records | `nodes`, `build_nodes` in `runs_*.csv`; the identities of `docs/proof.md` section 10 | what a strategy retains, in its own record type: tree records, log entries, checkpoints, in-node states or buffered slots, each reported by the structure's `nodeCount()` under its documented convention |
| Payload bytes | `bytes` in `runs_*.csv`, `bench/adapters.hpp` | records times the documented record size of section 2 |
| Container capacity and allocator bytes | `alloc_live_bytes`, `alloc_peak_bytes`, `alloc_count` from `valseg_bench_alloc` | what `operator new` was asked for, so `std::vector` capacity, the old block during a doubling and the counting header are included; recorded in the allocation binary only, never beside a timing |
| Resident set | `peak_rss_bytes` in `runs_*.csv` | the operating system's high-water mark for the process; per trial only under the fresh-process orchestration of the registered protocol |
| Temporary bytes | not yet measured | scratch used by reconstruction or analysis; the checkpoint baseline's query replays a log without a scratch tree for SumAdd, so it has none, and a generic reconstruction variant (not implemented) would report `O(n)` here |

Ratios are formed within one unit. "Memory amplification" in the registered
estimand E2 is three separate ratios: payload, allocator peak and RSS.

## 2. Record layouts

Let `I` be the index width (`sizeof(std::size_t)`), `V` the version width
(also `std::size_t` in every implementation), `S` the aggregate width and
`A` the action width (`sizeof(long long)` for SumAdd), all 8 bytes on the
targets CI builds. Every record below is a struct of 8-byte fields, so there
is no padding and alignment is 8, except the ordinary lazy trees, which keep
two parallel arrays rather than a record. Every persistent header, and the
bench copy-on-push header, carries a `static_assert` on its total.

| Structure | Record | Bytes | SumAdd, 64-bit |
| --- | --- | --- | --- |
| `PersistentLazySegmentTree`, `CopyOnPushSegmentTree`, `RetainedTagPersistentTree<P>`, `CopyOnPushPersistentTree<P>` | two child indices, aggregate, tag | `2I + S + A` | 32 |
| `FullCopyPersistentSegmentTree`, `PointOnlyPersistentSegmentTree`, `PointMaterializedPersistentTree<P>` | two child indices, aggregate | `2I + S` | 24 |
| `LazySegmentTree`, `PushedLazyTree<P>` | two parallel arrays of `4n` positions, one aggregate and one tag per position; no struct, no assert | `S + A` per position | 16 |
| `CheckpointingSegmentTree` | ephemeral node: aggregate, tag; log entry: range, delta | node `S + A`; entry `2I + A` | 16; 24 |
| `BufferedPathCopyingSegmentTree` | two child indices, base aggregate, base tag, fill count, two entries of (version, delta aggregate, delta tag) | `2I + S + A + I + 2(V + S + A)` | 88 |
| `FatNodePersistentSegmentTree` | state count and three states of (version, two child indices, aggregate, tag) | `I + 3(V + 2I + S + A)` | 128 |
| Edge-tag counterexample (`docs/proof.md` 10.6, test-only) | two child indices, aggregate, two edge tags | `2I + S + 2A` | 40 |

For a policy whose action is not one word, `A` grows and the tagged layouts
grow with it: `AffineSumModPolicy` has `A = 16`, so its retained-tag and
copy-on-push records are 40 bytes. `tests/record_layout_test.cpp` checks the
equations against `sizeof` and `alignof` of the generic templates for SumAdd,
MinAdd and AffineSum, and every SumAdd header's `static_assert` pins its own
total.

Retained payload after a history follows from section 10 of the proof: the
subject holds `(2n − 1 + Σ F_i)` records of `2I + S + A` bytes, copy-on-push
`(2n − 1 + Σ(F_i + 2P_i))` of the same size, point-only `(2n − 1 + Σ N_i)`
of `2I + S`. Bytes and records can rank strategies differently: the edge-tag
representation allocates fewer records than the subject on every non-full
update and fewer bytes only when `|D| / |Partial| > A / (2I + S + A)`.

## 3. Candidate time-model variables

The frozen pilot-informed mechanistic form is

```text
log(predicted_ns_per_op) = alpha_machine
                         + beta_machine   * visited_records
                         + gamma_machine  * allocated_records
                         + theta_machine  * working_set_transition
                         + lambda_machine * version_distance_transition
                         + phi_machine    * full_coverage_share
```

fitted per structure and per operation type (update, query), with the
coefficients machine-specific and predictions returned through `exp`. The
log response guarantees positive predictions and was selected from the
exploratory pilot before registration. `bytes_touched` is not separately
estimable within one structure because it is a fixed multiple of the record
counts. For the tag-retaining subject, `allocated_records` equals
`visited_records` exactly (Proposition 10.5). Constant or exactly collinear
columns are therefore removed deterministically before fitting; no
interaction or regularization term is selected. This response transform,
candidate set and column-removal rule are frozen for PR6. A later change
requires a written protocol amendment made before confirmatory measurement.

| Variable | Updates | Queries | Source | Available for |
| --- | --- | --- | --- | --- |
| `visited_records` | records update recursion reads, per update: `Σ F / updates` for tree strategies; checkpointing adds a second `F` on each nonzero checkpoint-boundary update because it updates both the live and newly copied tree; `Σ N / updates` for point-only; `(2n − 1) · nonzero / updates` for full copy | records a query reads, `Σ F / queries`; checkpointing adds replayed log entries, zero for the latest version and otherwise `v mod K` (or `v` for unbounded `K`) | `structural_*.csv`, machine-independent | every structure; checkpoint work is strategy-specific |
| `allocated_records` | records stored per update, `(nodes − build_nodes) / updates` | 0 | `runs_*.csv`, machine-independent | every structure; for the subject, copy-on-push and point-only it is the identity of section 10 summed over the stream, so it is also predictable a priori |
| `working_set_transition` | `log2(max(1, retained payload bytes / cache bytes))` | same | `bytes` in `runs_*.csv`; the cache size is `l2_bytes` from the campaign's `system_*.txt` | every structure |
| `version_distance_transition` | 0 | `log2(1 + mean(latest version at query − requested version))` | `sum_query_version_distance` in `structural_*.csv` | every structure; the non-persistent control keeps the variable so its expected irrelevance is testable rather than assumed |
| `full_coverage_share` | nonzero updates with `[l,r] = [0,n−1]`, divided by all updates | queries with `[l,r] = [0,n−1]`, divided by all queries | `full_coverage_updates`, `full_coverage_queries` in `structural_*.csv` | every structure |

Every per-update predictor is divided by the number of updates including
zero-delta ones, the denominator of the response, so a zero-delta share
scales both sides alike.

Hardware counters are not a candidate: they are not collectable on every
target machine. The pilot's `working_set_transition` uses the captured
`l2_bytes = 4194304` of the Apple M4 development machine (its capture
reports no L3); the registered protocol takes the figure for each machine
from its environment capture.

The per-operation structural counts can be computed for any stream before it
is timed, so the model can be evaluated on a cell without reading its
timing. That is what makes the hold-out evaluation possible.

## 4. Training and hold-out split

`valseg_bench --structural` writes a stable fingerprint of the complete
generated input for every seed. `bench/analysis/split.py` hashes the sorted
seed/fingerprint inventory of each `(workload, n, axis, variant)` cell, then
assigns the resulting stream-equivalence group to `training` or `holdout`.
Every seed and trial of a cell stays together, and cells with byte-identical
generated inputs stay together. The known duplicate — W4 at `n = 10000` and
W11 at width 1 — is exercised by a synthetic fixture and was also verified
against the real generator. The earlier cell-key leak is closed.

The pilot salt and 30% share remain development values. PR6 registers a new
salt and share before the confirmatory campaign. The data path has three
explicit stages:

1. `--stage prepare` loads predictor-only structural rows, fixes every
   stream-group membership, and only then loads timing responses. It writes
   separate `cost_model_training_responses.csv` and
   `cost_model_holdout_responses.csv` files plus a checksum manifest.
2. `--stage fit` receives that manifest, verifies and deserializes only the
   training response file, fits/selects the registered form, and writes both a
   canonical JSON artifact and its `.sha256` sidecar. It has no code path that
   resolves or opens the holdout response file.
3. `--stage evaluate` verifies the artifact and sidecar first, checks their
   training-input provenance against the partition manifest, and only then
   verifies and opens the holdout response file. It never refits.

The synthetic integration fixture makes the holdout file deliberately invalid,
installs a guard that fails on any attempt to open it, and proves the actual fit
entry point still succeeds. A second guard proves evaluation rejects a changed
artifact hash before opening holdout. These tests exercise the process boundary
rather than merely showing that changed holdout values leave coefficients
unchanged. Campaign custody and the one-shot evaluation record remain Phase 3
orchestration responsibilities.

## 5. Pilot-only model development

`bench/rebuild_pilot_cost_model.sh` regenerates the section 3 structural
counts into a temporary directory without modifying the checksummed raw
pilot, runs the synthetic analysis fixtures, prepares the disjoint response
files, fits the training partition, fixes and hashes the JSON model artifact,
and evaluates that artifact on the pilot holdout. It opts into the
`cost_model_pilot` output stem and exploratory label; the analysis command's
defaults are confirmatory-neutral. It reports median and 90th-percentile
absolute percentage error and the median absolute log ratio, with residuals by
workload.

Its output is exploratory: one machine, one compiler and the per-operation
clock pair of the pilot harness rather than batch timing. The pilot numbers
are recorded only to show that the form is executable and stable enough to
register and to motivate PR6's thresholds; they are not evidence for H3.

Corrected pilot fit of 31 August 2026 (Apple M4, AppleClang, `-O3`; 11
trials per cell; 30% stream-group holdout; every row counted). Median and
90th-percentile absolute percentage error on the pilot holdout:

| Structure | Hold-out cells | Update: median, p90 | Query: median, p90 |
| --- | ---: | ---: | ---: |
| lazy (control) | 21 | 3.9%, 11.1% | 5.2%, 23.0% |
| persistent (subject) | 21 | 18.0%, 45.6% | 14.5%, 67.7% |
| copy-on-push | 21 | 13.3%, 50.2% | 19.5%, 68.8% |
| point-only | 10 | 25.9%, 94.1% | 21.2%, 47.8% |
| checkpointing | 20 | 39.9%, 78.9% | 35.2%, 90.9% |
| buffered | 21 | 18.4%, 56.3% | 11.8%, 75.7% |
| fat node | 21 | 14.1%, 69.1% | 17.0%, 110.7% |
| full copy | 1 | 90.9%, 93.3% | 159.5%, 165.8% |

The full-copy row remains one training cell scored on one holdout cell and
says nothing; PR6 must either add enough feasible cells or exclude full copy
from H3 while retaining it as a structural/feasibility baseline. The
subject's update fit drops `visited_records` because it equals
`allocated_records` (Proposition 10.5).

What the pilot says, and what it does not:

- Every predictor exists before timing, the fit artifact is explicit and all
  predictions are positive. The three-stage prepare/fit/evaluate path and
  synthetic fixtures are the properties PR6 needs from Phase 2.
- The revision improves the subject substantially, but the initial H3 target
  (median at most 15%, p90 at most 30%) is still not met: subject update is
  18.0%/45.6% and query is 14.5%/67.7%. The pilot therefore justifies a
  stringent confirmatory test; it does not establish predictive success.
- W5 remains the largest query residual for several persistent strategies,
  and small/rank-poor full-copy and point-only regimes remain unstable. No
  further variable is added after this inspection: doing so would tune the
  model repeatedly to one pilot. PR6 registers this fixed form, prespecifies
  its success/fallback thresholds and reports failure if the confirmatory
  holdout misses them.
- The frozen paper title does not use *predictive*. A failed H3 therefore
  narrows RQ3's result without forcing another title change.

## 6. Retirement is not reclamation

Every persistent structure here publishes versions into an append-only
arena. Dropping a root handle retires a logical version: it can no longer
be queried, and nothing else happens. The records it referenced stay in the
arena because they are shared with other roots, and finding out which are
not shared is itself a traversal or a reference count. Per strategy:

| Strategy | What retiring version `v` frees without a collector | What reclaiming its records needs |
| --- | --- | --- |
| Subject, copy-on-push, point-only | nothing; records are shared along paths | reference counts on records (one word per record, updated on every copy) or a tracing pass from the live roots followed by compaction, which renumbers arena indices |
| Full copy | the whole tree of `v`, since nothing is shared | dropping the root's block, if each version is its own allocation; with one arena, compaction |
| Checkpoint plus log | nothing; the log is a prefix the later versions replay | truncating the log before the oldest live checkpoint, once no live version precedes it |
| Buffered, fat node | nothing; states are appended in place | rewriting nodes to drop stamps older than the oldest live version, which is a rebuild |

The manuscript reports retirement as `O(1)` per version for every strategy
and reclamation as the strategy-specific cost above, and does not claim that
physical memory is freed without one of those mechanisms. Implementing any of
them is outside the programme's scope.

## 7. Review record

| Field | Review record |
| --- | --- |
| Reviewer | Independent automated reviewer acting for Sunjare Zulfiker, at the repository owner's request; wrote none of the code or this document |
| Method | Rebuilt the layout test and the structural mode; re-ran the split self-test and the pilot fit into a scratch directory and reproduced the summary CSV; re-derived the byte equations against the header `static_assert`s and `bench/adapters.hpp`; checked the structural counts against `docs/proof.md` section 10 and the checkpoint source |
| Decision | Approve with changes, 30 August 2026 |
| Required changes and disposition | Count rows with non-positive predictions instead of dropping them and restate the table and prose (done; the corrected table above); state the collinearity of the plan's form and register the reduced form (section 3); mark the full-copy row as a one-cell fit (section 5); normalise every per-update predictor by `updates` (section 3, `cost_model.py`); note the identical-stream leak for the registered split (section 4); correct the `static_assert` and struct claims and add the missing assert to the bench copy-on-push header (section 2); take the cache figure from the capture (section 3, `cost_model.py`); count query records read rather than recursion invocations and note latest-version reads (section 3, `valseg_bench --structural`); print the variant with `std::to_string`; guard `NOMINMAX`; document that `--structural` ignores `--structure` and `--trace`; wrap long lines; add the record constants to the Wiki API page. All applied. |

### Post-merge corrective audit, 31 August 2026

The Phase 2 exit audit found that the merged structural predictor charged
`v mod K` even when `v` was the latest version, although the implementation
queries the live tree with zero replay. It also omitted the second update
traversal taken on checkpoint boundaries. Both counts are corrected in
`bench/structural_counts.hpp` and covered by deterministic tests. The pilot
structural files were regenerated outside the immutable raw directory, the
table above was reproduced, and the stream-group split, collinearity rule,
non-positive-error treatment, missing-input failure and artifact round trip
are covered by synthetic fixtures in CI.
