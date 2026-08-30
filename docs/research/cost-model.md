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

The plan's mechanistic form is

```text
predicted_ns_per_op = alpha_machine
                    + beta_machine  * visited_records
                    + gamma_machine * allocated_records
                    + eta_machine   * bytes_touched
                    + theta_machine * working_set_transition
```

fitted per structure and per operation type (update, query), with the
coefficients machine-specific. Two of its terms are not separately estimable
within one structure: `bytes_touched` is a fixed multiple of the record
counts (one record size per structure), and for the tag-retaining subject
`allocated_records` equals `visited_records` exactly (Proposition 10.5). The
form the pilot fits, and the one PR6 registers unless it justifies a change,
is therefore the reduced form without `bytes_touched`, with
`visited_records` dropped where it coincides with `allocated_records` and
`allocated_records` dropped for queries, where it is zero. The exact
response transform, interactions and regularization are frozen in PR6; this
section freezes the candidate variables and how each is obtained.

| Variable | Updates | Queries | Source | Available for |
| --- | --- | --- | --- | --- |
| `visited_records` | records the update recursion reads, per update: `Σ F / updates` for the tree strategies; `Σ N / updates` for point-only; `(2n − 1) · nonzero / updates` for full copy | records a query reads, `Σ F / queries` (the recursion is also entered at disjoint children but reads nothing there); checkpointing adds the replayed log entries, `Σ (v mod K) / queries`, zero for a read of the latest version | `structural_*.csv`, machine-independent | every structure; the checkpoint replay term is strategy-specific |
| `allocated_records` | records stored per update, `(nodes − build_nodes) / updates` | 0 | `runs_*.csv`, machine-independent | every structure; for the subject, copy-on-push and point-only it is the identity of section 10 summed over the stream, so it is also predictable a priori |
| `working_set_transition` | `log2(max(1, retained payload bytes / cache bytes))` | same | `bytes` in `runs_*.csv`; the cache size is `l2_bytes` from the campaign's `system_*.txt` | every structure |

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

`bench/analysis/split.py` assigns every cell `(workload, n, axis, variant)`
to `training` or `holdout` by the SHA-256 of the cell key under a fixed
salt; every seed and every trial of a cell follow the cell. The pilot salt
and 30% hold-out share are pilot-development values. PR6 registers its own
salt and share before the confirmatory campaign, and the confirmatory
hold-out labels are not read until the model and analysis hashes are fixed.

One leak the cell key does not close: two cells can generate byte-identical
streams (W4 at `n = 10000` and W11 at width 1 do, for every seed). In the
pilot both landed in training, so nothing leaked, but the registered split
must key identical streams together or exclude the duplicated endpoints of
a sweep; PR6 records which.

## 5. Pilot-only model development

`bench/analysis/cost_model.py` fits the section 3 form on the pilot's
training cells and reports median and 90th-percentile absolute percentage
error and the median absolute log ratio on the pilot's hold-out cells, per
structure and operation type, with residuals broken down by workload. It
reads `structural_timing-W*.csv`, which `valseg_bench --structural` writes
for the pilot's seeds (`--seed 20260818 --warmup 3 --trials 11`).

Its output is exploratory: one machine, one compiler, the per-operation
clock pair of the pilot harness rather than batch timing, and a model form
that PR6 may still change with written justification. The pilot numbers are
recorded here only to show the model form is stable enough to register and
to motivate the registered thresholds; they are not evidence for H3.

Pilot fit of 30 August 2026 (Apple M4, AppleClang, `-O3`; 11 trials per
cell; 30% of cells held out by the pilot salt; every row counted, a
non-positive prediction scored at its full error). Median and 90th-percentile
absolute percentage error on the pilot's hold-out cells:

| Structure | Hold-out cells | Update: median, p90 | Query: median, p90 |
| --- | ---: | ---: | ---: |
| lazy (control) | 10 | 6.8%, 22.9% | 5.4%, 40.5% |
| persistent (subject) | 10 | 58.7%, 154.4% | 31.8%, 416.8% |
| copy-on-push | 10 | 45.8%, 170.2% | 34.1%, 563.0% |
| point-only | 6 | 28.3%, 76.0% | 19.0%, 168.4% |
| checkpointing | 11 | 36.3%, 82.4% | 20.8%, 77.3% |
| buffered | 10 | 34.5%, 166.5% | 21.7%, 485.0% |
| fat node | 10 | 20.3%, 120.7% | 26.2%, 463.7% |
| full copy | 1 | 944.0%, 949.1% | 293.4%, 300.4% |

The full-copy row is one training cell (W8, `n = 10000`) scored on one
hold-out cell (W3, `n = 1000`); it is a rank-one fit and says nothing. The
subject's update fit keeps `allocated_records` only, because its
`visited_records` is the same number (Proposition 10.5).

What the pilot says, and what it does not:

- The reduced form can be fitted and evaluated for every structure without
  reading a hold-out label, from counts that exist before a cell is timed.
  That is the property PR6 needs from this PR.
- The form as written does not predict. Ten percent of the hold-out rows of
  every persistent structure receive a negative prediction (the W5 cells,
  where every update copies one root and costs a few nanoseconds; a linear
  intercept fitted across cells sits far above that), and the 90th
  percentile of the error is above 100% for every persistent structure on
  at least one operation. The plan's initial H3 target (median at most 15%,
  90th percentile at most 30%) is missed everywhere except the control.
- The residuals are systematic, not noise: W5 (full-range updates) and W12
  (reads confined to the oldest versions, a locality effect the working-set
  term does not see) dominate the hold-out residuals for every tree
  structure. A logarithmic response would remove the negative predictions;
  a locality term (version distance of the read) and a full-coverage
  indicator are the candidates the residuals point at.
- PR6 therefore has a written, pilot-based reason to revise the form before
  registration, and the revision, its thresholds and the fact that they are
  pilot-informed go into the registered protocol before any confirmatory
  run. If the registered form still misses its target on the confirmatory
  hold-out cells, *predictive* leaves the title (plan, section 4.7). The
  pilot numbers above are never that decision.

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
