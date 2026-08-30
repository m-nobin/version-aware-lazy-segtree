# Claim-evidence matrix

Prior-art audit for the research programme, recorded 30 August 2026. Each
family below answers: what is the closest prior mechanism, and what exact
difference can this project defend? Search-result counts are not novelty
evidence; every row cites the specific work read or verified. The required
primary-source flags found during the exit audit were resolved on 30 August
2026; later additions must be flagged until their primary text is read.

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
| Ibtehaz, Kaykobad, Rahman, "Multidimensional segment trees can do range updates in poly-logarithmic time", Theoretical Computer Science 854:30-43, 2021; primary [arXiv:1811.01226](https://arxiv.org/abs/1811.01226) text read 30 August 2026 | Emulates lazy propagation in higher dimensions with dispersed/intended updates and partial/complete queries, proves the range-sum construction correct, and derives `O(log^d n)` range-update/query time. It discusses extension to aggregate functions compatible with its repeated-combination construction. The full primary manuscript contains no persistence model and no action-order correctness boundary. | Orthogonal axis (dimensionality, not versioning). It is prior art for redesigning deferred state and for the need to state aggregate restrictions, not for C2's copy-on-write action-order question. The arXiv draft title differs from the TCS title; cite the published title. |
| Standard lazy propagation (textbook; AtCoder Library `lazy_segtree` documentation, the maintained competitive-programming library the plan's audit minimum calls proconlib) | Requires monoid plus monoid-action laws; no commutativity, because in-place push-down preserves chronological order. | The subject removes push-down. C2 characterizes exactly what that removal costs algebraically; the ordinary lazy tree is the arbitrary-action control in the taxonomy. |

## 3. Temporal aggregate indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Yang, Widom, "Incremental computation and maintenance of temporal aggregates", ICDE 2001; VLDB Journal 12(3):262-283, 2003, DOI [10.1007/s00778-003-0107-z](https://doi.org/10.1007/s00778-003-0107-z) (SB-tree) | Primary journal text read 30 August 2026. Interior nodes keep per-interval partial aggregate values; lookup accumulates values on a root-to-leaf path; an interval insertion updates a fully covered record at the highest possible level and descends on partial overlap. The disk-based B-tree/segment-tree hybrid has logarithmic lookup and update. SUM/COUNT/AVG are central; a separate min/max extension is also described, so the work must not be characterized as group-only. The data has a time dimension, but the SB-tree itself is an ephemeral mutable index: it publishes no root per index state. | The SB-tree is the strongest precedent for retained deferred aggregation, and the paper must credit it as such. The defensible difference is persistence of the index state: there is no copy-on-write sharing across published roots, hence no cross-version tag-ordering boundary or record-allocation frontier. |
| Zhang, Markowetz, Tsotras, Gunopulos, Seeger, "Efficient computation of temporal aggregates with range predicates", PODS 2001, DOI 10.1145/375551.375600; journal version "On computing temporal aggregates with range predicates", TODS 33(2), 2008, DOI [10.1145/1366102.1366109](https://doi.org/10.1145/1366102.1366109) (MVSB-tree) | Primary 33-page journal manuscript read 30 August 2026 from the authors' [UCR copy](https://www.cs.ucr.edu/~tsotras/functional/1.pdf), including structure, complexity and experiments. It explicitly makes an SB-tree partially persistent under transaction-time order, copies/splits disk pages, and answers key/time rectangle SUM by four dominance-sum queries over two MVSB-trees. It proves `O(log_b n)` query I/Os, `O(log_b K)` update I/Os and `O((n/b) log_b K)` blocks; experiments use one million intervals. COUNT is a SUM special case and the functional extension still uses fixed-size additive values. | Closest database-side relative of the subject. The meaningful differences are the problem and representation: ordered point insertions into an external-memory dominance-sum index versus in-memory range actions over a fixed canonical partition; page multiversioning versus immutable per-update record frontiers; additive values assumed rather than an action-order boundary; no lower bound for the subject's model R. The paper is direct precedent for persistence plus retained aggregate contributions, so novelty language must be narrower than "first persistent lazy aggregate tree." |
| Salzberg, Tsotras, "Comparison of access methods for time-evolving data", ACM Computing Surveys 31(2):158-221, 1999, DOI 10.1145/319806.319816 | Analytical survey of transaction-time/valid-time access methods; asymptotic comparison framework; no aggregation, no measurements. | Terminology anchor (partial persistence as transaction time) and survey citation; no overlap with C2-C5. |

## 4. Experimental persistence

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Pluquet, Langerman, Marot, Wuyts, "Implementing partial persistence in object-oriented languages", ALENEX 2008, pp. 37-48, DOI [10.1137/1.9781611972887.4](https://doi.org/10.1137/1.9781611972887.4); primary [author PDF](https://fpluquet.be/Publications_%26_Talks_files/Implementing%20Partial%20Persistence%20in%20Object-Oriented%20Languages.pdf) read 30 August 2026 | First transparent fat-node-style partial persistence in Java/AspectJ. Experiments used one dual-2 GHz PowerPC G5, fixed Java/AspectJ versions and heap options, disabled automatic GC with a manual collection before each test, and disabled JIT for the persistence-aspect decomposition. It compared state storage with growable arrays; native Java, successive instrumentation layers and snapshot frequency; and ephemeral versus persistent treaps, plus point location and memory overhead. Operations were repeated and reported as average time; no paired randomized order, practical-equivalence rule or cross-machine replication is described. | In-memory experimental persistence and controlled component decomposition are established precedent, but for field updates and object graphs with no range actions or aggregates. C5 must credit this work and defend its registered, paired, cross-machine regime study as the narrower difference. |
| Pluquet, Langerman, Wuyts, "Executing code in the past: efficient in-memory object graph versioning", OOPSLA 2009, pp. 391-408, DOI 10.1145/1640089.1640118 | HistOOry: in-memory object-graph versioning, three primitives, code executed against past snapshots; validated on three applications. | Same as above; closest experimental-methodology relative for the in-memory claim, no range actions or aggregates. |
| Kaplan, "Persistent data structures", in Mehta and Sahni (eds.), Handbook of Data Structures and Applications, CRC Press, 2004, chapter 31 | Survey of partial, full and confluent persistence techniques (fat node, node copying, path copying, node splitting) with their space and time trade-offs. **Flag: added 30 August 2026 as the later comparative reference for this family; primary chapter not yet re-read for this audit.** | Comparative survey of techniques, not measurements; cite for the technique taxonomy C1 refines, not as experimental precedent. |

## 5. Recent buffered and multiversion indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Brodal, Rysgaard, Svenning, "External memory fully persistent search trees", STOC 2023, DOI 10.1145/3564246.3585140 | First fully persistent external-memory search tree matching ephemeral B-tree bounds; space linear in updates; theory only. | Point-update dictionary; no aggregates, tags or range actions. State of the art to contrast with, not overlap. |
| Brodal, Rysgaard, Svenning, "Buffered partially-persistent external-memory search trees", ESA 2025, DOI 10.4230/LIPIcs.ESA.2025.82, arXiv:2503.08211 | Optimal partially persistent B-epsilon-tree; buffering of point updates; space linear in updates. | Buffering here batches point updates for I/O; it does not retain range actions as queryable deferred state. C2-C4 unaffected. |
| Kaufmann, Manjili, Vagenas, Fischer, Kossmann, Faerber, May, "Timeline index: a unified data structure for processing queries on temporal data in SAP HANA", SIGMOD 2013, DOI 10.1145/2463676.2465293 | In-memory temporal index over a column store: an event list plus periodic checkpoints answers temporal aggregation, time travel and temporal joins. **Flag: added 30 August 2026 as the current in-memory temporal-index reference; primary text not yet read for this audit.** | Checkpoint-plus-log in memory, so it is the closest same-purpose relative of the checkpoint baseline, not of the subject. Its aggregation is additive and it publishes no immutable per-update tree records; it does not pose C2 or C3. |

## 6. Direct implementation precedents

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Library Checker (`yosupo06/library-checker-problems`), problem `data_structure/persistent_range_affine_range_sum` | Source audited at upstream commit [`04c8de378bab67be926325de2871f0babb8e6451`](https://github.com/yosupo06/library-checker-problems/commit/04c8de378bab67be926325de2871f0babb8e6451) on 30 August 2026. The judge supports branching copies, range-affine updates, crossover and historical sums modulo 998244353. Nachia's `PersistentLazySegtree` stores aggregate and tag per immutable pooled node, but a partial update computes the accumulated ancestor composition and calls `applyAtNode` on both children (including the disjoint child); the old tags are therefore materialized into copied child roots before descent. | This is copy-on-push, not the subject's retained-ancestor-tag strategy. It is a strong direct implementation precedent for persistent lazy propagation with arbitrary noncommutative actions and belongs beside the copy-on-push ablation. Its chronological-order discipline supports, rather than subsumes, C2's proposed boundary. |
| ei1333 (Luzhiled) library, `persistent-lazy-red-black-tree.hpp` | Persistent lazy balanced BST whose propagation clones children when pushing. | Existing practice of the copy-on-push strategy: our ablation mirrors a real engineering pattern, which strengthens C2's relevance. |
| cp-algorithms, segment tree page, persistent section | Point updates only; no persistent lazy material. | Confirms the gap: the canonical tutorial source does not cover persistence plus lazy tags. |

## 7. Empirical methodology

| Work | Verified facts | Use |
| --- | --- | --- |
| SIGPLAN Empirical Evaluation Guidelines and Checklist, version dated 26 October 2018 (Blackburn, chair; Berger, Hauswirth, Hicks; with Krishnamurthi), sigplan.org/Resources/EmpiricalEvaluation/ | Checklist of scoped claims, suitable comparisons, principled workloads, adequate analysis. | Design standard for C5; cite Blackburn as chair, not "Berger et al.". |
| ACM [Artifact Review and Badging](https://www.acm.org/publications/policies/artifact-review-and-badging-current), version 1.1, 24 August 2020 | Defines Artifacts Available; Artifacts Evaluated--Functional/Reusable; and Results Reproduced/Replicated. The date and terminology were verified from the current ACM policy page on 30 August 2026. | Target expectations for the PR10 artifact. |
| Johnson, "A theoretician's guide to the experimental analysis of algorithms", in Data Structures, Near Neighbor Searches, and Methodology: Fifth and Sixth DIMACS Implementation Challenges, AMS, 2002, pp. 215-250 | Experimental-algorithmics guidance: reproducibility, comparable baselines, honest reporting of variance and machine dependence, pitfalls in timing. **Flag: added 30 August 2026; primary text not yet re-read for this audit.** | Design standard for C5 beside the SIGPLAN checklist; the plan's "experimental-algorithmics guidance" item. |
| McGeoch, "A Guide to Experimental Algorithmics", Cambridge University Press, 2012 | Book-length treatment of experimental design, measurement and analysis for algorithm studies. **Flag: added 30 August 2026; primary text not yet re-read for this audit.** | Same use as Johnson; cite for the paired-design and variance-reduction choices in PR6. |

## 7a. Audit fields for the closest items

The plan asks for eight fields per close item. The rows above carry them in
prose; this table states them side by side for the five items that decide
Gate G1.

| Item | Semantic operation model | Persistence model | Memory model | Algebraic scope | Asymptotic bounds | Measured evidence | Artifact availability | Exact difference defended |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DSST 1989 | Point field updates on linked structures | Partial (fat node, node copying) and full (node splitting) | Pointer machine | None (no aggregation) | O(1) amortized space per field change | None | None (theory) | No range actions or deferred aggregate state |
| SB-tree (Yang-Widom 2003) | Interval insertion with additive value; time-range aggregate lookup | None: one mutable index | External memory, B-tree pages | SUM/COUNT/AVG, separate min/max extension | Logarithmic lookup and update | Yes, disk-based experiments in the journal version | Not released with the paper | No published roots, no copy-on-write sharing, no cross-version tag order |
| MVSB-tree (Zhang et al. 2008) | Ordered point insertions; key/time rectangle SUM | Partial, transaction time, page multiversioning | External memory | Fixed-size additive values | `O(log_b n)` query I/Os, `O(log_b K)` update I/Os, `O((n/b) log_b K)` blocks | Yes, one million intervals | Not released with the paper | Dominance-sum index versus in-memory range actions over a canonical partition; additive values assumed rather than characterized; no record-frontier bound |
| Pluquet et al. 2008 | Field updates on Java object graphs | Partial, fat-node style | JVM heap, GC disabled for runs | None | DSST bounds | Yes, single PowerPC G5 machine, averaged repeats | Not released with the paper | No range actions or aggregates; no paired, registered, cross-machine design |
| Library Checker `persistent_range_affine_range_sum` | Range affine update, range sum, clone any version | Full (branching roots) | Pooled immutable nodes in memory | Affine actions mod 998244353 (noncommutative) | `O(log n)` per operation | Judge time limits only | Public source at the audited commit | Copy-on-push materializes accumulated tags into copied children before descent; it is the ablation's precedent, not the subject's |

## 8. Verdict by contribution

| Contribution | Closest precedent | Status after audit |
| --- | --- | --- |
| C1 taxonomy | Salzberg-Tsotras framework; DSST method taxonomy | Open. No prior taxonomy spans retained tags, copy-on-push, materialization, snapshots, checkpoints, modification boxes and fat nodes under one algebraic interface. |
| C2 boundary | MVSB-tree (additive dominance sums, no action-order theorem); Library Checker (copy-on-push preserves order) | Established at G2. Sufficiency, conditional necessity, the faithful-action corollary and the minimal AffineSum witness are proved in `docs/proof.md` section 9, independently reviewed in section 9.10 and exercised by `tests/policy_trees_test.cpp`. |
| C3 frontier laws and lower bound | DSST/MVBT optimality for point updates; Fiat-Kaplan confluent lower bounds | Frontier laws established at G2; lower bound withdrawn. Section 10 gives exact `F`, tight perfect-tree global maxima, `F + 2P`, the point-materialization identity and executable range-family expectations. The edge-tag counterexample defeats the lower bound inside unchanged model R, so no optimality claim survives. |
| C4 predictive cost model | None found for persistence-strategy selection | Model form and physical units frozen at G2; predictive success remains open until the registered Phase 3 holdout. |
| C5 registered regime study | Pluquet et al. (in-memory, point updates, no registration) | Open. |

## 9. Gate G1 assessment (30 August 2026)

Checklist state:

- Pilot preserved, labeled exploratory, reproducible with one command: **done**
  (under the local-data provenance policy of the plan).
- SB-tree and MVSB-tree primary texts read: **done.** The matrix now records
  the mechanisms, aggregate scope, bounds and exact persistence model without
  relying on a secondary-source characterization.
- Direct Library Checker implementation audited at a fixed upstream commit:
  **done.** It is copy-on-push, not retained-tag accumulation.
- Every contribution has closest-precedent and difference statements: **done**
  (this document).
- Algebraic laws and composition direction complete: **done**
  (`include/valseg/policy.hpp`, capability-taxonomy.md, `tests/policy_test.cpp`).
- No generic checkpoint replay claim: **done** (capability-taxonomy.md
  section 5).
- All primary-source flags raised by this Phase 1 audit: **resolved.** The
  Ibtehaz et al. and Pluquet et al. full manuscripts were read, and the ACM
  v1.1 policy date was verified from the official page.
- Independent reader signs this matrix and agrees Route B is defensible:
  **done, 30 August 2026; see section 10.**

Decision recommendation: **continue Route B.** The audit found direct and
important precedent for retained aggregate contributions (SB/MVSB) and for
persistent copy-on-push affine propagation (Library Checker), but no archival
work that analyzes retained ancestor tags under copy-on-write persistence, no
commutativity-boundary theorem, and no space lower bound for persistent range
actions in model R. Novelty claims must name that narrower boundary.

## 10. Independent review record

This section must be completed by a reader who did not implement the Phase 1
changes. A repository author may prepare the record but may not self-attest it.

| Field | Review record |
| --- | --- |
| Reviewer name | Sunjare Zulfiker |
| Independence basis | Second project contributor; reviewer of the operation model and specification under the execution plan; did not author the Phase 1 changes |
| Material reviewed | This matrix, cited primary sources, capability taxonomy and Gate G1 recommendation |
| Decision | Approve Route B with the narrower novelty language of section 9 |
| Required changes and disposition | None recorded at sign-off; the exit-audit fixes in the Phase 1 closeout commits (ordering statement in capability-taxonomy.md section 2.2, audit-minimum rows above) were applied in the same pass |
| Date | 30 August 2026 |
| Signature or review-link evidence | Recorded by the repository owner on the reviewer's confirmation; the closeout commit on the Phase 1 pull requests is the review link |
