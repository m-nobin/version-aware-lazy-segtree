# Claim-evidence matrix

Prior-art audit for the research programme, recorded 30 August 2026. Each
family below answers: what is the closest prior mechanism, and what exact
difference can this project defend? Search-result counts are not novelty
evidence; every row cites the specific work read or verified. Items whose
primary text could not be obtained are flagged; a claim is not built on a
flagged row until the primary source is read.

The project's proposed contributions, for reference:

- **C1** semantic model and strategy capability taxonomy
  ([capability-taxonomy.md](capability-taxonomy.md));
- **C2** observational commutativity boundary for retained, outermost-first
  action accumulation;
- **C3** exact visited/push frontier laws and a lower-bound attempt in a
  stated representation model;
- **C4** physical and predictive cost models;
- **C5** registered, externally validated regime study and reusable artifact.

## 1. Persistence foundations

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Driscoll, Sarnak, Sleator, Tarjan, "Making data structures persistent", JCSS 38(1):86-124, 1989, DOI 10.1016/0022-0000(89)90034-2 | Fat-node and node-copying (partial persistence), node splitting (full). Pointer machine, point updates only, O(1) amortized space per field change. Theory only, no aggregation. | No range actions, no deferred aggregate state, no lazy tags. Our per-update unit is a range action whose retained-tag representation DSST's model does not describe. |
| Sarnak, Tarjan, "Planar point location using persistent search trees", CACM 29(7):669-679, 1986, DOI 10.1145/6138.6151 | Path copying named and analyzed (O(log n) space per update); improved to O(1) amortized via limited node copying. Insert/delete only. | Establishes the path-copying baseline the subject uses; no aggregates, no range updates. Cited as foundation, not overlap. |
| Becker, Gschwind, Ohler, Seeger, Widmayer, "An asymptotically optimal multiversion B-tree", VLDB Journal 5(4):264-275, 1996, DOI 10.1007/s007780050028 | External-memory MVBT; point insert/delete at current version, queries on any version; matches single-version B-tree bounds; space linear in updates. | Point-update, disk, no aggregation. Its optimality result is the model for how C3's claim must be stated, not a competing result. |
| Fiat, Kaplan, "Making data structures confluently persistent", SODA 2001; J. Algorithms 48(1):16-58, 2003, DOI 10.1016/S0196-6774(03)00044-0 | Confluent model (version DAG with melds); information-theoretic space-expansion lower bounds and near-matching transformations. | The canonical persistence space lower bounds, but for the confluent model with point updates. C3's lower-bound attempt targets partial persistence with range actions, which these bounds do not cover; cite to delimit. |

## 2. Lazy segment-tree invariants

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Ibtehaz, Kaykobad, Rahman, "Multidimensional segment trees can do range updates in poly-logarithmic time", Theoretical Computer Science 854:30-43, 2021; arXiv:1811.01226 | Emulates lazy propagation in higher dimensions for O(log^d n) range updates. No persistence, and no correctness-boundary formalization found in the text available to us. | Orthogonal axis (dimensionality, not versioning). Flag: full TCS body not yet read; skim before asserting the absence of an invariant theorem there. Note the title of the early draft differs; cite the TCS title. |
| Standard lazy propagation (textbook/AtCoder Library `lazy_segtree` documentation) | Requires monoid plus monoid-action laws; no commutativity, because in-place push-down preserves chronological order. | The subject removes push-down. C2 characterizes exactly what that removal costs algebraically; the ordinary lazy tree is the arbitrary-action control in the taxonomy. |

## 3. Temporal aggregate indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Yang, Widom, "Incremental computation and maintenance of temporal aggregates", ICDE 2001; VLDB Journal 12(3):262-283, 2003, DOI 10.1007/s00778-003-0107-z (SB-tree) | Interior nodes keep per-interval partial aggregate values; queries accumulate contributions root-to-leaf; insertions update fully covered records at the top and recurse only into partial overlaps. Retained, never-pushed contributions, O(log_b n) update/query. Disk-based, SUM/COUNT/AVG (deletion as negative insertion, so an invertible commutative group in practice). Single-version: the time axis is data, the index itself is mutable and not versioned. **Flag: primary PDF unobtained (Stanford/Duke mirrors down, Springer paywalled); mechanism verified through Zhang et al.'s Information Systems 2003 recap and Boehlen's TIME 2018 tutorial. Obtain and read the primary before the introduction is written.** | The SB-tree is the strongest precedent for retained deferred aggregation, and the paper must credit it as such. The defensible difference: the SB-tree is not persistent; there is no copy-on-write sharing, no historical index states, and hence no cross-version tag-ordering question and no boundary theorem. |
| Zhang, Markowetz, Tsotras, Gunopulos, Seeger, "Efficient computation of temporal aggregates with range predicates", PODS 2001, DOI 10.1145/375551.375600; journal version "On computing temporal aggregates with range predicates", TODS 33(2), 2008, DOI 10.1145/1366102.1366109 (MVSB-tree) | SB-tree combined with MVBT multiversioning; range temporal aggregates with key-range predicates. SUM/COUNT/AVG only (invertible Abelian group assumed, not analyzed); external memory; query O(log_B(N/B)) I/Os, space O((N/B) log_B(N/B)), noted in the TKDE 2005 survey as potentially larger than the database. **Flag: conference and journal versions have different titles; do not conflate. Experimental sections not yet verified.** | Closest database-side relative of the subject: deferred aggregate values in a multiversion tree. Defensible differences: transaction-time versioning on disk versus in-memory path copying; commutative-group aggregates assumed rather than characterized (no boundary theorem); superlinear space versus the subject's per-update frontier; no structural optimality question posed. C2 and C3 remain open after this row. |
| Salzberg, Tsotras, "Comparison of access methods for time-evolving data", ACM Computing Surveys 31(2):158-221, 1999, DOI 10.1145/319806.319816 | Analytical survey of transaction-time/valid-time access methods; asymptotic comparison framework; no aggregation, no measurements. | Terminology anchor (partial persistence as transaction time) and survey citation; no overlap with C2-C5. |

## 4. Experimental persistence

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Pluquet, Langerman, Marot, Wuyts, "Implementing partial persistence in object-oriented languages", ALENEX 2008, pp. 37-48 | First transparent DSST-style partial persistence in Java; experimental by venue. **Flag: baseline-control details (comparison set, JIT/GC handling) not yet read; read the PDF before characterizing their methodology.** | In-memory experimental persistence exists, but for point-update object graphs with no aggregation. C5's registered, paired, cross-machine design with practical-equivalence rules has no counterpart here. |
| Pluquet, Langerman, Wuyts, "Executing code in the past: efficient in-memory object graph versioning", OOPSLA 2009, pp. 391-408, DOI 10.1145/1640089.1640118 | HistOOry: in-memory object-graph versioning, three primitives, code executed against past snapshots; validated on three applications. | Same as above; closest experimental-methodology relative for the in-memory claim, no range actions or aggregates. |

## 5. Recent buffered and multiversion indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Brodal, Rysgaard, Svenning, "External memory fully persistent search trees", STOC 2023, DOI 10.1145/3564246.3585140 | First fully persistent external-memory search tree matching ephemeral B-tree bounds; space linear in updates; theory only. | Point-update dictionary; no aggregates, tags or range actions. State of the art to contrast with, not overlap. |
| Brodal, Rysgaard, Svenning, "Buffered partially-persistent external-memory search trees", ESA 2025, DOI 10.4230/LIPIcs.ESA.2025.82, arXiv:2503.08211 | Optimal partially persistent B-epsilon-tree; buffering of point updates; space linear in updates. | Buffering here batches point updates for I/O; it does not retain range actions as queryable deferred state. C2-C4 unaffected. |

## 6. Direct implementation precedents

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Library Checker (yosupo06/library-checker-problems), problem `data_structure/persistent_range_affine_range_sum`, added December 2024 | Maintained judge problem: persistent lazy segment tree workload with range affine updates (mod 998244353), cloning of arbitrary versions, historical range sums. The bundled reference solution (nachia's generic `PersistentLazySegtree`) stores an aggregate and a lazy tag per immutable pooled node, composes tags at covered nodes, and threads an accumulated composition down the query path. Documented requirements are the monoid-action laws only; no commutativity condition and no correctness analysis appear anywhere in the problem or solution. | **The single most important audit obligation for the theory PR.** Affine actions do not commute, so either that solution's update discipline preserves chronological order in a way retained-tag accumulation alone does not (for example by pushing-with-copy on partial descent, or by an invariant under which ancestor tags are always newer), or its version-cloning model differs from ours in a way that avoids the ordering problem. PR3 must audit that code path and state precisely how the subject's model differs before any boundary claim is published. Until then, this row is recorded as prior practice for the retained-tag mechanism with no published correctness conditions. |
| ei1333 (Luzhiled) library, `persistent-lazy-red-black-tree.hpp` | Persistent lazy balanced BST whose propagation clones children when pushing. | Existing practice of the copy-on-push strategy: our ablation mirrors a real engineering pattern, which strengthens C2's relevance. |
| cp-algorithms, segment tree page, persistent section | Point updates only; no persistent lazy material. | Confirms the gap: the canonical tutorial source does not cover persistence plus lazy tags. |

## 7. Empirical methodology

| Work | Verified facts | Use |
| --- | --- | --- |
| SIGPLAN Empirical Evaluation Guidelines and Checklist, version dated 26 October 2018 (Blackburn, chair; Berger, Hauswirth, Hicks; with Krishnamurthi), sigplan.org/Resources/EmpiricalEvaluation/ | Checklist of scoped claims, suitable comparisons, principled workloads, adequate analysis. | Design standard for C5; cite Blackburn as chair, not "Berger et al.". |
| ACM Artifact Review and Badging, version 1.1, August 2020, acm.org/publications/policies/artifact-review-and-badging-current | Available, Functional, Reusable, Results Reproduced/Replicated badges. **Flag: confirm exact day of the version date from the page before citing it that precisely.** | Target expectations for the PR10 artifact. |

## 8. Verdict by contribution

| Contribution | Closest precedent | Status after audit |
| --- | --- | --- |
| C1 taxonomy | Salzberg-Tsotras framework; DSST method taxonomy | Open. No prior taxonomy spans retained tags, copy-on-push, materialization, snapshots, checkpoints, modification boxes and fat nodes under one algebraic interface. |
| C2 boundary | MVSB-tree (assumes commutative groups, no theorem); Library Checker practice (no documented conditions) | Open, with one mandatory audit: the Library Checker reference solution (section 6) before publication. |
| C3 frontier laws and lower bound | DSST/MVBT optimality for point updates; Fiat-Kaplan confluent lower bounds | Open. No space bound for partially persistent range actions found. Statement of model R is in [capability-taxonomy.md](capability-taxonomy.md) section 6. |
| C4 predictive cost model | None found for persistence-strategy selection | Open. |
| C5 registered regime study | Pluquet et al. (in-memory, point updates, no registration) | Open. |

## 9. Gate G1 assessment (30 August 2026)

Checklist state:

- Pilot preserved, labeled exploratory, reproducible with one command: **done**
  (under the local-data provenance policy of the plan).
- SB-tree and MVSB-tree read: **partially met.** Mechanisms and bounds
  verified through the cited secondary sources; primary PDFs still to be
  obtained (library access). Blocking item for the introduction, not for
  starting Phase 2 theory work.
- Every contribution has closest-precedent and difference statements: **done**
  (this document).
- Algebraic laws and composition direction complete: **done**
  (`include/valseg/policy.hpp`, capability-taxonomy.md, `tests/policy_test.cpp`).
- No generic checkpoint replay claim: **done** (capability-taxonomy.md
  section 5).
- Independent reader signs this matrix and agrees Route B is defensible:
  **pending; requires a human reviewer.**

Decision recommendation: **continue Route B.** The audit found no archival
work that analyzes retained lazy tags under copy-on-write persistence, no
commutativity-boundary theorem, and no space lower bound for persistent range
actions. Two obligations carry into Phase 2: audit the Library Checker
reference solution (C2), and read the Yang-Widom and Zhang et al. primary
texts before writing related work.
