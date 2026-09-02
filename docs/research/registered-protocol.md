# Registered confirmatory protocol

This document, together with the frozen files that will be listed in
`docs/research/registration-manifest.txt`, is the prepared analysis plan
for the confirmatory campaign of the Route B research programme. Once the
manifest is deposited immutably (OSF or Zenodo) with a link to the repository
commit, everything here is frozen: a later change requires a versioned,
timestamped protocol deviation recorded in section 12 before any affected
result is inspected. No confirmatory seed runs before the deposit timestamp.

Status: **prepared, not yet deposited.** The deposit DOI, URL and timestamp
are recorded here at registration time; until then this protocol binds
nothing and no confirmatory measurement may run.

| Registration field | Value |
| --- | --- |
| Frozen repository commit | Pending PR6 statistical review and PR7 two-machine dry run |
| Manifest SHA-256 | Pending clean-commit generation and verification |
| Immutable DOI / URL | Pending OSF or Zenodo deposit |
| Deposit UTC timestamp | Pending |
| Statistical reviewer / approval record | Approved at round 3 on 1 September 2026 by the automated reviewer acting for Sunjare Zulfiker; record in `docs/research/statistical-review.md` |
| Blinding custodian / controlled location | Sunjare Zulfiker, custody material held on his own machine/account outside the primary analyst's routine access; assigned 2 September 2026 |

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
- **Order:** `(machine, mode, cell, trial)` is the experimental block. Every
  eligible structure runs once in that block, in a deterministic
  counterbalanced permutation. The schedule and every raw row record both a
  global process sequence and `exec_order`, the zero-based position within
  the block. H2 pairs on `(cell, trial)`; the registered order sensitivity
  uses the subject-minus-baseline `exec_order` gap.
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
  campaign. The exact sixteen cells and twenty alternate-campaign trials per
  cell are versioned in `bench/sensitivity_cells.csv`.
- **Memory:** allocation metrics come only from `valseg_bench_alloc`
  (interleaved mode, 3 trials); RSS is per-trial because one process runs one
  cell. Nothing memory-related is read from a timing binary's replay loop
  except the O(1) cap check.
- **Failures:** a trial that reaches the cap is a censored feasibility
  outcome (E5). Its truncated timing never enters a throughput ratio. Cells
  the pilot already showed capped run 2 trials (`bench/capped_cells.csv`);
  the cap is their result. A structurally incomplete CSV row (any empty
  field, as a process killed mid-write leaves) is counted and excluded. Apart
  from H2's explicit conservative insufficient-pair rule, no other missing
  row, non-finite value, failed registered model, or absent H3/H4/H5 cell is
  silently removed: the affected registered decision is unavailable, an
  explicit `<stage>_unavailable.json` record carrying the reason and
  diagnostics stands in for its output and is hashed with the other outputs,
  and the locked analysis command exits non-zero after every other registered
  decision has been produced. One unavailable decision never blocks the
  hashing or unblinding of the rest. A prepare or fit failure of the cost
  model makes both H3 and H5 unavailable because they share the frozen
  artifact. A software defect that is not a registered failure (a crash
  outside the registered rules) leaves neither output nor record; unblinding
  refuses to proceed until the defect is fixed and the stage rerun, which is
  safe because every stage removes its own stale output or record first and
  the rerun is recorded as a deviation in section 12.

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
| H5 predictive region | predicted × [1/1.5, 1.5]; ≥ 80% of 24 draw-operation cells |
| H5 external trace draws | 12 × 20 trials (`bench/h5_trace_draws.csv`) |

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

**Array sizes**: W1-W5 run at n = 1e3, 1e4, 1e5, 1e6 and 1e7
(`bench/workloads.cpp`). The pilot stopped at 1e6, which is inside the working
set the sharing structures were built for. The fifth decade is where the
per-version copy cost of a no-sharing baseline meets the 4096 MiB cap and
where no structure's tree fits a cache level, so it is the decade that
separates a per-operation cost growing with log n from one growing with n. No
pilot ran at 1e7, so no 1e7 cell is a primary cell and none informs a trial
count; 1e7 cells run the 20-trial default and enter the exploratory regime map
and the censored feasibility estimand E5. The cap is unchanged at every size:
a structure that cannot finish 1e7 inside it produces a censored feasibility
outcome, which is the result.

**Replication cells (H4)**: exactly the twelve rows of `bench/h4_cells.csv`:
the six primary cells, W1 at all five sizes, and W5 at n = 1e4. They are
classified independently on both machines. The file also fixes 40 paired
trials for the six primary rows and 20 for the other six. A missing pair,
missing/duplicate row or unclassified cell makes H4 unavailable; agreement is
never computed on the intersection of what happened to finish.

## 4. Hypotheses and decision rules

| ID | Statement | Decision rule |
| --- | --- | --- |
| **H1 Structural** | The derived identities predict stored records exactly: subject Σ F, copy-on-push Σ (F + 2P), point-only Σ N, full copy (2n − 1) per nonzero update. | Exact integer equality on every complete confirmatory trial (`confirm.py --stage h1`). No p-value. Any mismatch contradicts H1 for that structure. |
| **H2 Ablation** | On the six primary cells, tag retention beats copy-on-push on batch update throughput by more than `delta`. | Per cell: median paired log ratio with bootstrap CI; *meaningfully faster* only when the whole CI clears `log(delta)`. Holm over the six-cell family for the reported p-values. Anything else is equivalent, slower, inconclusive or underpowered per the classification rule. |
| **H3 Prediction** | The frozen cost model (docs/research/cost-model.md section 3, unchanged) predicts unseen confirmatory cells. | Fit once on training cells, evaluate once on hold-out under the registered salt/share. The unit is one `(workload, n, axis, variant, structure, operation)` cell: actual and predicted trial responses are reduced separately by the median, then every cell has equal weight regardless of its registered trial count. Each structure's hold-out inventory is fixed prospectively by the prepare stage as the registered hold-out cells minus that structure's rows in `bench/capped_cells.csv`; a registered-capped structure-cell is outside the inventory whether or not it completes, and an unregistered cap or missing cell makes H3 unavailable rather than shrinking the inventory. The capped registry identifies a cell by `(workload, n, variant)`; the prepare stage verifies that this identifies one hold-out cell. For each included in-house persistent structure and operation, hold-out median APE ≤ 15% and p90 ≤ 30%. Lazy control, full copy and the external adapter are excluded from H3. A miss removes *predictive* language; the failure and residual structure are reported. H5 runs whether or not H3 is available. |
| **H4 Replication** | The four-state classification is stable across the two machines. | ≥ 80% agreement on all twelve rows of `bench/h4_cells.csv` (`confirm.py --stage h4`); every disagreement is reported. Any missing/unclassified cell makes H4 unavailable. |
| **H5 External** | The copy-on-push model transfers to an independently authored implementation under an externally motivated workload distribution. | `cost_model.py --stage transfer` applies the copy-on-push training coefficients without refitting to the external adapter's structural predictors on both operations of every one of the twelve registered trace draws. H5 is supported only when at least 80% of all 24 draw-operation cells fall inside predicted × [1/1.5, 1.5]. A missing draw/operation or non-finite/non-positive value makes H5 unavailable. Adapter-specific residuals (per-node allocation and allocating queries) are reported beside the share. |

H2 is the only primary inferential family. H1 is deterministic; H3–H5 are
prespecified validation criteria. Every other comparison is exploratory,
BH-flagged within one named family, and cannot be promoted afterward.

## 5. Statistical analysis

All confirmatory statistics live in `bench/analysis/confirm.py`; the frozen
file hashes are in the registration manifest.

- **Estimator:** median of per-trial paired log throughput ratios per cell;
  seeded percentile-bootstrap 95% CI of that median. Every reported empirical
  percentile uses NumPy's `linear` quantile interpolation. These are confidence
  intervals and are named so; the pilot's observed percentiles are not used.
- **Wilcoxon edge cases:** an all-zero paired-difference vector has statistic
  zero and `p = 1` by definition. Any non-finite input, statistic or p-value
  aborts the registered stage; it is never omitted before Holm/BH correction.
  Otherwise SciPy's two-sided Wilcoxon uses `zero_method="wilcox"`, no
  continuity correction and the locked version's `method="auto"` rule. The
  locked environment (`bench/analysis/uv.lock`, in the manifest) resolves
  SciPy 1.18.0 with NumPy 2.5.2 on Python 3.12 and later and SciPy 1.17.1
  with NumPy 2.4.6 on Python 3.11; both confirmatory machines run Python 3.12
  or later, and every stage table records the resolved `scipy_version` and
  `numpy_version` it was computed with.
- **Classification:** the four-state rule of `classify()` against
  `log(delta)`, interval-based, never point-estimate-based.
- **Blinded orientation:** the custodian supplies the registered contrast's
  two opaque labels in lexical order as `LABEL-A < LABEL-B`; neither is called
  treatment, subject or baseline in the analyst workflow. Pre-unblinding
  effects are oriented as A relative to B and carry both labels. After names
  are restored, a `meaningfully faster` row supports the persistent direction
  when its named `subject` is `persistent`; a `meaningfully slower` row does so
  when its named `baseline` is `persistent`. Equivalence and inconclusive
  states are orientation-invariant; p-values are two-sided. This mechanical
  rule applies to H2 and its paired sensitivities.
- **Primary family:** the six H2 cells, one metric (batch update
  throughput), one baseline (copy-on-push): Holm-controlled. A cell with too
  fewer pairs than its registered trial count is assigned the conservative
  `p = 1`, labeled `insufficient_pairs`, classified `inconclusive` whatever
  its interval says (the interval is retained in the row), marked
  underpowered and kept in the six-value Holm family; it is never silently
  removed. The H2 registry must
  contain exactly six unique `(workload, n, axis, variant)` rows; extra pairs
  are a protocol error rather than additional evidence.
- **Broad regime map:** every cell × baseline, exploratory, BH at 5% FDR per
  (subject, metric) family for both update and query throughput, reported as a
  regime map with the four states. During blinding it is computed twice, once
  for each canonical contrast label, into separately hashed files. After
  unblinding only the file whose named subject is `persistent` is the
  registered subject family; the other is a prespecified blinding control and
  cannot be promoted.
  No "wins in k of m" statements anywhere, abstract included.
- **Hierarchical model:** per-trial log ratios with a fixed effect per cell
  and a random intercept per registered trial-index round across cells
  (statsmodels MixedLM), as the
  registered pooled view of the regime map. Fresh fits try `lbfgs`, `bfgs`,
  then `cg`, each for at most 500 iterations. Every optimizer, convergence
  flag, warning, and the minimum eigenvalues of the random- and fixed-effect
  covariance matrices are recorded. A fit is accepted only when statsmodels
  reports convergence, every matrix/value is finite, the random-intercept
  variance exceeds `1e-10`, and the fixed-effect covariance has positive
  minimum eigenvalue greater than `1e-12` times its maximum eigenvalue. There
  is no OLS inferential fallback. If every attempt fails, the pooled view is
  unavailable, its diagnostics are preserved in the diagnostics file and the
  unavailable record, and the locked command records that and continues;
  per-cell H2 is not replaced or reinterpreted.
- **Mean-versus-median sensitivity:** for every eligible H2 contrast, report
  median and arithmetic-mean paired log effects, separate seeded bootstrap
  intervals/classifications, their effect difference and whether the
  classification changes. A cell with fewer than four pairs remains in the
  output as `insufficient_pairs`/inconclusive. For H3, retain median- and
  mean-within-cell APE summaries; cells remain equally weighted in both.
- **Order sensitivity:** regress per-trial paired log ratio on
  `subject exec_order - baseline exec_order`, with a fixed effect for every
  cell and HC3 covariance. Report coefficient, 95% interval and p-value. A
  missing, non-finite or constant `exec_order` makes the diagnostic
  unavailable; the primary result is not adjusted post hoc.
- **Leave-one-trial-out sensitivity:** omit each complete pair in turn and
  report the effect shift and any four-state classification change for every
  eligible cell. A cell with fewer than five pairs gets one explicit
  `insufficient_pairs` row because no registered four-pair leave-one-out
  estimate exists. Censored trials were never in the paired sample and are
  not an omission candidate.
- **Compiler and allocator sensitivity:** rerun W1, W5 and W11 under the
  registered alternate compiler/allocator. Report the per-cell primary and
  sensitivity effects, their difference and every classification change.
  Both inputs must match all sixteen registered cells, and the alternate
  campaign must contain exactly twenty complete pairs per cell. Missing or
  extra cells/trials abort that sensitivity stage; no pass/fail threshold is
  attached to these diagnostics.
- **Exclusions:** none, except the registered censoring rule (capped trials)
  and structurally incomplete CSV rows (a process killed mid-write). Both
  are counted and reported; every analysis table carries its input-wide
  incomplete-row count, while feasibility reports capped outcomes. No
  outlier removal, winsorization, imputation,
  response-dependent trial removal or post-unblinding exclusion is permitted.
- **Cross-structure validity:** every eligible (cell, seed) must agree on
  the answer checksum across all historical structures including the
  external adapter (`confirm.py --stage checksums`); a disagreement stops
  the analysis of that cell and is reported as a defect.

The explicit decision entry points are `confirm.py --stage h3`, `--stage h4`
and `--stage h5`. H3/H5 verify the generated prediction CSV sidecar and require
its recorded model hash to equal the supplied frozen artifact before reading
the decision values. `bench/run_registered_analysis.sh` is the one locked
end-to-end script, in two halves run by different people. The `analyst` half
takes only the opaque analyst campaigns and the two lexically ordered contrast
labels and runs the blinded primary, regime, hierarchical, feasibility,
sensitivity, H4, compiler and allocator stages; it never receives a named
campaign or the custody directory. The `custodian` half hashes every machine's
outputs and unblinds; verifies the named inputs against custody; runs the
named deterministic checks; executes the three-stage cost model; runs H3, then
H5 regardless of H3's status; and finally re-verifies that every
pre-unblinding output still matches its recorded hash. An exploratory
table is always labeled exploratory and can never be promoted into H1–H5, the
abstract, or the primary family through a deviation.

PR7 supplies H5's versioned response/predictor join: one row per external
adapter trial with `workload = draw_id`, the ordinary timing response columns,
the same structural predictor columns used by the model, and `trace_seed`,
`trace_operations`, `trace_update_share` and `trace_interval_share`. The H5
transfer rejects every row whose status is not `ok`; the H5 stage verifies
those fields, `n` and the exact complete-trial count against
`bench/h5_trace_draws.csv`. The locked
pipeline passes that file to `cost_model.py --stage transfer`; hand-authored
predictions without the generated provenance fields, matching artifact and
checksum sidecar are not an accepted H5 input. Missing or extra draw-operation
rows also abort rather than changing the denominator.

## 6. Blinding

One blinding seal covers both machines and the registered sensitivity
campaigns. Before measurement, a custodian runs `blind.py seal` with a
controlled `--custody-dir` outside every analyst campaign. The key, named raw
campaign and structure-name map remain there; the analyst directory receives
only a key hash commitment (`blind.py attach` binds the other machine and
sensitivity campaigns to the same study-wide seal) and, via `blind.py blind`, generically named CSVs
whose structure field is already `S01`..`S09`. The custodian also retains a
hash manifest binding every named source file to its blinded copy; the analyst
gets only its commitment and blinded-file hashes. `confirm.py --blinded` only
validates opaque labels and has no key/map import path.

For the registered pair, the analyst receives only the two labels sorted
lexically. The runner rejects a role-labeled or noncanonical order, and every
paired output carries both opaque labels so direction can be interpreted only
after unblinding. The broad regime is evaluated under both possible subject
labels before disclosure. Thus the public hypothesis reveals the pair of
implementations under comparison, but not which opaque label is which.

After all blinded primary, regime, hierarchical, feasibility, H4 and
sensitivity files exist, one multi-campaign `blind.py unblind` call hashes
every machine's analysis CSV/diagnostic before opening custody material. The
first campaign is the coordinating campaign and must also contain H4,
compiler and allocator outputs. It writes named copies under each campaign's
`analysis/unblinded/` and an `unblinding.json` record containing both the
campaign and study-wide primary-output SHA-256 plus the common UTC unblinding
time. Before names are written, `unblind` reads the opaque contrast labels
that the blinded outputs carry and refuses to proceed unless they resolve to
exactly the registered pair; the named copies gain a `persistent_direction`
column that applies the orientation rule of section 5 mechanically. The
custodian executes the `custodian` half of the locked script and the analyst
the `analyst` half; the custodian and controlled path are recorded in the
registration deposit access log; they must not be the primary analyst or a
location routinely available to that analyst. H3 holdout responses remain
unopened until the fitted artifact and its hash exist, as specified in
`docs/research/cost-model.md` section 4.

Before named checksums or H1 run, `blind.py verify-named` verifies the named
campaign inventory and hashes against that custody manifest and rechecks the
blinded input hashes. Thus post-unblinding deterministic checks cannot be
silently redirected to a different named dataset. After the named stages,
`blind.py verify-outputs` re-hashes every pre-unblinding output against the
recorded manifest, so a post-unblinding rerun of a blinded stage is detected.

This is label blinding, not a claim of perfect implementation concealment.
The analyst necessarily knows which two opaque labels form the registered
contrast, and structure-specific timing or memory profiles may permit a label
to be inferred. The custody boundary prevents direct access to names and the map, while
the pre-unblinding output hash makes any analysis produced before disclosure
auditable; this residual side channel is disclosed as a study limitation.

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
docstring). It is not one trace measured repeatedly: `bench/h5_trace_draws.csv`
registers twelve deterministic draws (`WT01`–`WT12`, seeds 20270214–20270225)
at n = 100000, 200000 operations, update share 0.5 and interval share 0.01.
Each draw has twenty complete trials whose medians estimate its two responses;
H5 then gives every draw-operation cell one vote. The absence of a real
external trace and the limited twelve-draw
distribution are reported as limitations, not findings.

## 9. Cost model registration

The model form, response transform, candidate variables and column-removal
rule are exactly `docs/research/cost-model.md` section 3, frozen at G2 and
unchanged here. This protocol registers: the confirmatory split salt and
share (section 3 above), the three-stage procedure (`cost_model.py --stage
prepare` to write disjoint response files, one `--stage fit` that can read only
the training file and records the artifact SHA-256, then one `--stage
evaluate` that verifies that hash before opening holdout), and the H3/H5
thresholds. Hold-out rows are neither opened, plotted nor tabulated before the
artifact hash is recorded.

## 10. Campaign execution order

1. Before registration, complete the PR7 dry run on both machines:
   `VALSEG_DRY_RUN=1 bench/run_confirmatory.sh
   <id-dryrun> <phase>` for every phase; dry-run seeds are excluded from
   confirmation and dry-run campaigns keep `dryrun` in their id. Resolve all
   resulting defects before freezing PR7.
2. Merge/freeze PR6 and PR7 at one clean commit; obtain independent statistical
   approval and name the custodian; run `bench/make_registration.sh`, verify
   every checksum, deposit, and record the commit, DOI and timestamp here.
3. Re-verify the registered commit and manifest, seal one study-wide blinding
   map under independent custody, then per machine:
   `structural`, `timing`, `alloc`, `latency`, `trace` phases, then the two
   sensitivity campaigns.
4. The analyst runs `bench/run_registered_analysis.sh analyst`:
   primary/regime/hierarchical and registered sensitivities on the opaque
   analyst copies; H4 across machines; compiler and allocator sensitivities.
   The custodian then runs `bench/run_registered_analysis.sh custodian`: hash
   every output; unblind; verify controlled named input against its custody
   manifest; run checksums and H1; H3 prepare/fit/evaluate; H5; verify the
   pre-unblinding outputs are unchanged.
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

None. A deviation entry records: UTC date, requester/approver, what changed,
why, whether any affected response, blinded result or named result had been
inspected, affected files/hypotheses, and the rerun/claim policy. Deviations
never promote an exploratory result to a primary finding. Forced decisions
already registered: if the second machine is unavailable, cross-machine
claims are dropped rather than simulated; if a required H4/H5 cell is missing,
that hypothesis is unavailable rather than recomputed on a smaller set; if
the external adapter fails its pre-measurement audit, H5 is removed and the
limitation stated; and H2–H5 failures follow the null and contradiction policy
of the programme plan.
