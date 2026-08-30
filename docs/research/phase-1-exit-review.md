# Phase 1 exit review

Reviewed 30 August 2026 against [the versioned Phase 1 charter](phase-1-charter.md)
and the adopted Route B programme in `.local/docs/PLAN.md`.

## Verdict

**Implementation and evidence verdict: Gate G1 passed.** Every Phase 1
criterion passes, the source audit leaves a defensible but narrower Route B,
the independent reader has signed the
[claim-evidence matrix](claim-evidence-matrix.md), and no unresolved
implementation defect remains in the reviewed scope.

**Formal phase verdict: exited on 30 August 2026.** PR #34 merged into `main`
as `317dac7` and PR #35 as `f8a184f`, each with green CI on its merged
revision; the record is in the final integration table below.

Overall rating for the implementation and evidence package: **9.5/10**. The
deduction is for release governance, not a known code or research-model defect.

The exit audit found one statement that had to be corrected before Phase 2
builds on it: the capability taxonomy and `policy.hpp` said outermost-first
tag accumulation "reverses chronological order". It does not; the tree order
is independent of chronological order, and only a tag retained on an ancestor
through a partial descent is applied outside newer tags below it. Section 2.2
of the taxonomy now states the exact misordered pairs with a two-update trace
on `n = 4`, which PR3's necessity theorem must quantify over. The audit also
added the plan's remaining audit-minimum rows (Kaplan survey, Timeline Index,
Johnson, McGeoch) with primary-source flags, and the eight-field audit table
for the five gate-deciding items.

| Area | Rating | Assessment |
| --- | ---: | --- |
| Pilot provenance and reproduction | 9.5/10 | Complete, automated and honest about the original dirty worktree limitation |
| Prior-art positioning | 9.5/10 | Required primary texts and direct implementation source audited; novelty narrowed appropriately |
| Algebraic/capability model | 9.5/10 | Laws, composition, arithmetic boundary, capability restrictions and model R are internally consistent |
| Tests and build quality | 10/10 | Debug, strict, Release and sanitizer suites pass; format, Doxygen, manuscript and report checks pass |
| Exit governance | 9/10 | Independent signature recorded; merge of the two pull requests is the remaining integration action |

## Charter assessment

### PR1 — pilot freeze and positioning

| Criterion | Status | Evidence |
| --- | --- | --- |
| Preserve raw pilot data and environments with checksums | Pass | `bench/results/raw.sha256` covers 96 raw files; `bench/results/README.md` records origin and limitations |
| One-command regeneration | Pass | `bench/verify_pilot.sh` verifies checksums, regenerates analysis outputs and rebuilds the report |
| Analysis prose matches implementation | Pass | Wilcoxon pairing, BH/Holm families, raw ratio bounds and exploratory scope are stated consistently in the report and benchmark guide |
| Required prior-art audit | Pass | SB-tree, MVSB-tree, Ibtehaz et al. and Pluquet et al. primary texts read; Library Checker source audited at a fixed upstream commit; ACM policy date verified |
| Exploratory label at report entry points | Pass | The title page and benchmark documentation identify the campaign as an exploratory pilot, not a confirmatory study |
| Claim dependencies preserved | Pass locally | Analysis/lock/report sources and the manifest are versionable; raw/generated data remain checksummed and scheduled for external deposit |
| Independent reader signs the matrix | Pass | Section 10 of `claim-evidence-matrix.md`, signed 30 August 2026 |

The pilot provenance record correctly says the measurement checkout was dirty.
It records base commit `1dfb090` and does not pretend that later commit `adf65ec`
proves the exact dirty diff. This limits forensic reconstruction of the pilot
source state but does not invalidate its Phase 1 role as exploratory evidence.

### PR2 — semantic capability taxonomy

| Criterion | Status | Evidence |
| --- | --- | --- |
| Complete laws and composition convention | Pass | `policy.hpp` and `capability-taxonomy.md` define `compose(newer, older)` and the aggregate/action laws |
| Source-audited strategy matrix | Pass | Each strategy row names its mechanism, supported scope, code and later proof obligation |
| Checkpoint restriction is honest | Pass | Query projection is SumAdd-specific; MinAdd requires reconstruction or additional retained information |
| Policy interface, law tests and oracle | Pass | Small-domain exhaustive laws, chronological oracle tests and capability checks pass |
| Defined arithmetic behavior | Pass | SumAdd/MinAdd use checked mathematical-integer refinement; AffineSum canonicalizes modular inputs and avoids intermediate wraparound near `2^32` |
| Representation model R | Pass | Word model, fixed-size records, root accounting, auxiliary storage, sharing, complexity and allocation metric are fixed before Phase 2 lower-bound work |
| Existing public SumAdd contract unchanged | Pass | Production trees remain SumAdd-only; all existing deterministic and differential tests pass |

The dedicated copy-on-push suite verifies both the exact hand trace after a
tagged-root partial update and seeded agreement with the brute-force versioned
array. This closes the prior gap where the benchmark ablation was described but
not directly tested.

### Gate G1 — positioning and model

| Contribution | Gate assessment |
| --- | --- |
| C2 observational commutativity boundary | Defensible to attempt. SB/MVSB use additive contributions; the audited Library Checker affine tree is copy-on-push and preserves chronological order rather than retaining ancestor tags. |
| C3 frontier laws/lower bound | Defensible to attempt under model R. No reviewed source supplies a record-allocation frontier or lower bound for this in-memory partial-persistence range-action model. |
| C4 predictive cost model | Open and defensible; no reviewed persistence-strategy selection model subsumes it. |
| C5 registered regime study | Open but must be stated narrowly against Pluquet et al.'s existing single-machine experimental persistence work. |

Gate recommendation: **continue Route B**, using the narrow retained-ancestor
action-order and immutable-record-frontier language. Do not claim the first
persistent lazy aggregate tree, and do not put *boundary*, *optimality* or
*predictive* into accepted results before the corresponding Phase 2/3 gates.

## Verification record

| Check | Result |
| --- | --- |
| Debug configure/build/test | 249/249 tests passed |
| Strict Debug, warnings as errors | 249/249 tests passed |
| Optimized Release | 249/249 tests passed |
| Apple Clang ASan + UBSan build | 249/249 tests passed |
| Changed C++ formatting | `clang-format --dry-run --Werror` passed |
| Doxygen | `doxygen Doxyfile` passed with warnings treated as errors |
| Manuscript | `latexmk -pdf -cd paper/main.tex` passed |
| Pilot reproduction | 96 checksums, 3,324 trials, 357 summary cells and the 15-page report verified |
| Pilot PDF visual review | All 15 rendered pages checked; no clipping, overlap, broken glyphs or unreadable figures/tables |
| Campaign safety | Missing campaign ID rejected; a second run against the same output files rejected before overwrite |
| Shell syntax | Campaign, environment and pilot-verifier scripts passed `bash -n` |

## Required actions before declaring Phase 1 exited

1. Done: the independent reader completed section 10 of the claim-evidence
   matrix and approved Route B on 30 August 2026.
2. Done: the closeout changes are committed and pushed to the Phase 1 pull
   requests; CI runs on the pushed revisions.
3. Done: PR #34 merged as `317dac7`, then PR #35 as `f8a184f`; both recorded
   below.

Phase 2 planning can proceed in parallel, but PR3/PR4 theorem results must not
be treated as accepted and must not merge before these three closeout actions
are complete.

## Final integration record

| Field | Value |
| --- | --- |
| Independent review | Recorded 30 August 2026, claim-evidence matrix section 10 |
| PR #34 merged revision | `317dac7` (squash of `3d85cd2`), 30 August 2026 |
| PR #35 merged revision | `f8a184f` (squash of `761807a`), 30 August 2026 |
| Fresh required CI | Green on `3d85cd2` and `761807a` (pull request and push runs) |
| Phase 1 exit declared by | Mohammad Nobinur, repository owner |
| Exit date | 30 August 2026 |
