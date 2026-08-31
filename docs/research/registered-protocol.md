# Registered confirmatory protocol

This document, together with the frozen files listed in
`docs/research/registration-manifest.txt`, is the registered analysis plan
for the confirmatory campaign of the Route B research programme. Once the
manifest is deposited immutably (OSF or Zenodo) with a link to the repository
commit, everything here is frozen: a later change requires a versioned,
timestamped protocol deviation recorded in section 12 before any affected
result is inspected. No confirmatory seed runs before the deposit timestamp.

Status: **prepared, not yet deposited.** The deposit DOI, URL and timestamp
are recorded here at registration time; until then this protocol binds
nothing and no confirmatory measurement may run.

Pilot-informed choices are marked *(pilot)* throughout and their basis is
section 11. The exploratory pilot of 21 August 2026 is never pooled with
confirmatory data.

## 1. Research questions and estimands

The confirmatory campaign answers RQ3 and RQ4 of the programme plan and
supplies the deterministic evidence for RQ2's identities. Estimands, with the
raw columns they resolve to:

| ID | Estimand | Resolution |
| --- | --- | --- |
| E1 | New logical records per published state-changing update | `(nodes − build_nodes) / updates` per trial; identity-checked by H1 |
| E2 | Memory amplification, three separate ratios | payload `bytes`; `alloc_peak_bytes` (allocation binary); `peak_rss_bytes` (fresh process) |
| E3 | Paired batch update-throughput ratio | `updates / update_ns` from batch mode, paired per trial |
| E4 | Paired batch historical-query-throughput ratio | `queries / query_ns` from batch mode, paired per trial |
| E5 | Maximum completed problem scale under the 4096 MiB cap | censored feasibility outcomes (`status = memory_cap`) |
| E6 | Held-out predictive error | the frozen cost-model artifact evaluated once on hold-out cells |

Latency p50/p95/p99 is secondary and comes only from the sampled-latency
runs; it is never inferred from the primary batch replays.

## 2. Design

- **Repetition unit: one fresh process per (cell, structure, trial).** A cell
  is one (workload, n, axis, variant). `bench/run_confirmatory.sh` executes
  the schedule; `bench/confirm_schedule.py` generates it from the binary's
  own cell inventory.
- **Primary timing: batch mode** (`valseg_bench --mode batch`): one clock
  pair around the update batch and one around the query batch, no
  per-operation clock pair. Queries mutate no version's answers, so the
  update-then-query replay publishes byte-identical versions and answers the
  same queries as the interleaved replay; the shared checksum enforces this.
- **Secondary latency: sampled mode** (`--mode latency --sample-every 64`) on
  the registered subset W1, W3, W9. Timer overhead and resolution are
  calibrated per process and recorded beside every number.
- **Order:** trials are blocks. Within each trial block the process order is
  shuffled by `random.Random("20270214|<trial>")` and recorded in the
  schedule file and `exec_order`.
- **Warm-up:** 3 seconds of representative load plus 1 discarded replay per
  process, fixed here; no result-dependent discarding. *(pilot)*
- **Machines:** Apple Silicon (macOS, AppleClang) and Linux x86-64 from a
  different microarchitecture family (GCC). Environment and pinning:
  `bench/collect_environment.sh`, `bench/env/pin_macos.sh`,
  `bench/env/pin_linux.sh`. Power, thermal, governor, core placement and
  allocator are captured before every process.
- **Sensitivity:** W1, W5 and W11 rerun under (a) the platform's second
  compiler (`release-verify-gcc` / `release-verify-clang` presets) and (b) a
  second allocator via preload (`bench/run_sensitivity.sh`), each as its own
  campaign.
- **Memory:** allocation metrics come only from `valseg_bench_alloc`
  (interleaved mode, 3 trials); RSS is per-trial because one process runs one
  cell. Nothing memory-related is read from a timing binary's replay loop
  except the O(1) cap check.
- **Failures:** a trial that reaches the cap is a censored feasibility
  outcome (E5). Its truncated timing never enters a throughput ratio. Cells
  the pilot already showed capped run 2 trials (`bench/capped_cells.csv`);
  the cap is their result.

## 3. Registered constants

| Constant | Value |
| --- | --- |
| Practical equivalence margin `delta` | 1.05 |
| Confirmatory seed base | 20270214 |
| Dry-run seed base (excluded from confirmation) | 20260901 |
| Schedule seed | 20270214 |
| Bootstrap seed / resamples | 20270214 / 10000 |
| Trials per cell (default / primary / alloc / pilot-capped) | 20 / 40 / 3 / 2 |
| Latency sampling | every 64th operation; subset W1, W3, W9 |
| Memory cap | 4096 MiB |
| Split salt / hold-out share | `valseg-confirm-split-20270214` / 0.30 |
| H3 thresholds | hold-out median APE ≤ 15%, p90 ≤ 30% |
| H4 agreement threshold | ≥ 80% on the replication cells |
| H5 predictive region | predicted × [1/1.5, 1.5]; ≥ 80% of eligible hold-out cells |

**Trial counts** *(pilot)*: pilot paired log ratios for the H2 contrast give
a per-cell standard deviation whose 75th percentile requires 33 trials for a
95% interval half-width of `log(1.05)` (t-based); the six registered primary
cells individually require at most 37. Primary cells therefore run 40
trials, all other timing cells 20, within the plan's 20–50 rule. A primary
cell whose realized interval half-width still exceeds `log(delta)` is
labeled underpowered and cannot support practical equivalence
(`bench/analysis/confirm.py`). The pilot spread was measured under
interleaved single-process timing; this is disclosed as a limitation of the
sizing, not re-estimated after seeing confirmatory data.

**Primary cells (H2 family)**, chosen before any confirmatory run for
tagged-partial-update relevance and attainable pilot precision
(`bench/primary_cells.csv`): W2 at n = 1e5 and 1e6; W11 at widths 64 and
512; W10 at hot-window share 0.05; W6 at zero-delta share 0.1.

**Replication cells (H4)**: the six primary cells plus W1 at every size and
W5 at n = 1e4, eleven cells in total, classified independently on both
machines.

## 4. Hypotheses and decision rules

| ID | Statement | Decision rule |
| --- | --- | --- |
| **H1 Structural** | The derived identities predict stored records exactly: subject Σ F, copy-on-push Σ (F + 2P), point-only Σ N, full copy (2n − 1) per nonzero update. | Exact integer equality on every complete confirmatory trial (`confirm.py --stage h1`). No p-value. Any mismatch contradicts H1 for that structure. |
| **H2 Ablation** | On the six primary cells, tag retention beats copy-on-push on batch update throughput by more than `delta`. | Per cell: median paired log ratio with bootstrap CI; *meaningfully faster* only when the whole CI clears `log(delta)`. Holm over the six-cell family for the reported p-values. Anything else is equivalent, slower, inconclusive or underpowered per the classification rule. |
| **H3 Prediction** | The frozen cost model (docs/research/cost-model.md section 3, unchanged) predicts unseen confirmatory cells. | Fit once on training cells, evaluate once on hold-out under the registered salt/share; per structure and operation, hold-out median APE ≤ 15% and p90 ≤ 30%. Full copy is excluded from H3 (one feasible pilot training cell; it stays as a structural and feasibility baseline). A miss removes *predictive* language; the failure and residual structure are reported. |
| **H4 Replication** | The four-state classification is stable across the two machines. | ≥ 80% agreement on the eleven replication cells (`confirm.py`, `h4_agreement`); every disagreement is analyzed and reported. |
| **H5 External** | The model remains informative for the external implementation and the external workload. | The copy-on-push model form fitted on in-house training cells, applied to the external adapter's structural counts: ≥ 80% of eligible hold-out cells fall inside predicted × [1/1.5, 1.5]. Adapter-specific residuals (per-node allocation, allocating queries) are reported beside the share. |

H2 is the only primary inferential family. H1 is deterministic; H3–H5 are
prespecified validation criteria. Every other comparison is exploratory,
BH-flagged within one named family, and cannot be promoted afterward.

## 5. Statistical analysis

All confirmatory statistics live in `bench/analysis/confirm.py`; the frozen
file hashes are in the registration manifest.

- **Estimator:** median of per-trial paired log throughput ratios per cell;
  seeded percentile-bootstrap 95% CI of that median. These are confidence
  intervals and are named so; the pilot's observed percentiles are not used.
- **Classification:** the four-state rule of `classify()` against
  `log(delta)`, interval-based, never point-estimate-based.
- **Primary family:** the six H2 cells, one metric (batch update
  throughput), one baseline (copy-on-push): Holm-controlled.
- **Broad regime map:** every cell × baseline, exploratory, BH at 5% FDR per
  (subject, metric) family, reported as a regime map with the four states.
  No "wins in k of m" statements anywhere, abstract included.
- **Hierarchical model:** per-trial log ratios with a fixed effect per cell
  and a random intercept per trial block (statsmodels MixedLM), as the
  registered pooled view of the regime map. Cell effects are classified with
  the same four-state rule.
- **Sensitivity checks (registered):** mean versus median of log ratios;
  order effect via `exec_order` regression; second compiler; second
  allocator; leave-one-trial-out influence on primary cells.
- **Exclusions:** none, except the registered censoring rule (capped trials)
  and structurally incomplete CSV rows (a process killed mid-write). Both
  are counted and reported. No outlier removal.
- **Cross-structure validity:** every eligible (cell, seed) must agree on
  the answer checksum across all historical structures including the
  external adapter (`confirm.py --stage checksums`); a disagreement stops
  the analysis of that cell and is reported as a defect.

## 6. Blinding

Before any confirmatory measurement, `blind.py seal <campaign>` creates a
sealed HMAC key and label map under `<campaign>/sealed/`, read by no analysis
stage. Primary and regime analyses run with `--blinded`: the statistician
sees `S01`..`S09` and all pairwise contrasts. Labels are opened only after
the primary analysis output files are hashed; the hash and the unblinding
time are recorded in the campaign log. The hold-out labels of H3 stay
untouched until the model artifact hash is recorded, as in
`docs/research/cost-model.md` section 4.

## 7. Workload-to-RQ map

The frozen W1–W12 axes are retained; each maps to a question and no two
cover the same axis (the one known stream duplicate, W4 at n = 1e4 and W11
at width 1, shares a stream-equivalence group in the split, so it can never
straddle training and hold-out).

| Workload | Axis | Serves |
| --- | --- | --- |
| W1 | balanced 50/50 baseline | RQ3 model, H4 replication |
| W2 | update-heavy: copy cost per version | H2 primary, E1/E3 |
| W3 | query-heavy, old and recent reads | E4, latency subset |
| W4 | point updates: documented per-update bound | H1, E1 |
| W5 | full-range updates | H1 boundary case, sensitivity subset |
| W6 | zero-delta share | H2 primary (shared-root fast path) |
| W7 | checkpoint interval K | E3/E4 trade-off of the one tunable baseline |
| W8 | version count 1e3–1e6 | E5 feasibility, version-scaling |
| W9 | Zipf recency skew | E4, `version_distance_transition` predictor |
| W10 | update locality | H2 primary, `working_set_transition` predictor |
| W11 | range width sweep | H2 primary, joins W4 to W5 |
| W12 | oldest-share reads | E4 audit pattern |
| WT | external distribution (section 8) | H5, RQ4 |

## 8. External validity

### 8.1 External implementation

Selection criteria, fixed before selection: (1) a license permitting
vendored redistribution with attribution; (2) semantic compatibility (range
add, historical range sum, 64-bit signed values, one version per update)
verified by the cross-structure checksum; (3) authored outside this project
with no shared code; (4) adapter changes limited to header extraction,
instrumentation counters and interface conversion, each documented and
diffable; (5) identical streams, seeds, flags and cap as every in-house
structure; no per-side tuning.

Selected: **TheAlgorithms/C-Plus-Plus `persistent_seg_tree_lazy_prop.cpp`**
(MIT, author Magdy Sedra, upstream commit `79aeaa9b`), a copy-on-push
persistent lazy segment tree over `shared_ptr` nodes. Audit, exact
modifications and fairness limits: `bench/external/PROVENANCE.md`.
Considered and rejected: ei1333's persistent lazy red-black tree (Unlicense;
a balanced-BST representation, so strategy and structure family would change
together), Library Checker judge solutions (no license grant).

### 8.2 External workload

Search protocol: published temporal-aggregation and persistence literature
(SB-tree, MVSB-tree, Timeline Index, HistOOry) and their artifact links were
searched for released operation logs replayable as range-add/range-sum
streams. None exists; these papers publish workload descriptions, not logs.
Per the plan, the external workload is therefore a distribution derived from
the MVSB-tree experimental criteria
(`bench/traces/make_external_distribution.py`, parameters in the module
docstring), replayed as WT under the same protocol. The absence of a real
external trace is reported in the paper as a limitation, not a finding.

## 9. Cost model registration

The model form, response transform, candidate variables and column-removal
rule are exactly `docs/research/cost-model.md` section 3, frozen at G2 and
unchanged here. This protocol registers: the confirmatory split salt and
share (section 3 above), the two-stage procedure (`cost_model.py --stage
fit` on training rows, artifact SHA-256 recorded in the campaign log, then
one `--stage evaluate`), and the H3/H5 thresholds. Hold-out rows are neither
plotted nor tabulated before the artifact hash is recorded.

## 10. Campaign execution order

1. Register: `bench/make_registration.sh`, deposit, record DOI and timestamp
   here.
2. Dry run on both machines: `VALSEG_DRY_RUN=1 bench/run_confirmatory.sh
   <id-dryrun> <phase>` for every phase; dry-run seeds are excluded from
   confirmation and dry-run campaigns keep `dryrun` in their id.
3. Seal blinding per confirmatory campaign; then per machine:
   `structural`, `timing`, `alloc`, `latency`, `trace` phases, then the two
   sensitivity campaigns.
4. Checksums, H1, primary (blinded), regime (blinded), hierarchical,
   feasibility; hash outputs; unblind; H3 fit/evaluate; H4 across machines;
   H5.
5. Freeze the claim–evidence matrix to the obtained results (Gate G3).

## 11. What the pilot informed

The pilot (one machine, interleaved timing, unregistered) informed: the
practical margin's plausibility, the primary-cell selection and trial
counts (section 3), the capped-cell list, the batch-timing decision (the
pilot measured its own per-operation instrumentation at a large fraction of
the cheapest operations), the warm-up shape, and the cost-model form frozen
at G2 after one revision. The pilot's H3-style numbers missed the H3 target
and are recorded in `docs/research/cost-model.md` section 5 as motivation
for a stringent test, not as evidence.

## 12. Deviations

None. A deviation entry records: date, what changed, why, whether any
affected result had been inspected, and the analysis rerun policy. Forced
decisions already registered: if the second machine is unavailable,
cross-machine claims are dropped rather than simulated; if the external
adapter fails its audit, it is removed and the limitation stated; H2–H5
failures follow the null and contradiction policy of the programme plan.
